#pragma once

#include "content_source.h"
#include "../../bridge/frame_protocol.h"

#include <cstdint>
#include <memory>

namespace sources {

// Starts a TCP listener on 127.0.0.1. The source remains valid while senders
// disconnect and reconnect; all network work happens on its worker thread.
std::unique_ptr<IContentSource> CreateLinuxBridgeSource(uint16_t port = bridge_protocol::kDefaultPort);

} // namespace sources
