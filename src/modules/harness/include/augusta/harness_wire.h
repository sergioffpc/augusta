#ifndef AUGUSTA_HARNESS_WIRE_H_
#define AUGUSTA_HARNESS_WIRE_H_

#include <cstdint>
#include <span>
#include <string>

#include "augusta/assets.h"
#include "augusta/command.h"
#include "augusta/harness.h"
#include "augusta/parameters.h"
#include "augusta/physics.h"
#include "augusta/protocol.h"
#include "augusta/tick.h"

/// \file
/// The client's edge with the Networking Protocol (ADR-0038): what
/// harness::Session receives, turned from the protocol's plain types into the
/// engine's right after Decode, and what it sends, turned back right before
/// Encode. The only place on the client where a protocol::*Wire type meets an
/// engine type. Pure field-by-field copies; whether a value is one the client
/// accepts is decided after, by whoever takes it in.
namespace augusta::harness {

/// What this client asks when it joins, in the engine's terms.
struct JoinRequest {
  /// Its engine version (augusta::EngineVersion).
  std::string engine_version;
  /// The hash of the client pack it loaded.
  assets::PackHash client_pack{};
  /// The character it asks to play, by its path relative to `authoring/`.
  std::string character;
};

/// One tick's command under the sequence this client gave it.
struct SequencedCommand {
  command::Sequence sequence = 0;
  command::Command command{};
};

/// session in the engine's terms.
[[nodiscard]] SessionId FromWire(protocol::SessionIdWire session);

/// reason in the engine's terms.
[[nodiscard]] JoinRefusal FromWire(protocol::JoinRefusalWire reason);

/// body in the engine's terms.
[[nodiscard]] physics::BodyState FromWire(const protocol::BodyStateWire& body);

/// parameters in the engine's terms.
[[nodiscard]] parameters::Parameters FromWire(const protocol::ParametersWire& parameters);

/// The server's admission of this client, in the engine's terms.
[[nodiscard]] Admission FromWire(const protocol::JoinAcceptedWire& accepted);

/// entity in the engine's terms.
[[nodiscard]] EntityId FromWire(protocol::EntityIdWire entity);

/// A body the server named, in the engine's terms.
[[nodiscard]] EntityBody FromWire(const protocol::EntityStateWire& body);

/// An Authoritative State the server sent, in the engine's terms.
[[nodiscard]] AuthoritativeState FromWire(const protocol::AuthoritativeStateWire& state);

/// A Shot the server announced, in the engine's terms.
[[nodiscard]] Shot FromWire(const protocol::ShotWire& shot);

/// A Hit confirmation the server sent, in the engine's terms.
[[nodiscard]] HitConfirmation FromWire(const protocol::HitConfirmationWire& hit);

/// The Lobby's Roster the server sent, in the engine's terms.
[[nodiscard]] Lobby FromWire(const protocol::LobbyWire& lobby);

/// A match's start the server sent, in the engine's terms.
[[nodiscard]] MatchStart FromWire(const protocol::MatchStartWire& start);

/// A Death the server told, in the engine's terms.
[[nodiscard]] Death FromWire(const protocol::DeathWire& death);

/// A match's end the server sent, in the engine's terms: a winner of
/// protocol::kDraw is none.
[[nodiscard]] MatchEnd FromWire(const protocol::MatchEndWire& end);

/// hash as the protocol carries it.
[[nodiscard]] protocol::PackHashWire ToWire(const assets::PackHash& hash);

/// request as the protocol carries it.
[[nodiscard]] protocol::JoinRequestWire ToWire(const JoinRequest& request);

/// command as the protocol carries it in a message whose Seen tick is
/// seen_tick: its own Seen time's tick as how far before that it is, no further
/// than a byte tells.
[[nodiscard]] protocol::CommandWire ToWire(const command::Command& command, tick::Tick seen_tick);

/// commands, oldest first, as the one message that carries them; its Seen tick
/// is the newest of theirs.
[[nodiscard]] protocol::CommandsWire ToWire(std::span<const SequencedCommand> commands);

}  // namespace augusta::harness

#endif  // AUGUSTA_HARNESS_WIRE_H_
