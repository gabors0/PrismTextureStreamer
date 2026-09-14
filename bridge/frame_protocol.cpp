#include "frame_protocol.h"

#include <algorithm>
#include <cstring>

namespace bridge_protocol {
namespace {

constexpr uint8_t kMagic[4] = { 'P', 'T', 'S', 'B' };

void SetError(std::string* error, const char* message)
{
    if (error) *error = message;
}

void WriteU16(uint8_t* dst, uint16_t value)
{
    dst[0] = static_cast<uint8_t>(value);
    dst[1] = static_cast<uint8_t>(value >> 8);
}

void WriteU32(uint8_t* dst, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i)
        dst[i] = static_cast<uint8_t>(value >> (i * 8));
}

void WriteU64(uint8_t* dst, uint64_t value)
{
    for (unsigned i = 0; i < 8; ++i)
        dst[i] = static_cast<uint8_t>(value >> (i * 8));
}

uint16_t ReadU16(const uint8_t* src)
{
    return static_cast<uint16_t>(src[0]) |
        (static_cast<uint16_t>(src[1]) << 8);
}

uint32_t ReadU32(const uint8_t* src)
{
    uint32_t value = 0;
    for (unsigned i = 0; i < 4; ++i)
        value |= static_cast<uint32_t>(src[i]) << (i * 8);
    return value;
}

uint64_t ReadU64(const uint8_t* src)
{
    uint64_t value = 0;
    for (unsigned i = 0; i < 8; ++i)
        value |= static_cast<uint64_t>(src[i]) << (i * 8);
    return value;
}

bool ValidateHeader(const Header& header, std::string* error)
{
    if (header.pixelFormat != kPixelFormatRgba8) {
        SetError(error, "unsupported pixel format");
        return false;
    }
    if (header.width == 0 || header.height == 0 ||
        header.width > kMaxWidth || header.height > kMaxHeight) {
        SetError(error, "frame dimensions are outside the supported range");
        return false;
    }

    const uint64_t expected = static_cast<uint64_t>(header.width) * header.height * kBytesPerPixel;
    if (expected > kMaxPayloadSize || header.payloadSize != expected) {
        SetError(error, "payload size does not match RGBA8 frame dimensions");
        return false;
    }
    return true;
}

} // namespace

bool EncodeHeader(const Header& header, std::array<uint8_t, kHeaderSize>& encoded, std::string* error)
{
    if (!ValidateHeader(header, error)) return false;

    std::copy(kMagic, kMagic + sizeof(kMagic), encoded.begin());
    WriteU16(encoded.data() + 4, kVersion);
    WriteU16(encoded.data() + 6, static_cast<uint16_t>(kHeaderSize));
    WriteU32(encoded.data() + 8, header.width);
    WriteU32(encoded.data() + 12, header.height);
    WriteU32(encoded.data() + 16, header.pixelFormat);
    WriteU32(encoded.data() + 20, header.payloadSize);
    WriteU64(encoded.data() + 24, header.sequence);
    return true;
}

bool DecodeHeader(const uint8_t* encoded, size_t size, Header& header, std::string* error)
{
    if (!encoded || size < kHeaderSize) {
        SetError(error, "incomplete frame header");
        return false;
    }
    if (std::memcmp(encoded, kMagic, sizeof(kMagic)) != 0) {
        SetError(error, "invalid frame magic");
        return false;
    }
    if (ReadU16(encoded + 4) != kVersion) {
        SetError(error, "unsupported protocol version");
        return false;
    }
    if (ReadU16(encoded + 6) != kHeaderSize) {
        SetError(error, "invalid frame header size");
        return false;
    }

    Header decoded;
    decoded.width = ReadU32(encoded + 8);
    decoded.height = ReadU32(encoded + 12);
    decoded.pixelFormat = ReadU32(encoded + 16);
    decoded.payloadSize = ReadU32(encoded + 20);
    decoded.sequence = ReadU64(encoded + 24);
    if (!ValidateHeader(decoded, error)) return false;

    header = decoded;
    return true;
}

bool FrameStreamParser::Feed(const uint8_t* data, size_t size, std::vector<Frame>& completedFrames, std::string* error)
{
    if (m_failed) {
        SetError(error, "parser must be reset after a protocol error");
        return false;
    }
    if (!data && size != 0) {
        m_failed = true;
        SetError(error, "null input buffer");
        return false;
    }

    while (size != 0) {
        const size_t wanted = m_readingPayload ? m_header.payloadSize : kHeaderSize;
        const size_t amount = std::min(size, wanted - m_buffer.size());
        m_buffer.insert(m_buffer.end(), data, data + amount);
        data += amount;
        size -= amount;

        if (m_buffer.size() != wanted) continue;

        if (!m_readingPayload) {
            if (!DecodeHeader(m_buffer.data(), m_buffer.size(), m_header, error)) {
                m_failed = true;
                m_buffer.clear();
                return false;
            }
            m_buffer.clear();
            m_buffer.reserve(m_header.payloadSize);
            m_readingPayload = true;
            continue;
        }

        Frame frame;
        frame.header = m_header;
        frame.pixels = std::move(m_buffer);
        completedFrames.push_back(std::move(frame));

        m_buffer.clear();
        m_buffer.reserve(kHeaderSize);
        m_header = {};
        m_readingPayload = false;
    }

    return true;
}

void FrameStreamParser::Reset()
{
    m_failed = false;
    m_readingPayload = false;
    m_header = {};
    m_buffer.clear();
    m_buffer.reserve(kHeaderSize);
}

} // namespace bridge_protocol
