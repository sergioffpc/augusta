#ifndef AUGUSTA_HARNESS_WIRE_H_
#define AUGUSTA_HARNESS_WIRE_H_

#include "augusta/harness.h"
#include "augusta/input.h"
#include "augusta/parameters.h"
#include "augusta/physics.h"
#include "augusta/protocol.h"

// The client's edge with the Networking Protocol (ADR-0038): what
// harness::Session receives, turned from the protocol's plain types into the
// engine's, and what it sends, turned back. Pure field-by-field copies; whether
// a value is one the client accepts is decided after, by whoever takes it in.
namespace augusta::harness {

/// body in the engine's terms.
[[nodiscard]] physics::BodyState FromWire(const protocol::BodyStateWire& body);

/// parameters in the engine's terms.
[[nodiscard]] parameters::Parameters FromWire(const protocol::ParametersWire& parameters);

/// A player the server named, in the engine's terms.
[[nodiscard]] PlayerBody FromWire(const protocol::PlayerStateWire& player);

/// An Authoritative State the server sent, in the engine's terms.
[[nodiscard]] AuthoritativeState FromWire(const protocol::AuthoritativeStateWire& state);

/// The Lobby's Roster the server sent, in the engine's terms.
[[nodiscard]] Lobby FromWire(const protocol::LobbyWire& lobby);

/// A match's start the server sent, in the engine's terms.
[[nodiscard]] MatchStart FromWire(const protocol::MatchStartWire& start);

/// command as the protocol carries it.
[[nodiscard]] protocol::CommandWire ToWire(const input::Command& command);

}  // namespace augusta::harness

#endif  // AUGUSTA_HARNESS_WIRE_H_
