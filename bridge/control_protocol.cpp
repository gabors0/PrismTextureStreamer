#include "control_protocol.h"

#include <algorithm>
#include <cstring>

namespace bridge_protocol {
namespace {

constexpr uint8_t kControlMagic[4] = { 'P', 'T', 'S', 'C' };

void SetError(std::string* error, const char* message)
{
    if (error) *error = message;
}

void WriteU16(uint8_t* destination, uint16_t value)
{
    destination[0] = static_cast<uint8_t>(value);
    destination[1] = static_cast<uint8_t>(value >> 8);
}

uint16_t ReadU16(const uint8_t* source)
{
    return static_cast<uint16_t>(source[0]) |
        (static_cast<uint16_t>(source[1]) << 8);
}

bool IsKnownCommand(ControlCommand command)
{
    return command == ControlCommand::StartCapture;
}

} // namespace

bool EncodeControlMessage(const ControlMessage& message,
                          std::array<uint8_t, kControlMessageSize>& encoded,
                          std::string* error)
{
    if (!IsKnownCommand(message.command)) {
        SetError(error, "unsupported control command");
        return false;
    }
    std::copy(kControlMagic, kControlMagic + sizeof(kControlMagic), encoded.begin());
    WriteU16(encoded.data() + 4, kControlVersion);
    WriteU16(encoded.data() + 6, static_cast<uint16_t>(message.command));
    return true;
}

bool DecodeControlMessage(const uint8_t* encoded, size_t size,
                          ControlMessage& message, std::string* error)
{
    if (!encoded || size < kControlMessageSize) {
        SetError(error, "incomplete control message");
        return false;
    }
    if (std::memcmp(encoded, kControlMagic, sizeof(kControlMagic)) != 0) {
        SetError(error, "invalid control magic");
        return false;
    }
    if (ReadU16(encoded + 4) != kControlVersion) {
        SetError(error, "unsupported control protocol version");
        return false;
    }

    ControlMessage decoded;
    decoded.command = static_cast<ControlCommand>(ReadU16(encoded + 6));
    if (!IsKnownCommand(decoded.command)) {
        SetError(error, "unsupported control command");
        return false;
    }
    message = decoded;
    return true;
}

bool ControlStreamParser::Feed(const uint8_t* data, size_t size,
                               std::vector<ControlMessage>& completed, std::string* error)
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
        const size_t amount = std::min(size, kControlMessageSize - m_buffer.size());
        m_buffer.insert(m_buffer.end(), data, data + amount);
        data += amount;
        size -= amount;
        if (m_buffer.size() != kControlMessageSize) continue;

        ControlMessage message;
        if (!DecodeControlMessage(m_buffer.data(), m_buffer.size(), message, error)) {
            m_failed = true;
            m_buffer.clear();
            return false;
        }
        completed.push_back(message);
        m_buffer.clear();
    }
    return true;
}

void ControlStreamParser::Reset()
{
    m_failed = false;
    m_buffer.clear();
}

} // namespace bridge_protocol
