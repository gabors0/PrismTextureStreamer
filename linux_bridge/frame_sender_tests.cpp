#include "frame_sender.h"

#include "../bridge/control_protocol.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cerrno>
#include <cstring>
#include <cstdint>
#include <iostream>
#include <poll.h>
#include <string>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

namespace {

bool SendFragmented(int socketFd,
                    const std::array<uint8_t, bridge_protocol::kControlMessageSize>& message)
{
    const ssize_t first = send(socketFd, message.data(), 3, MSG_NOSIGNAL);
    const ssize_t second = send(socketFd, message.data() + 3, message.size() - 3, MSG_NOSIGNAL);
    return first == 3 && second == static_cast<ssize_t>(message.size() - 3);
}

} // namespace

int main()
{
    const int listener = socket(AF_INET, SOCK_STREAM, 0);
    if (listener < 0) {
        std::cerr << "Could not create listener: " << std::strerror(errno) << '\n';
        return 1;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = 0;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(listener, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0 ||
        listen(listener, 1) != 0) {
        std::cerr << "Could not bind listener: " << std::strerror(errno) << '\n';
        close(listener);
        return 1;
    }

    socklen_t addressLength = sizeof(address);
    if (getsockname(listener, reinterpret_cast<sockaddr*>(&address), &addressLength) != 0) {
        std::cerr << "Could not read listener address: " << std::strerror(errno) << '\n';
        close(listener);
        return 1;
    }

    FrameSender sender(ntohs(address.sin_port));
    sender.Start();

    pollfd ready{ listener, POLLIN, 0 };
    if (poll(&ready, 1, 2000) <= 0) {
        sender.Stop();
        close(listener);
        std::cerr << "Sender did not connect\n";
        return 1;
    }
    const int client = accept(listener, nullptr, nullptr);
    close(listener);
    if (client < 0) {
        std::cerr << "Could not accept sender: " << std::strerror(errno) << '\n';
        sender.Stop();
        return 1;
    }

    bridge_protocol::ControlMessage command{ bridge_protocol::ControlCommand::StartCapture };
    std::array<uint8_t, bridge_protocol::kControlMessageSize> encoded{};
    std::string error;
    if (!bridge_protocol::EncodeControlMessage(command, encoded, &error) ||
        !SendFragmented(client, encoded) || !sender.WaitForStartCapture()) {
        close(client);
        sender.Stop();
        std::cerr << "Fragmented initial request failed\n";
        return 1;
    }

    std::atomic<bool> restarted{};
    sender.SetRestartCallback([&restarted] { restarted = true; });
    if (!SendFragmented(client, encoded)) {
        close(client);
        sender.Stop();
        return 1;
    }

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!restarted && std::chrono::steady_clock::now() < deadline)
        usleep(1000);

    sender.SetRestartCallback({});
    close(client);
    sender.Stop();
    if (!restarted) {
        std::cerr << "Repeated request did not invoke restart callback\n";
        return 1;
    }

    std::cout << "All frame sender tests passed\n";
    return 0;
}
