#ifndef AUGUSTA_SERVER_WIRE_H_
#define AUGUSTA_SERVER_WIRE_H_

#include "augusta/input.h"
#include "augusta/parameters.h"
#include "augusta/physics.h"
#include "augusta/protocol.h"
#include "augusta/replication.h"
#include "command_queue.h"
#include "match.h"

// The server's edge with the Networking Protocol (ADR-0038): what server::Host
// sends, turned from the engine's types into the protocol's plain ones, and
// what it receives, turned back. Pure field-by-field copies; whether a value is
// one the server accepts is decided after, by whoever takes it in.
namespace augusta::server {

/// body as the protocol carries it.
[[nodiscard]] protocol::BodyStateWire ToWire(const physics::BodyState& body);

/// parameters as the protocol carries them.
[[nodiscard]] protocol::ParametersWire ToWire(const parameters::Parameters& parameters);

/// A roster entry as the protocol carries it.
[[nodiscard]] protocol::PlayerStateWire ToWire(const RosterEntry& entry);

/// What replication planned for one recipient, as the message it is sent.
[[nodiscard]] protocol::AuthoritativeStateWire ToWire(const replication::Update& update);

/// A command a client sent, in the engine's terms.
[[nodiscard]] input::Command FromWire(const protocol::CommandWire& command);

/// A sequenced command a client sent, in the engine's terms.
[[nodiscard]] SequencedCommand FromWire(const protocol::SequencedCommandWire& command);

}  // namespace augusta::server

#endif  // AUGUSTA_SERVER_WIRE_H_
