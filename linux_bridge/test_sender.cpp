#include "../bridge/frame_protocol.h"

#include <algorithm>
#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {

volatile std::sig_atomic_t g_stop{};

void HandleSignal(int) { g_stop = 1; }

bool SendAll(int socket, const uint8_t* data, size_t size)
{
    while (size != 0) {
        const ssize_t sent = send(socket, data, size, MSG_NOSIGNAL);
        if (sent > 0) {
            data += sent;
            size -= static_cast<size_t>(sent);
            continue;
        }
        if (sent < 0 && errno == EINTR) continue;
        return false;
    }
    return true;
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

void FillPattern(std::vector<uint8_t>& pixels, uint32_t width, uint32_t height, uint64_t sequence)
{
    const uint32_t boxSize = std::max(8u, std::min(width, height) / 6);
    const uint32_t boxX = width > boxSize ? static_cast<uint32_t>((sequence * 5) % (width - boxSize)) : 0;
    const uint32_t boxY = height > boxSize ? static_cast<uint32_t>((sequence * 3) % (height - boxSize)) : 0;

    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            const size_t offset = (static_cast<size_t>(y) * width + x) * 4;
            const bool inBox = x >= boxX && x < boxX + boxSize && y >= boxY && y < boxY + boxSize;
            pixels[offset + 0] = inBox ? 255 : static_cast<uint8_t>(255ull * x / width);
            pixels[offset + 1] = inBox ? 255 : static_cast<uint8_t>(255ull * y / height);
            pixels[offset + 2] = inBox ? 255 : static_cast<uint8_t>((sequence * 4) & 0xff);
            pixels[offset + 3] = 255;
        }
    }
}

} // namespace

int main(int argc, char** argv)
{
    uint32_t width = 640;
    uint32_t height = 360;
    uint32_t fps = 15;
    uint32_t port = bridge_protocol::kDefaultPort;

    if (argc > 1) width = ParseNumber(argv[1], "width");
    if (argc > 2) height = ParseNumber(argv[2], "height");
    if (argc > 3) fps = ParseNumber(argv[3], "fps");
    if (argc > 4) port = ParseNumber(argv[4], "port");
    if (argc > 5) {
        std::cerr << "Usage: " << argv[0] << " [width [height [fps [port]]]]\n";
        return 2;
    }
    if (width == 0 || height == 0 || width > bridge_protocol::kMaxWidth ||
        height > bridge_protocol::kMaxHeight || fps == 0 || fps > 30 || port == 0 || port > 65535) {
        std::cerr << "Limits: width 1-" << bridge_protocol::kMaxWidth
                  << ", height 1-" << bridge_protocol::kMaxHeight
                  << ", fps 1-30, port 1-65535\n";
        return 2;
    }

    std::signal(SIGINT, HandleSignal);
    std::signal(SIGTERM, HandleSignal);

    const size_t payloadSize = static_cast<size_t>(width) * height * bridge_protocol::kBytesPerPixel;
    std::vector<uint8_t> pixels(payloadSize);
    uint64_t sequence = 0;
    const auto frameInterval = std::chrono::microseconds(1000000 / fps);

    while (!g_stop) {
        const int socketFd = socket(AF_INET, SOCK_STREAM, 0);
        if (socketFd < 0) {
            std::cerr << "socket failed: " << std::strerror(errno) << '\n';
            return 1;
        }

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(static_cast<uint16_t>(port));
        inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);

        if (connect(socketFd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
            close(socketFd);
            std::cerr << "Waiting for 127.0.0.1:" << port << "...\r" << std::flush;
            std::this_thread::sleep_for(std::chrono::seconds(1));
            continue;
        }

        std::cout << "Connected to 127.0.0.1:" << port << " at "
                  << width << 'x' << height << " RGBA8, " << fps << " FPS\n";

        while (!g_stop) {
            const auto frameStart = std::chrono::steady_clock::now();
            FillPattern(pixels, width, height, sequence);

            bridge_protocol::Header header;
            header.width = width;
            header.height = height;
            header.payloadSize = static_cast<uint32_t>(payloadSize);
            header.sequence = sequence++;

            std::array<uint8_t, bridge_protocol::kHeaderSize> encoded{};
            std::string error;
            if (!bridge_protocol::EncodeHeader(header, encoded, &error)) {
                std::cerr << "Header error: " << error << '\n';
                close(socketFd);
                return 1;
            }

            if (!SendAll(socketFd, encoded.data(), encoded.size()) ||
                !SendAll(socketFd, pixels.data(), pixels.size())) {
                std::cerr << "Sender disconnected; retrying...\n";
                break;
            }

            const auto elapsed = std::chrono::steady_clock::now() - frameStart;
            if (elapsed < frameInterval) std::this_thread::sleep_for(frameInterval - elapsed);
        }

        close(socketFd);
    }

    std::cout << "Stopped\n";
    return 0;
}
