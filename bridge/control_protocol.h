#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace bridge_protocol {

constexpr uint16_t kControlVersion = 1;
constexpr size_t kControlMessageSize = 8;

enum class ControlCommand : uint16_t
{
    StartCapture = 1,
};

struct ControlMessage
{
    ControlCommand command{};
};

bool EncodeControlMessage(const ControlMessage& message,
                          std::array<uint8_t, kControlMessageSize>& encoded,
                          std::string* error = nullptr);
bool DecodeControlMessage(const uint8_t* encoded, size_t size,
                          ControlMessage& message, std::string* error = nullptr);

class ControlStreamParser
{
public:
    bool Feed(const uint8_t* data, size_t size, std::vector<ControlMessage>& completed,
              std::string* error = nullptr);
    void Reset();
    bool Failed() const { return m_failed; }

private:
    bool m_failed{};
    std::vector<uint8_t> m_buffer;
};

} // namespace bridge_protocol
