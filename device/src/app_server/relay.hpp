// Relay: receives events from brim/supervisor on the bus, validates them against the schema,
// stamps t_relay_mono_ms, writes history to SQLite, and hands them to the WebSocket broadcaster.
#pragma once

#include <atomic>
#include <functional>
#include <string>

#include "app_server/db.hpp"
#include "app_server/state.hpp"

namespace brim::app {

using Broadcast = std::function<void(std::string json)>;

// Validates + stamps one event. Returns the JSON to broadcast, or "" if invalid.
// Side effects on db/state. Exposed for tests.
std::string process_event(const std::string& payload, Db& db, ServerState& state);

// Blocking loop; returns when stop becomes true. Also runs the privacy purge every 60 s.
void run_relay(Db& db, ServerState& state, const Broadcast& broadcast, const std::atomic<bool>& stop);

}  // namespace brim::app
