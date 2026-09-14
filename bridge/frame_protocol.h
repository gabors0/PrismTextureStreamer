#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace bridge_protocol {

constexpr uint16_t kVersion = 1;
constexpr size_t kHeaderSize = 32;
constexpr uint32_t kPixelFormatRgba8 = 1;
constexpr uint32_t kMaxWidth = 1280;
constexpr uint32_t kMaxHeight = 720;
constexpr uint32_t kBytesPerPixel = 4;
constexpr uint32_t kMaxPayloadSize = kMaxWidth * kMaxHeight * kBytesPerPixel;
constexpr uint16_t kDefaultPort = 27861;

struct Header
{
    uint32_t width{};
    uint32_t height{};
    uint32_t pixelFormat{ kPixelFormatRgba8 };
    uint32_t payloadSize{};
    uint64_t sequence{};
};

struct Frame
{
    Header header;
    std::vector<uint8_t> pixels;
};

bool EncodeHeader(const Header& header, std::array<uint8_t, kHeaderSize>& encoded, std::string* error = nullptr);
bool DecodeHeader(const uint8_t* encoded, size_t size, Header& header, std::string* error = nullptr);

// Incremental parser for a TCP byte stream. A protocol error fails the parser
// until Reset() is called; callers should close that connection.
class FrameStreamParser
{
public:
    bool Feed(const uint8_t* data, size_t size, std::vector<Frame>& completedFrames, std::string* error = nullptr);
    void Reset();
    bool Failed() const { return m_failed; }

private:
    bool m_failed{};
    bool m_readingPayload{};
    Header m_header{};
    std::vector<uint8_t> m_buffer;
};

} // namespace bridge_protocol
