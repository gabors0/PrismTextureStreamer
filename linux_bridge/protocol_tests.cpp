#include "../bridge/frame_protocol.h"

#include <array>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace {

int g_failures{};

#define CHECK(condition) do { if (!(condition)) { \
    std::cerr << __FILE__ << ':' << __LINE__ << ": check failed: " #condition "\n"; \
    ++g_failures; } } while (false)

std::vector<uint8_t> MakeFrame(uint32_t width, uint32_t height, uint64_t sequence, uint8_t fill)
{
    bridge_protocol::Header header;
    header.width = width;
    header.height = height;
    header.payloadSize = width * height * bridge_protocol::kBytesPerPixel;
    header.sequence = sequence;

    std::array<uint8_t, bridge_protocol::kHeaderSize> encoded{};
    CHECK(bridge_protocol::EncodeHeader(header, encoded));

    std::vector<uint8_t> bytes(encoded.begin(), encoded.end());
    bytes.insert(bytes.end(), header.payloadSize, fill);
    return bytes;
}

void TestHeaderRoundTrip()
{
    bridge_protocol::Header input;
    input.width = 640;
    input.height = 360;
    input.payloadSize = 640 * 360 * 4;
    input.sequence = 0x0102030405060708ull;

    std::array<uint8_t, bridge_protocol::kHeaderSize> bytes{};
    CHECK(bridge_protocol::EncodeHeader(input, bytes));
    CHECK(bytes[0] == 'P' && bytes[1] == 'T' && bytes[2] == 'S' && bytes[3] == 'B');
    CHECK(bytes[4] == 1 && bytes[5] == 0);

    bridge_protocol::Header output;
    CHECK(bridge_protocol::DecodeHeader(bytes.data(), bytes.size(), output));
    CHECK(output.width == input.width);
    CHECK(output.height == input.height);
    CHECK(output.payloadSize == input.payloadSize);
    CHECK(output.sequence == input.sequence);
}

void TestByteByByteFragmentation()
{
    const auto bytes = MakeFrame(7, 3, 42, 0x5a);
    bridge_protocol::FrameStreamParser parser;
    std::vector<bridge_protocol::Frame> frames;

    for (uint8_t byte : bytes)
        CHECK(parser.Feed(&byte, 1, frames));

    CHECK(frames.size() == 1);
    CHECK(frames[0].header.width == 7);
    CHECK(frames[0].header.height == 3);
    CHECK(frames[0].header.sequence == 42);
    CHECK(frames[0].pixels.size() == 7 * 3 * 4);
    CHECK(frames[0].pixels.front() == 0x5a && frames[0].pixels.back() == 0x5a);
}

void TestMultipleFramesAndResolutionChange()
{
    auto bytes = MakeFrame(4, 2, 1, 0x11);
    const auto second = MakeFrame(3, 5, 2, 0x22);
    bytes.insert(bytes.end(), second.begin(), second.end());

    bridge_protocol::FrameStreamParser parser;
    std::vector<bridge_protocol::Frame> frames;
    CHECK(parser.Feed(bytes.data(), 17, frames));
    CHECK(frames.empty());
    CHECK(parser.Feed(bytes.data() + 17, bytes.size() - 17, frames));
    CHECK(frames.size() == 2);
    CHECK(frames[0].header.width == 4 && frames[0].header.height == 2);
    CHECK(frames[1].header.width == 3 && frames[1].header.height == 5);
    CHECK(frames[1].pixels.back() == 0x22);
}

void TestMalformedHeaders()
{
    auto valid = MakeFrame(4, 4, 1, 0);
    const std::array<size_t, 6> offsets = { 0, 4, 6, 16, 20, 12 };

    for (size_t offset : offsets) {
        auto malformed = valid;
        malformed[offset] ^= 0xff;
        bridge_protocol::FrameStreamParser parser;
        std::vector<bridge_protocol::Frame> frames;
        std::string error;
        CHECK(!parser.Feed(malformed.data(), malformed.size(), frames, &error));
        CHECK(parser.Failed());
        CHECK(!error.empty());
        parser.Reset();
        frames.clear();
        CHECK(parser.Feed(valid.data(), valid.size(), frames));
        CHECK(frames.size() == 1);
    }

    bridge_protocol::Header tooLarge;
    tooLarge.width = bridge_protocol::kMaxWidth + 1;
    tooLarge.height = 1;
    tooLarge.payloadSize = tooLarge.width * 4;
    std::array<uint8_t, bridge_protocol::kHeaderSize> bytes{};
    CHECK(!bridge_protocol::EncodeHeader(tooLarge, bytes));
}

} // namespace

int main()
{
    TestHeaderRoundTrip();
    TestByteByByteFragmentation();
    TestMultipleFramesAndResolutionChange();
    TestMalformedHeaders();

    if (g_failures != 0) {
        std::cerr << g_failures << " protocol test(s) failed\n";
        return 1;
    }
    std::cout << "All protocol tests passed\n";
    return 0;
}
