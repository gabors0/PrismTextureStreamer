#include "frame_sender.h"

#include "../bridge/frame_protocol.h"

#include <array>
#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <iostream>
#include <netinet/tcp.h>
#include <string>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

namespace {

bool SendAll(int socketFd, const uint8_t* data, size_t size)
{
    while (size != 0) {
        const ssize_t sent = send(socketFd, data, size, MSG_NOSIGNAL);
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

} // namespace

FrameSender::FrameSender(uint16_t port) : m_port(port) {}

FrameSender::~FrameSender() { Stop(); }

void FrameSender::Start()
{
    if (!m_thread.joinable()) m_thread = std::thread(&FrameSender::Run, this);
}

void FrameSender::Stop()
{
    m_stop = true;
    m_changed.notify_all();
    const int socketFd = m_socket.load();
    if (socketFd >= 0) shutdown(socketFd, SHUT_RDWR);
    if (m_thread.joinable()) m_thread.join();
}

bool FrameSender::Publish(uint32_t width, uint32_t height, std::vector<uint8_t> pixels)
{
    const uint64_t expected = static_cast<uint64_t>(width) * height * bridge_protocol::kBytesPerPixel;
    if (width == 0 || height == 0 || width > bridge_protocol::kMaxWidth ||
        height > bridge_protocol::kMaxHeight || expected != pixels.size()) {
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_pending.width = width;
        m_pending.height = height;
        m_pending.pixels = std::move(pixels);
        ++m_pending.generation;
    }
    m_changed.notify_one();
    return true;
}

void FrameSender::Run()
{
    uint64_t sentGeneration = 0;
    uint64_t sequence = 0;

    while (!m_stop) {
        const int socketFd = socket(AF_INET, SOCK_STREAM, 0);
        if (socketFd < 0) {
            std::cerr << "socket failed: " << std::strerror(errno) << '\n';
            return;
        }
        m_socket = socketFd;

        timeval timeout{};
        timeout.tv_sec = 1;
        setsockopt(socketFd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

        // Keep the kernel from accepting several stale full-size frames when
        // the Proton receiver briefly falls behind. FrameSender will then
        // block only its worker and pick the newest published frame next.
        const int sendBufferSize = 256 * 1024;
        setsockopt(socketFd, SOL_SOCKET, SO_SNDBUF, &sendBufferSize, sizeof(sendBufferSize));
        const int noDelay = 1;
        setsockopt(socketFd, IPPROTO_TCP, TCP_NODELAY, &noDelay, sizeof(noDelay));

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(m_port);
        inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);

        if (connect(socketFd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
            close(socketFd);
            m_socket = -1;
            if (!m_stop) {
                std::cerr << "Waiting for 127.0.0.1:" << m_port << "...\r" << std::flush;
                std::unique_lock<std::mutex> lock(m_mutex);
                m_changed.wait_for(lock, std::chrono::seconds(1), [this] { return m_stop.load(); });
            }
            continue;
        }

        std::cout << "Connected to 127.0.0.1:" << m_port << '\n';
        bool connected = true;
        while (!m_stop && connected) {
            PendingFrame frame;
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                m_changed.wait(lock, [this, sentGeneration] {
                    return m_stop.load() || m_pending.generation != sentGeneration;
                });
                if (m_stop) break;
                frame = m_pending;
            }

            bridge_protocol::Header header;
            header.width = frame.width;
            header.height = frame.height;
            header.payloadSize = static_cast<uint32_t>(frame.pixels.size());
            header.sequence = sequence++;

            std::array<uint8_t, bridge_protocol::kHeaderSize> encoded{};
            std::string error;
            if (!bridge_protocol::EncodeHeader(header, encoded, &error)) {
                std::cerr << "Header error: " << error << '\n';
                connected = false;
            } else if (!SendAll(socketFd, encoded.data(), encoded.size()) ||
                       !SendAll(socketFd, frame.pixels.data(), frame.pixels.size())) {
                std::cerr << "Sender disconnected; retrying...\n";
                connected = false;
            } else {
                sentGeneration = frame.generation;
            }
        }

        close(socketFd);
        m_socket = -1;
    }
}
