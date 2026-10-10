#ifndef AUGUSTA_HARNESS_WIRE_H_
#define AUGUSTA_HARNESS_WIRE_H_

#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>

#include "augusta/assets.h"
#include "augusta/command.h"
#include "augusta/failure.h"
#include "augusta/harness.h"
#include "augusta/math.h"
#include "augusta/parameters.h"
#include "augusta/physics.h"
#include "augusta/protocol.h"
#include "augusta/reenactment.h"
#include "augusta/tick.h"

/// \file
/// The client's edge with the Networking Protocol (ADR-0038): what
/// harness::Session receives, turned from the protocol's plain types into the
/// engine's right after Decode, and what it sends, turned back right before
/// Encode. The only place on the client where a protocol::*Wire type meets an
/// engine type, but for the shared core's own, which convert in
/// augusta/shared_wire.h as on the server. Pure field-by-field copies; whether a value is one the client
/// accepts is decided after, by whoever takes it in. What it sends is encoded
/// here too, so a payload the protocol cannot carry becomes the broken
/// invariant that stops the client's runtime (ADR-0033) before anything of it
/// leaves.
namespace augusta::harness {

/// message encoded, or, if a field of it is beyond what the protocol carries,
/// no payload but a failure::Code::kInvariantViolated naming its type
/// (`message_type=`): what the Session never sends.
[[nodiscard]] std::expected<protocol::BytesWire, failure::Failure> EncodeToSend(const protocol::MessageWire& message);

/// What this client asks when it joins, in the engine's terms.
struct JoinRequest {
  /// Its engine version (augusta::EngineVersion).
  std::string engine_version;
  /// The hash of the client pack it loaded.
  assets::PackHash client_pack{};
  /// The character it asks to play, by its name in the scenario's manifest.
  std::string character;
  /// Where it asks to spawn, as a Captured player (ADR-0050); nullopt for anyone else.
  std::optional<math::Vec3> spawn = std::nullopt;
};

/// What this client asks when it watches a Replay (ADR-0051), in the engine's terms.
struct ReplayRequest {
  /// Its engine version (augusta::EngineVersion).
  std::string engine_version;
  /// The hash of the client pack it loaded.
  assets::PackHash client_pack{};
  /// The capture to watch, by its name in the replay server's Replay list.
  std::string capture;
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

/// A Command a capture holds (ADR-0050), in the engine's terms.
[[nodiscard]] CapturedCommand FromWire(const protocol::CapturedCommandWire& command);

/// A Replay view the replay server sent, in the engine's terms.
[[nodiscard]] ReplayView FromWire(const protocol::ReplayViewWire& view);

/// A capture of a replay server's Replay list, in the engine's terms.
[[nodiscard]] ReplayListing FromWire(const protocol::ReplayListingWire& listing);

/// request as the protocol carries it.
[[nodiscard]] protocol::ReplayRequestWire ToWire(const ReplayRequest& request);

/// A Death a capture holds, in the engine's terms.
[[nodiscard]] CapturedDeath FromWire(const protocol::CapturedDeathWire& death);

/// A capture's Match end, in the engine's terms: a winner of 0 is a Draw.
[[nodiscard]] CapturedEnd FromWire(const protocol::CapturedMatchEndWire& end);

/// request as the protocol carries it: a Reenact request when it names a
/// spawn, a Join request otherwise.
[[nodiscard]] protocol::MessageWire ToWire(const JoinRequest& request);

/// commands, oldest first, as the one message that carries them; its Seen tick
/// is the newest of theirs.
[[nodiscard]] protocol::CommandsWire ToWire(std::span<const SequencedCommand> commands);

}  // namespace augusta::harness

#endif  // AUGUSTA_HARNESS_WIRE_H_
