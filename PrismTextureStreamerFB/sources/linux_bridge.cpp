#include <WinSock2.h>
#include <WS2tcpip.h>

#include "linux_bridge.h"

#include "../../bridge/control_protocol.h"
#include "../../bridge/frame_protocol.h"
#include "../scs_logging.h"

#include <array>
#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#pragma comment(lib, "ws2_32.lib")

using namespace scs_logging;

namespace sources {
namespace {

class LinuxBridgeSource final : public IContentSource
{
public:
    explicit LinuxBridgeSource(uint16_t port) : m_port(port) {}

    ~LinuxBridgeSource() override
    {
        m_stopRequested = true;
        if (m_stopEvent != WSA_INVALID_EVENT) WSASetEvent(m_stopEvent);
        if (m_thread.joinable()) m_thread.join();

        if (m_listener != INVALID_SOCKET) closesocket(m_listener);
        if (m_listenEvent != WSA_INVALID_EVENT) WSACloseEvent(m_listenEvent);
        if (m_stopEvent != WSA_INVALID_EVENT) WSACloseEvent(m_stopEvent);
        if (m_winsockStarted) WSACleanup();

        scs_log(0, "[LinuxBridgeSource] Listener on 127.0.0.1:%u stopped", static_cast<unsigned>(m_port));
    }

    bool Start()
    {
        WSADATA data{};
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
            scs_log(2, "[LinuxBridgeSource] WSAStartup failed: %d", WSAGetLastError());
            return false;
        }
        m_winsockStarted = true;

        m_stopEvent = WSACreateEvent();
        m_listenEvent = WSACreateEvent();
        if (m_stopEvent == WSA_INVALID_EVENT || m_listenEvent == WSA_INVALID_EVENT) {
            scs_log(2, "[LinuxBridgeSource] Failed to create socket events: %d", WSAGetLastError());
            return false;
        }

        m_listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (m_listener == INVALID_SOCKET) {
            scs_log(2, "[LinuxBridgeSource] socket failed: %d", WSAGetLastError());
            return false;
        }

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(m_port);
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

        if (bind(m_listener, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR) {
            scs_log(2, "[LinuxBridgeSource] bind to 127.0.0.1:%u failed: %d", static_cast<unsigned>(m_port), WSAGetLastError());
            return false;
        }
        if (listen(m_listener, 1) == SOCKET_ERROR) {
            scs_log(2, "[LinuxBridgeSource] listen failed: %d", WSAGetLastError());
            return false;
        }
        if (WSAEventSelect(m_listener, m_listenEvent, FD_ACCEPT | FD_CLOSE) == SOCKET_ERROR) {
            scs_log(2, "[LinuxBridgeSource] WSAEventSelect failed: %d", WSAGetLastError());
            return false;
        }

        m_thread = std::thread(&LinuxBridgeSource::Run, this);
        scs_log(0, "[LinuxBridgeSource] Listening on 127.0.0.1:%u", static_cast<unsigned>(m_port));
        return true;
    }

    uint32_t GetWidth() const override { return m_copiedWidth.load(); }
    uint32_t GetHeight() const override { return m_copiedHeight.load(); }
    void SetFramerate(uint8_t) override {}
    bool IsConnected() const override { return m_clientConnected.load(); }

    bool RequestCapture() override
    {
        std::lock_guard<std::mutex> lock(m_clientMutex);
        return m_client != INVALID_SOCKET && SendStartCapture(m_client);
    }

    bool CopyLatestFrame(std::vector<uint8_t>& dst) override
    {
        std::lock_guard<std::mutex> lock(m_bufferMutex);
        if (!m_haveFrame) return false;

        dst = m_frameBuffer;
        // Only this consumer updates the public dimensions, so a resolution
        // change cannot make GetWidth/GetHeight describe a different buffer.
        m_copiedWidth = m_frameWidth;
        m_copiedHeight = m_frameHeight;
        return true;
    }

private:
    bool SendStartCapture(SOCKET client)
    {
        bridge_protocol::ControlMessage message{
            bridge_protocol::ControlCommand::StartCapture
        };
        std::array<uint8_t, bridge_protocol::kControlMessageSize> encoded{};
        std::string error;
        if (!bridge_protocol::EncodeControlMessage(message, encoded, &error)) {
            scs_log(2, "[LinuxBridgeSource] Could not encode start command: %s", error.c_str());
            return false;
        }

        size_t sentTotal = 0;
        while (sentTotal < encoded.size()) {
            const int sent = send(client,
                reinterpret_cast<const char*>(encoded.data() + sentTotal),
                static_cast<int>(encoded.size() - sentTotal), 0);
            if (sent <= 0) {
                scs_log(1, "[LinuxBridgeSource] Could not request portal capture: %d", WSAGetLastError());
                return false;
            }
            sentTotal += static_cast<size_t>(sent);
        }
        return true;
    }

    void Publish(bridge_protocol::Frame&& frame)
    {
        std::lock_guard<std::mutex> lock(m_bufferMutex);
        m_frameBuffer = std::move(frame.pixels);
        m_frameWidth = frame.header.width;
        m_frameHeight = frame.header.height;
        m_haveFrame = true;
    }

    void MarkDisconnected()
    {
        std::lock_guard<std::mutex> lock(m_bufferMutex);
        m_haveFrame = false;
    }

