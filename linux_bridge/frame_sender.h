#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

// Sends only the newest published frame from a worker thread. Capture callbacks
// never perform socket I/O and old frames are dropped when the receiver is slow.
class FrameSender
{
public:
    explicit FrameSender(uint16_t port);
    ~FrameSender();

    FrameSender(const FrameSender&) = delete;
    FrameSender& operator=(const FrameSender&) = delete;

    void Start();
    void Stop();
    bool WaitForStartCapture();
    bool Publish(uint32_t width, uint32_t height, std::vector<uint8_t> pixels);

private:
    struct PendingFrame
    {
        uint32_t width{};
        uint32_t height{};
        uint64_t generation{};
        std::vector<uint8_t> pixels;
    };

    void Run();

    uint16_t m_port;
    std::atomic<bool> m_stop{};
    std::atomic<int> m_socket{ -1 };
    std::mutex m_mutex;
    std::condition_variable m_changed;
    PendingFrame m_pending;
    std::mutex m_controlMutex;
    std::condition_variable m_controlChanged;
    bool m_startCaptureRequested{};
    std::thread m_thread;
};
