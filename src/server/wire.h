#ifndef AUGUSTA_SERVER_WIRE_H_
#define AUGUSTA_SERVER_WIRE_H_

#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <vector>

#include "augusta/assets.h"
#include "augusta/command.h"
#include "augusta/failure.h"
#include "augusta/math.h"
#include "augusta/parameters.h"
#include "augusta/physics.h"
#include "augusta/protocol.h"
#include "augusta/replication.h"
#include "augusta/tick.h"
#include "capture.h"
#include "command_queue.h"
#include "host_metrics.h"
#include "match.h"
#include "replay.h"
#include "replay_catalog.h"

/// \file
/// The server's edge with the Networking Protocol (ADR-0038): what server::Host
/// sends, turned from the engine's types into the protocol's plain ones right
/// before Encode, and what it receives, turned back right after Decode; and a
/// Match capture's records (capture.h), the same way. The only
/// place on the server where a protocol::*Wire type meets an engine type: Match,
/// CommandQueue and replication never see one. Pure field-by-field copies;
/// whether a value is one the server accepts is decided after, by whoever takes
/// it in. What the server sends or captures is encoded here too, so a payload
/// the protocol cannot carry becomes the broken invariant that stops the
/// runtime (ADR-0033) before anything of it leaves.
namespace augusta::server {

/// message encoded, or, if a field of it is beyond what the protocol carries,
/// no payload but a failure::Code::kInvariantViolated naming its type
/// (`message_type=`): what Host never sends.
[[nodiscard]] std::expected<protocol::BytesWire, failure::Failure> EncodeToSend(const protocol::MessageWire& message);

/// record encoded, or no payload but the broken invariant naming its type
/// (`capture_record_type=`), as EncodeToSend: what a capture never holds.
[[nodiscard]] std::expected<protocol::BytesWire, failure::Failure> EncodeToCapture(
    const protocol::CaptureRecordWire& record);

/// session as the protocol carries it.
[[nodiscard]] protocol::SessionIdWire ToWire(SessionId session);

/// The type of payload, an encoded message: what its first byte says. payload
/// must be one Encode made, or Decode took.
[[nodiscard]] MessageType TypeOf(std::span<const std::byte> payload);

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
/// same index; first_tick is the Match's first tick.
[[nodiscard]] protocol::MatchStartWire ToWire(const MatchStart& start, std::span<const math::Vec3> spawns,
                                              tick::Tick first_tick);

/// A match's end as the protocol carries it: a draw names protocol::kDraw.
[[nodiscard]] protocol::MatchEndWire ToWire(const MatchEnd& end);

/// What replication planned for every recipient of a tick, as the message each
/// is sent before Address fills in its own fields: the tick and every body.
[[nodiscard]] protocol::AuthoritativeStateWire ToWire(const replication::Updates& updates);

/// Fills in state, the message ToWire(Updates) made, with what replication
/// planned for recipient alone, replacing any other recipient's: so one message
/// is addressed to each recipient in turn, its bodies converted once.
void Address(protocol::AuthoritativeStateWire& state, const replication::RecipientUpdate& recipient);

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

/// A capture a replay server replays, as its Replay list names it (ADR-0051).
[[nodiscard]] protocol::ReplayListingWire ToWire(const ReplayListing& listing);

/// The captures a replay server's Replay list names (ListedOf), as the list.
[[nodiscard]] protocol::ReplayListWire ToWire(const std::vector<ReplayListing>& listings);

/// What each player of a Replay looked like on tick, as its viewer's Replay view.
[[nodiscard]] protocol::ReplayViewWire ToWire(const std::vector<PlayerView>& views, tick::Tick tick);

/// A Replay a viewer asked for, in the engine's terms.
[[nodiscard]] ReplayRequest FromWire(const protocol::ReplayRequestWire& request);

/// A Match capture's header as its record (ADR-0050), in this engine's format version.
[[nodiscard]] protocol::CaptureHeaderWire ToWire(const CaptureHeader& header);

/// A capture's header record in the engine's terms; its format version is the reader's to check.
[[nodiscard]] CaptureHeader FromWire(const protocol::CaptureHeaderWire& header);

/// One event of a capture as its record.
[[nodiscard]] protocol::CaptureRecordWire ToWire(const CaptureRecord& record);

/// A capture's record in the engine's terms; nullopt for its header, which is no event.
[[nodiscard]] std::optional<CaptureRecord> FromWire(const protocol::CaptureRecordWire& record);

}  // namespace augusta::server

#endif  // AUGUSTA_SERVER_WIRE_H_