    bool ReceiveClient(SOCKET client)
    {
        WSAEVENT clientEvent = WSACreateEvent();
        if (clientEvent == WSA_INVALID_EVENT) return false;

        if (WSAEventSelect(client, clientEvent, FD_READ | FD_CLOSE) == SOCKET_ERROR) {
            WSACloseEvent(clientEvent);
            return false;
        }

        bridge_protocol::FrameStreamParser parser;
        std::array<uint8_t, 64 * 1024> input{};
        WSAEVENT events[] = { m_stopEvent, clientEvent };
        bool keepRunning = true;

        while (!m_stopRequested.load() && keepRunning) {
            const DWORD result = WSAWaitForMultipleEvents(2, events, FALSE, WSA_INFINITE, FALSE);
            if (result == WSA_WAIT_EVENT_0) break;
            if (result != WSA_WAIT_EVENT_0 + 1) break;

            WSANETWORKEVENTS networkEvents{};
            if (WSAEnumNetworkEvents(client, clientEvent, &networkEvents) == SOCKET_ERROR) break;

            if (networkEvents.lNetworkEvents & FD_READ) {
                while (!m_stopRequested.load()) {
                    const int received = recv(client, reinterpret_cast<char*>(input.data()),
                        static_cast<int>(input.size()), 0);
                    if (received > 0) {
                        std::vector<bridge_protocol::Frame> frames;
                        std::string error;
                        if (!parser.Feed(input.data(), static_cast<size_t>(received), frames, &error)) {
                            scs_log(1, "[LinuxBridgeSource] Rejected malformed stream: %s", error.c_str());
                            keepRunning = false;
                            break;
                        }
                        // IContentSource exposes only the latest frame, so skip
                        // superseded frames that arrived in the same TCP read.
                        if (!frames.empty()) Publish(std::move(frames.back()));
                        continue;
                    }
                    if (received == 0) {
                        keepRunning = false;
                        break;
                    }

                    const int error = WSAGetLastError();
                    if (error != WSAEWOULDBLOCK)
                        keepRunning = false;
                    break;
                }
            }

            if (networkEvents.lNetworkEvents & FD_CLOSE)
                keepRunning = false;
        }

        WSAEventSelect(client, nullptr, 0);
        WSACloseEvent(clientEvent);
        return !m_stopRequested.load();
    }

    void Run()
    {
        WSAEVENT events[] = { m_stopEvent, m_listenEvent };

        while (!m_stopRequested.load()) {
            const DWORD result = WSAWaitForMultipleEvents(2, events, FALSE, WSA_INFINITE, FALSE);
            if (result == WSA_WAIT_EVENT_0) break;
            if (result != WSA_WAIT_EVENT_0 + 1) break;

            WSANETWORKEVENTS networkEvents{};
            if (WSAEnumNetworkEvents(m_listener, m_listenEvent, &networkEvents) == SOCKET_ERROR) break;
            if (!(networkEvents.lNetworkEvents & FD_ACCEPT)) continue;
            if (networkEvents.iErrorCode[FD_ACCEPT_BIT] != 0) continue;

            sockaddr_in peer{};
            int peerLength = sizeof(peer);
            SOCKET client = accept(m_listener, reinterpret_cast<sockaddr*>(&peer), &peerLength);
            if (client == INVALID_SOCKET) continue;

            // Bound the TCP window so complete but obsolete frames cannot
            // accumulate ahead of the incremental protocol parser.
            const int receiveBufferSize = 512 * 1024;
            setsockopt(client, SOL_SOCKET, SO_RCVBUF,
                reinterpret_cast<const char*>(&receiveBufferSize), sizeof(receiveBufferSize));

            // Send the command before WSAEventSelect makes the accepted socket
            // non-blocking. Manual senders safely ignore this reverse channel.
            u_long blocking = 0;
            ioctlsocket(client, FIONBIO, &blocking);
            if (!SendStartCapture(client)) {
                closesocket(client);
                continue;
            }

            // Keep UI-triggered control sends non-blocking even in the short
            // interval before ReceiveClient installs its WSA event selection.
            u_long nonblocking = 1;
            if (ioctlsocket(client, FIONBIO, &nonblocking) == SOCKET_ERROR) {
                closesocket(client);
                continue;
            }

            {
                std::lock_guard<std::mutex> lock(m_clientMutex);
                m_client = client;
                m_clientConnected = true;
            }

            scs_log(0, "[LinuxBridgeSource] Sender connected");
            ReceiveClient(client);
            {
                std::lock_guard<std::mutex> lock(m_clientMutex);
                if (m_client == client) m_client = INVALID_SOCKET;
                m_clientConnected = false;
            }
            shutdown(client, SD_BOTH);
            closesocket(client);
            MarkDisconnected();
            if (!m_stopRequested.load())
                scs_log(0, "[LinuxBridgeSource] Sender disconnected; waiting for reconnection");
        }
    }

    const uint16_t m_port;
    bool m_winsockStarted{};
    SOCKET m_listener{ INVALID_SOCKET };
    WSAEVENT m_stopEvent{ WSA_INVALID_EVENT };
    WSAEVENT m_listenEvent{ WSA_INVALID_EVENT };
    std::thread m_thread;
    std::atomic<bool> m_stopRequested{};

    std::mutex m_clientMutex;
    SOCKET m_client{ INVALID_SOCKET };
    std::atomic<bool> m_clientConnected{};

    std::mutex m_bufferMutex;
    std::vector<uint8_t> m_frameBuffer;
    uint32_t m_frameWidth{};
    uint32_t m_frameHeight{};
    bool m_haveFrame{};
    std::atomic<uint32_t> m_copiedWidth{};
    std::atomic<uint32_t> m_copiedHeight{};
};

} // namespace

std::unique_ptr<IContentSource> CreateLinuxBridgeSource(uint16_t port)
{
    auto source = std::make_unique<LinuxBridgeSource>(port);
    if (!source->Start()) return nullptr;
    return source;
}

} // namespace sources
