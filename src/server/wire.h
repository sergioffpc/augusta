#ifndef AUGUSTA_SERVER_WIRE_H_
#define AUGUSTA_SERVER_WIRE_H_

#include <cstdint>
#include <span>
#include <vector>

#include "augusta/assets.h"
#include "augusta/command.h"
#include "augusta/math.h"
#include "augusta/parameters.h"
#include "augusta/physics.h"
#include "augusta/protocol.h"
#include "augusta/replication.h"
#include "augusta/tick.h"
#include "command_queue.h"
#include "match.h"

/// \file
/// The server's edge with the Networking Protocol (ADR-0038): what server::Host
/// sends, turned from the engine's types into the protocol's plain ones right
/// before Encode, and what it receives, turned back right after Decode. The only
/// place on the server where a protocol::*Wire type meets an engine type: Match,
/// CommandQueue and replication never see one. Pure field-by-field copies;
/// whether a value is one the server accepts is decided after, by whoever takes
/// it in.
namespace augusta::server {

/// session as the protocol carries it.
[[nodiscard]] protocol::SessionIdWire ToWire(SessionId session);

/// entity as the protocol carries it.
[[nodiscard]] protocol::EntityIdWire ToWire(EntityId entity);

/// reason as the protocol carries it.
[[nodiscard]] protocol::JoinRefusalWire ToWire(JoinRefusal reason);

/// admission as the message that tells the peer, with the tick rate and the
/// parameters every client is told when it joins.
[[nodiscard]] protocol::JoinAcceptedWire ToWire(const Admission& admission, std::uint8_t tick_rate_hz,
                                                const parameters::Parameters& parameters);

/// body as the protocol carries it.
[[nodiscard]] protocol::BodyStateWire ToWire(const physics::BodyState& body);

/// parameters as the protocol carries them.
[[nodiscard]] protocol::ParametersWire ToWire(const parameters::Parameters& parameters);

/// The Lobby's Roster as the protocol carries it.
[[nodiscard]] protocol::LobbyWire ToWire(const Roster& roster);

/// A match's start as the protocol carries it, its players spawned where
/// SimulationWorld put them: each of start.players at the spawns entry of the
/// same index.
[[nodiscard]] protocol::MatchStartWire ToWire(const MatchStart& start, std::span<const math::Vec3> spawns);

/// A match's end as the protocol carries it: a draw names protocol::kDraw.
[[nodiscard]] protocol::MatchEndWire ToWire(const MatchEnd& end);

/// What replication planned for one recipient, as the message it is sent.
[[nodiscard]] protocol::AuthoritativeStateWire ToWire(const replication::Update& update);

/// A Shot replication planned, as the message every client in the match is sent.
[[nodiscard]] protocol::ShotWire ToWire(const replication::Shot& shot);

/// A Hit confirmation replication planned, as the message its shooter is sent.
[[nodiscard]] protocol::HitConfirmationWire ToWire(const replication::HitConfirmation& hit);

/// A Death replication planned, as the message every client in the match is sent.
[[nodiscard]] protocol::DeathWire ToWire(const replication::Death& death);

/// hash in the engine's terms.
[[nodiscard]] assets::PackHash FromWire(const protocol::PackHashWire& hash);

/// A join a client asked for, in the engine's terms.
[[nodiscard]] JoinRequest FromWire(const protocol::JoinRequestWire& request);

/// A command a client sent in a message whose Seen tick is seen_tick, in the
/// engine's terms: its own Seen time's tick is that many ticks before it, or 0.
[[nodiscard]] command::Command FromWire(const protocol::CommandWire& command, tick::Tick seen_tick);

/// A sequenced command a client sent in a message whose Seen tick is seen_tick,
/// in the engine's terms.
[[nodiscard]] SequencedCommand FromWire(const protocol::SequencedCommandWire& command, tick::Tick seen_tick);

/// The commands a client sent in one message, oldest first, in the engine's terms.
[[nodiscard]] std::vector<SequencedCommand> FromWire(const protocol::CommandsWire& message);

}  // namespace augusta::server

#endif  // AUGUSTA_SERVER_WIRE_H_
