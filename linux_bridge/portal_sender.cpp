#include "frame_sender.h"
#include "frame_convert.h"

#include "../bridge/frame_protocol.h"

#include <gio/gio.h>
#include <glib-unix.h>
#include <pipewire/pipewire.h>
#include <spa/param/video/format-utils.h>
#include <spa/utils/result.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <unistd.h>
#include <vector>

namespace {

constexpr const char* kPortalBus = "org.freedesktop.portal.Desktop";
constexpr const char* kPortalPath = "/org/freedesktop/portal/desktop";
constexpr const char* kScreenCastInterface = "org.freedesktop.portal.ScreenCast";

struct RequestWaiter
{
    GMainLoop* loop{};
    bool completed{};
    uint32_t response{ 2 };
    GVariant* results{};
};

void OnRequestResponse(GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar*,
                       GVariant* parameters, gpointer userData)
{
    auto* waiter = static_cast<RequestWaiter*>(userData);
    g_variant_get(parameters, "(u@a{sv})", &waiter->response, &waiter->results);
    waiter->completed = true;
    g_main_loop_quit(waiter->loop);
}

gboolean OnPortalInterrupt(gpointer userData)
{
    auto* waiter = static_cast<RequestWaiter*>(userData);
    waiter->response = 1;
    waiter->completed = true;
    g_main_loop_quit(waiter->loop);
    return G_SOURCE_CONTINUE;
}

std::string NewToken(const char* prefix)
{
    return std::string(prefix) + std::to_string(getpid()) + "_" + std::to_string(g_random_int());
}

std::string RequestPath(GDBusConnection* connection, const std::string& token)
{
    std::string sender = g_dbus_connection_get_unique_name(connection);
    if (!sender.empty() && sender.front() == ':') sender.erase(sender.begin());
    std::replace(sender.begin(), sender.end(), '.', '_');
    return "/org/freedesktop/portal/desktop/request/" + sender + "/" + token;
}

bool RunPortalRequest(GDBusConnection* connection, const char* method, GVariant* parameters,
                      const std::string& token, GVariant** results)
{
    RequestWaiter waiter;
    waiter.loop = g_main_loop_new(nullptr, FALSE);
    const std::string expectedPath = RequestPath(connection, token);
    const guint subscription = g_dbus_connection_signal_subscribe(
        connection, kPortalBus, "org.freedesktop.portal.Request", "Response",
        expectedPath.c_str(), nullptr, G_DBUS_SIGNAL_FLAGS_NONE,
        OnRequestResponse, &waiter, nullptr);
    const guint interruptSource = g_unix_signal_add(SIGINT, OnPortalInterrupt, &waiter);

    GError* error = nullptr;
    GVariant* callResult = g_dbus_connection_call_sync(
        connection, kPortalBus, kPortalPath, kScreenCastInterface, method, parameters,
        G_VARIANT_TYPE("(o)"), G_DBUS_CALL_FLAGS_NONE, -1, nullptr, &error);
    if (!callResult) {
        std::cerr << method << " failed: " << error->message << '\n';
        g_error_free(error);
        g_source_remove(interruptSource);
        g_dbus_connection_signal_unsubscribe(connection, subscription);
        g_main_loop_unref(waiter.loop);
        return false;
    }
    g_variant_unref(callResult);

    if (!waiter.completed) g_main_loop_run(waiter.loop);
    g_source_remove(interruptSource);
    g_dbus_connection_signal_unsubscribe(connection, subscription);
    g_main_loop_unref(waiter.loop);

    if (waiter.response != 0) {
        std::cerr << method << (waiter.response == 1 ? " was cancelled\n" : " was denied or failed\n");
        if (waiter.results) g_variant_unref(waiter.results);
        return false;
    }
    *results = waiter.results;
    return true;
}

GVariant* FinishOptions(GVariantBuilder& builder)
{
    return g_variant_builder_end(&builder);
}

bool CreateSession(GDBusConnection* connection, std::string& sessionHandle)
{
    const std::string requestToken = NewToken("pts_create_");
    const std::string sessionToken = NewToken("pts_session_");
    GVariantBuilder options;
    g_variant_builder_init(&options, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add(&options, "{sv}", "handle_token", g_variant_new_string(requestToken.c_str()));
    g_variant_builder_add(&options, "{sv}", "session_handle_token", g_variant_new_string(sessionToken.c_str()));

    GVariant* results = nullptr;
    if (!RunPortalRequest(connection, "CreateSession",
                          g_variant_new("(@a{sv})", FinishOptions(options)), requestToken, &results)) {
        return false;
    }

    const char* handle = nullptr;
    const bool found = g_variant_lookup(results, "session_handle", "&s", &handle);
    if (found) sessionHandle = handle;
    g_variant_unref(results);
    if (!found) std::cerr << "CreateSession response had no session_handle\n";
    return found;
}

bool SelectSources(GDBusConnection* connection, const std::string& sessionHandle)
{
    const std::string token = NewToken("pts_select_");
    GVariantBuilder options;
    g_variant_builder_init(&options, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add(&options, "{sv}", "handle_token", g_variant_new_string(token.c_str()));
    g_variant_builder_add(&options, "{sv}", "types", g_variant_new_uint32(1u | 2u));
    g_variant_builder_add(&options, "{sv}", "multiple", g_variant_new_boolean(FALSE));

    GVariant* results = nullptr;
    const bool ok = RunPortalRequest(connection, "SelectSources",
        g_variant_new("(o@a{sv})", sessionHandle.c_str(), FinishOptions(options)), token, &results);
    if (results) g_variant_unref(results);
    return ok;
}

bool StartSession(GDBusConnection* connection, const std::string& sessionHandle, uint32_t& nodeId)
{
    const std::string token = NewToken("pts_start_");
    GVariantBuilder options;
    g_variant_builder_init(&options, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add(&options, "{sv}", "handle_token", g_variant_new_string(token.c_str()));

    GVariant* results = nullptr;
    if (!RunPortalRequest(connection, "Start",
            g_variant_new("(os@a{sv})", sessionHandle.c_str(), "", FinishOptions(options)),
            token, &results)) {
        return false;
    }

    GVariant* streams = g_variant_lookup_value(results, "streams", G_VARIANT_TYPE("a(ua{sv})"));
    bool found = false;
    if (streams) {
        GVariantIter iter;
        GVariant* properties = nullptr;
        g_variant_iter_init(&iter, streams);
        found = g_variant_iter_next(&iter, "(u@a{sv})", &nodeId, &properties);
        if (properties) g_variant_unref(properties);
        g_variant_unref(streams);
    }
    g_variant_unref(results);
    if (!found) std::cerr << "Start response contained no PipeWire stream\n";
    return found;
}

int OpenPipeWireRemote(GDBusConnection* connection, const std::string& sessionHandle)
{
    GVariantBuilder options;
    g_variant_builder_init(&options, G_VARIANT_TYPE_VARDICT);
    GUnixFDList* outputFds = nullptr;
    GError* error = nullptr;
    GVariant* result = g_dbus_connection_call_with_unix_fd_list_sync(
        connection, kPortalBus, kPortalPath, kScreenCastInterface, "OpenPipeWireRemote",
        g_variant_new("(o@a{sv})", sessionHandle.c_str(), FinishOptions(options)),
        G_VARIANT_TYPE("(h)"), G_DBUS_CALL_FLAGS_NONE, -1, nullptr, &outputFds, nullptr, &error);
    if (!result) {
        std::cerr << "OpenPipeWireRemote failed: " << error->message << '\n';
        g_error_free(error);
        return -1;
    }

    int handleIndex = -1;
    g_variant_get(result, "(h)", &handleIndex);
    const int fd = outputFds ? g_unix_fd_list_get(outputFds, handleIndex, &error) : -1;
    if (fd < 0) {
        std::cerr << "Could not retrieve PipeWire file descriptor";
        if (error) {
            std::cerr << ": " << error->message;
            g_error_free(error);
        }
        std::cerr << '\n';
    }
    g_variant_unref(result);
    if (outputFds) g_object_unref(outputFds);
    return fd;
}

void CloseSession(GDBusConnection* connection, const std::string& sessionHandle)
{
    GError* error = nullptr;
    GVariant* result = g_dbus_connection_call_sync(
        connection, kPortalBus, sessionHandle.c_str(), "org.freedesktop.portal.Session", "Close",
        nullptr, nullptr, G_DBUS_CALL_FLAGS_NONE, 1000, nullptr, &error);
    if (result) g_variant_unref(result);
    if (error) g_error_free(error);
}

uint32_t ParseNumber(const char* value, const char* name)
{
    char* end = nullptr;
    errno = 0;
    const unsigned long parsed = std::strtoul(value, &end, 10);
    if (errno != 0 || !end || *end != '\0' || parsed > UINT32_MAX) {
        std::cerr << "Invalid " << name << ": " << value << '\n';
        std::exit(2);
    }
    return static_cast<uint32_t>(parsed);
}

struct CaptureState
{
    pw_main_loop* loop{};
    pw_stream* stream{};
    spa_hook streamListener{};
    spa_video_info_raw format{};
    FrameSender* sender{};
    uint32_t fps{};
    std::chrono::steady_clock::time_point lastFrame{};
};

void OnStreamStateChanged(void* userData, pw_stream_state, pw_stream_state state, const char* error)
{
    auto* capture = static_cast<CaptureState*>(userData);
    std::cout << "PipeWire: " << pw_stream_state_as_string(state) << '\n';
    if (state == PW_STREAM_STATE_ERROR || state == PW_STREAM_STATE_UNCONNECTED) {
        if (error) std::cerr << "PipeWire stream error: " << error << '\n';
        pw_main_loop_quit(capture->loop);
    }
}

void OnStreamFormatChanged(void* userData, uint32_t id, const spa_pod* parameter)
{
    auto* capture = static_cast<CaptureState*>(userData);
    if (!parameter || id != SPA_PARAM_Format) return;
    if (spa_format_video_raw_parse(parameter, &capture->format) < 0) {
        std::cerr << "Could not parse negotiated PipeWire video format\n";
        pw_main_loop_quit(capture->loop);
        return;
    }
    std::cout << "Capturing " << capture->format.size.width << 'x' << capture->format.size.height << '\n';
}

bool ConvertFrame(const CaptureState& capture, const spa_data& plane,
                  uint32_t& outputWidth, uint32_t& outputHeight, std::vector<uint8_t>& output)
{
    const uint32_t sourceWidth = capture.format.size.width;
    const uint32_t sourceHeight = capture.format.size.height;
    if (!plane.data || !plane.chunk || sourceWidth == 0 || sourceHeight == 0) return false;

    PixelLayout layout;
    switch (capture.format.format) {
    case SPA_VIDEO_FORMAT_RGBA:
        layout = PixelLayout::Rgba;
        break;
    case SPA_VIDEO_FORMAT_RGBx:
        layout = PixelLayout::Rgbx;
        break;
    case SPA_VIDEO_FORMAT_BGRA:
        layout = PixelLayout::Bgra;
        break;
    case SPA_VIDEO_FORMAT_BGRx:
        layout = PixelLayout::Bgrx;
        break;
    default:
        return false;
    }
    if (plane.chunk->stride < 0) return false;
    return ConvertToLimitedRgba(static_cast<const uint8_t*>(plane.data), plane.maxsize,
        plane.chunk->offset, static_cast<uint32_t>(plane.chunk->stride), sourceWidth, sourceHeight,
        layout, bridge_protocol::kMaxWidth, bridge_protocol::kMaxHeight,
        outputWidth, outputHeight, output);
}

void OnStreamProcess(void* userData)
{
    auto* capture = static_cast<CaptureState*>(userData);
    pw_buffer* pipewireBuffer = pw_stream_dequeue_buffer(capture->stream);
    if (!pipewireBuffer) return;

    const auto now = std::chrono::steady_clock::now();
    const auto interval = std::chrono::microseconds(1000000 / capture->fps);
    if (capture->lastFrame.time_since_epoch().count() == 0 || now - capture->lastFrame >= interval) {
        spa_buffer* buffer = pipewireBuffer->buffer;
        if (buffer && buffer->n_datas > 0) {
            uint32_t width = 0;
            uint32_t height = 0;
            std::vector<uint8_t> pixels;
            if (ConvertFrame(*capture, buffer->datas[0], width, height, pixels)) {
                capture->sender->Publish(width, height, std::move(pixels));
                capture->lastFrame = now;
            }
        }
    }
    pw_stream_queue_buffer(capture->stream, pipewireBuffer);
}

void OnPipeWireSignal(void* userData, int)
{
    pw_main_loop_quit(static_cast<CaptureState*>(userData)->loop);
}

const pw_stream_events kStreamEvents = [] {
    pw_stream_events events{};
    events.version = PW_VERSION_STREAM_EVENTS;
    events.state_changed = OnStreamStateChanged;
    events.param_changed = OnStreamFormatChanged;
    events.process = OnStreamProcess;
    return events;
}();

} // namespace

int main(int argc, char** argv)
{
    uint32_t fps = 15;
    uint32_t port = bridge_protocol::kDefaultPort;
    if (argc > 1) fps = ParseNumber(argv[1], "fps");
    if (argc > 2) port = ParseNumber(argv[2], "port");
    if (argc > 3 || fps == 0 || fps > 30 || port == 0 || port > 65535) {
        std::cerr << "Usage: " << argv[0] << " [fps 1-30 [port 1-65535]]\n";
        return 2;
    }

    GError* error = nullptr;
    GDBusConnection* connection = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &error);
    if (!connection) {
        std::cerr << "Could not connect to the session D-Bus: " << error->message << '\n';
        g_error_free(error);
        return 1;
    }

    std::string sessionHandle;
    uint32_t nodeId = 0;
    if (!CreateSession(connection, sessionHandle) ||
        !SelectSources(connection, sessionHandle) ||
        !StartSession(connection, sessionHandle, nodeId)) {
        if (!sessionHandle.empty()) CloseSession(connection, sessionHandle);
        g_object_unref(connection);
        return 1;
    }

    const int pipewireFd = OpenPipeWireRemote(connection, sessionHandle);
    if (pipewireFd < 0) {
        CloseSession(connection, sessionHandle);
        g_object_unref(connection);
        return 1;
    }

    pw_init(&argc, &argv);
    CaptureState capture;
    capture.fps = fps;
    capture.loop = pw_main_loop_new(nullptr);
    pw_context* context = capture.loop
        ? pw_context_new(pw_main_loop_get_loop(capture.loop), nullptr, 0)
        : nullptr;
    pw_core* core = context ? pw_context_connect_fd(context, pipewireFd, nullptr, 0) : nullptr;
    if (!capture.loop || !context || !core) {
        std::cerr << "Could not connect to the portal PipeWire remote\n";
        if (core) pw_core_disconnect(core);
        if (context) pw_context_destroy(context);
        if (capture.loop) pw_main_loop_destroy(capture.loop);
        CloseSession(connection, sessionHandle);
        g_object_unref(connection);
        pw_deinit();
        return 1;
    }

    FrameSender sender(static_cast<uint16_t>(port));
    capture.sender = &sender;
    sender.Start();

    pw_properties* properties = pw_properties_new(
        PW_KEY_MEDIA_TYPE, "Video",
        PW_KEY_MEDIA_CATEGORY, "Capture",
        PW_KEY_MEDIA_ROLE, "Screen",
        nullptr);
    capture.stream = pw_stream_new(core, "Prism Texture Streamer portal capture", properties);
    if (!capture.stream) {
        std::cerr << "Could not create PipeWire capture stream\n";
        sender.Stop();
        pw_core_disconnect(core);
        pw_context_destroy(context);
        pw_main_loop_destroy(capture.loop);
        pw_deinit();
        CloseSession(connection, sessionHandle);
        g_object_unref(connection);
        return 1;
    }
    pw_stream_add_listener(capture.stream, &capture.streamListener, &kStreamEvents, &capture);

    uint8_t podBuffer[1024];
    spa_pod_builder builder = SPA_POD_BUILDER_INIT(podBuffer, sizeof(podBuffer));
    spa_rectangle defaultSize = SPA_RECTANGLE(640, 360);
    spa_rectangle minimumSize = SPA_RECTANGLE(1, 1);
    spa_rectangle maximumSize = SPA_RECTANGLE(8192, 8192);
    spa_fraction defaultRate = SPA_FRACTION(fps, 1);
    spa_fraction minimumRate = SPA_FRACTION(0, 1);
    spa_fraction maximumRate = SPA_FRACTION(60, 1);
    const spa_pod* parameters[] = {
        static_cast<const spa_pod*>(spa_pod_builder_add_object(&builder,
            SPA_TYPE_OBJECT_Format, SPA_PARAM_EnumFormat,
            SPA_FORMAT_mediaType, SPA_POD_Id(SPA_MEDIA_TYPE_video),
            SPA_FORMAT_mediaSubtype, SPA_POD_Id(SPA_MEDIA_SUBTYPE_raw),
            SPA_FORMAT_VIDEO_format, SPA_POD_CHOICE_ENUM_Id(5,
                SPA_VIDEO_FORMAT_RGBx, SPA_VIDEO_FORMAT_RGBx, SPA_VIDEO_FORMAT_RGBA,
                SPA_VIDEO_FORMAT_BGRx, SPA_VIDEO_FORMAT_BGRA),
            SPA_FORMAT_VIDEO_size, SPA_POD_CHOICE_RANGE_Rectangle(
                &defaultSize, &minimumSize, &maximumSize),
            SPA_FORMAT_VIDEO_framerate, SPA_POD_CHOICE_RANGE_Fraction(
                &defaultRate, &minimumRate, &maximumRate)))
    };

    const pw_stream_flags flags = static_cast<pw_stream_flags>(
        PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS);
    const int connectResult = pw_stream_connect(
        capture.stream, PW_DIRECTION_INPUT, nodeId, flags, parameters, 1);
    if (connectResult < 0) {
        std::cerr << "Could not connect PipeWire stream: " << spa_strerror(connectResult) << '\n';
    } else {
        pw_loop_add_signal(pw_main_loop_get_loop(capture.loop), SIGINT, OnPipeWireSignal, &capture);
        pw_loop_add_signal(pw_main_loop_get_loop(capture.loop), SIGTERM, OnPipeWireSignal, &capture);
        std::cout << "Portal capture started at up to " << fps << " FPS; press Ctrl+C to stop\n";
        pw_main_loop_run(capture.loop);
    }

    sender.Stop();
    pw_stream_destroy(capture.stream);
    pw_core_disconnect(core);
    pw_context_destroy(context);
    pw_main_loop_destroy(capture.loop);
    pw_deinit();
    CloseSession(connection, sessionHandle);
    g_object_unref(connection);
    return connectResult < 0 ? 1 : 0;
}
