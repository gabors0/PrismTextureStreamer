#include "../bridge/control_protocol.h"

#include <array>
#include <cassert>
#include <iostream>
#include <string>
#include <vector>

int main()
{
    bridge_protocol::ControlMessage start{ bridge_protocol::ControlCommand::StartCapture };
    std::array<uint8_t, bridge_protocol::kControlMessageSize> encoded{};
    std::string error;
    assert(bridge_protocol::EncodeControlMessage(start, encoded, &error));

    bridge_protocol::ControlStreamParser parser;
    std::vector<bridge_protocol::ControlMessage> messages;
    for (uint8_t byte : encoded)
        assert(parser.Feed(&byte, 1, messages, &error));
    assert(messages.size() == 1);
    assert(messages[0].command == bridge_protocol::ControlCommand::StartCapture);

    auto malformed = encoded;
    malformed[0] = 'X';
    messages.clear();
    assert(!parser.Feed(malformed.data(), malformed.size(), messages, &error));
    assert(parser.Failed());

    parser.Reset();
    std::array<uint8_t, bridge_protocol::kControlMessageSize * 2> combined{};
    std::copy(encoded.begin(), encoded.end(), combined.begin());
    std::copy(encoded.begin(), encoded.end(), combined.begin() + encoded.size());
    assert(parser.Feed(combined.data(), combined.size(), messages, &error));
    assert(messages.size() == 2);

    std::cout << "All control protocol tests passed\n";
    return 0;
}
