#ifndef AUGUSTA_HARNESS_WIRE_H_
#define AUGUSTA_HARNESS_WIRE_H_

#include <cstdint>
#include <span>
#include <string>

#include "augusta/assets.h"
#include "augusta/harness.h"
#include "augusta/identity.h"
#include "augusta/input.h"
#include "augusta/parameters.h"
#include "augusta/physics.h"
#include "augusta/protocol.h"

// The client's edge with the Networking Protocol (ADR-0038): what
// harness::Session receives, turned from the protocol's plain types into the
// engine's right after Decode, and what it sends, turned back right before
// Encode. The only place on the client where a protocol::*Wire type meets an
// engine type. Pure field-by-field copies; whether a value is one the client
// accepts is decided after, by whoever takes it in.
namespace augusta::harness {

/// What the server said when it admitted this client, in the engine's terms.
struct Admission {
  /// The session the server assigned to this client.
  identity::SessionId session{};
  /// The rate, in Hz, at which the server ticks and this client must.
  std::uint8_t tick_rate_hz = 0;
  /// The parameters this client must predict with.
  parameters::Parameters parameters{};
  /// This client's own character index (see RosterEntry::character).
  std::uint8_t character = 1;
};

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
  std::uint32_t sequence = 0;
  input::Command command{};
};

/// session in the engine's terms.
[[nodiscard]] identity::SessionId FromWire(protocol::SessionIdWire session);

/// reason in the engine's terms.
[[nodiscard]] JoinRefusal FromWire(protocol::JoinRefusalWire reason);

/// body in the engine's terms.
[[nodiscard]] physics::BodyState FromWire(const protocol::BodyStateWire& body);

/// parameters in the engine's terms.
[[nodiscard]] parameters::Parameters FromWire(const protocol::ParametersWire& parameters);

/// The server's admission of this client, in the engine's terms.
[[nodiscard]] Admission FromWire(const protocol::JoinAcceptedWire& accepted);

/// A player the server named, in the engine's terms.
[[nodiscard]] PlayerBody FromWire(const protocol::PlayerStateWire& player);

/// An Authoritative State the server sent, in the engine's terms.
[[nodiscard]] AuthoritativeState FromWire(const protocol::AuthoritativeStateWire& state);

/// The Lobby's Roster the server sent, in the engine's terms.
[[nodiscard]] Lobby FromWire(const protocol::LobbyWire& lobby);

/// A match's start the server sent, in the engine's terms.
[[nodiscard]] MatchStart FromWire(const protocol::MatchStartWire& start);

/// hash as the protocol carries it.
[[nodiscard]] protocol::PackHashWire ToWire(const assets::PackHash& hash);

/// request as the protocol carries it.
[[nodiscard]] protocol::JoinRequestWire ToWire(const JoinRequest& request);

/// command as the protocol carries it.
[[nodiscard]] protocol::CommandWire ToWire(const input::Command& command);

/// commands, oldest first, as the one message that carries them.
[[nodiscard]] protocol::CommandsWire ToWire(std::span<const SequencedCommand> commands);

}  // namespace augusta::harness

#endif  // AUGUSTA_HARNESS_WIRE_H_
