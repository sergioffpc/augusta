#ifndef AUGUSTA_LINK_MESSAGES_H_
#define AUGUSTA_LINK_MESSAGES_H_

#include <cstddef>
#include <expected>
#include <optional>
#include <span>
#include <vector>

#include "augusta/capture_records.h"
#include "augusta/failure.h"
#include "augusta/link_types.h"
#include "augusta/parameters.h"
#include "augusta/physics.h"
#include "augusta/protocol.h"
#include "augusta/tick.h"

/// \file
/// Every conversion between the game's state and the Networking Protocol's
/// messages, both ways (ADR-0038): what a peer sends, turned from the engine's
/// types (augusta/link_types.h) into the protocol's plain ones right before
/// Encode, and what it receives, turned back right after Decode; and a Match
/// capture's records (augusta/capture_records.h, ADR-0050), which the capture
/// module reads and writes. The one place a protocol::*Wire type meets an
/// engine type, for the Link's client end (augusta/link.h), the server's Host
/// and capture alike.
/// Stateless functions with no transport beneath: pure field-by-field copies,
/// but for a tick's Commands, of which the tick's one message carries only the
/// newest within the protocol's bound. Whether a value is one a peer accepts is decided
/// after, by whoever takes it in. What a peer sends is encoded here too, so a
/// payload the protocol cannot carry becomes the broken invariant that stops
/// its runtime (ADR-0033) before anything of it leaves.
namespace augusta::link {

/// message encoded, or, if a field of it is beyond what the protocol carries,
/// no payload but a failure::Code::kInvariantViolated naming its type
/// (`message_type=`): what a peer never sends.
[[nodiscard]] std::expected<protocol::BytesWire, failure::Failure> EncodeToSend(const protocol::MessageWire& message);

/// The type of payload, an encoded message: what its first byte says. payload
/// must be one Encode made, or Decode took.
[[nodiscard]] MessageType TypeOf(std::span<const std::byte> payload);

[[nodiscard]] protocol::SessionIdWire ToWire(SessionId session);
[[nodiscard]] SessionId FromWire(protocol::SessionIdWire session);

[[nodiscard]] protocol::EntityIdWire ToWire(EntityId entity);
[[nodiscard]] EntityId FromWire(protocol::EntityIdWire entity);

/// A refusal no case names, a corrupted one, travels on as a value the
/// protocol lacks, which Encode refuses.
[[nodiscard]] protocol::JoinRefusalWire ToWire(JoinRefusal reason);
[[nodiscard]] JoinRefusal FromWire(protocol::JoinRefusalWire reason);

[[nodiscard]] protocol::BodyStateWire ToWire(const physics::BodyState& body);
[[nodiscard]] physics::BodyState FromWire(const protocol::BodyStateWire& body);

[[nodiscard]] protocol::ParametersWire ToWire(const parameters::Parameters& parameters);
[[nodiscard]] parameters::Parameters FromWire(const protocol::ParametersWire& parameters);

[[nodiscard]] protocol::JoinAcceptedWire ToWire(const Admission& admission);
[[nodiscard]] Admission FromWire(const protocol::JoinAcceptedWire& accepted);

[[nodiscard]] protocol::EntityStateWire ToWire(const EntityBody& body);
[[nodiscard]] EntityBody FromWire(const protocol::EntityStateWire& body);

[[nodiscard]] protocol::AuthoritativeStateWire ToWire(const AuthoritativeState& state);
/// Fills in state, a message ToWire made, with recipient's own fields alone
/// (its acknowledged sequence, rifle, health and queued commands), its tick and
/// bodies left as they were: so one message, its bodies converted once, is
/// addressed to each recipient in turn.
void Address(protocol::AuthoritativeStateWire& state, const AuthoritativeState& recipient);
[[nodiscard]] AuthoritativeState FromWire(const protocol::AuthoritativeStateWire& state);

[[nodiscard]] protocol::ShotWire ToWire(const Shot& shot);
[[nodiscard]] Shot FromWire(const protocol::ShotWire& shot);

[[nodiscard]] protocol::HitConfirmationWire ToWire(const HitConfirmation& hit);
[[nodiscard]] HitConfirmation FromWire(const protocol::HitConfirmationWire& hit);

[[nodiscard]] protocol::DeathWire ToWire(const Death& death);
[[nodiscard]] Death FromWire(const protocol::DeathWire& death);

[[nodiscard]] protocol::LobbyWire ToWire(const Lobby& lobby);
[[nodiscard]] Lobby FromWire(const protocol::LobbyWire& lobby);

[[nodiscard]] protocol::MatchStartWire ToWire(const MatchStart& start);
[[nodiscard]] MatchStart FromWire(const protocol::MatchStartWire& start);

/// A draw names protocol::kDraw.
[[nodiscard]] protocol::MatchEndWire ToWire(const MatchEnd& end);
[[nodiscard]] MatchEnd FromWire(const protocol::MatchEndWire& end);

[[nodiscard]] protocol::ReplayViewWire ToWire(const ReplayView& view);
[[nodiscard]] ReplayView FromWire(const protocol::ReplayViewWire& view);

[[nodiscard]] protocol::ReplayListingWire ToWire(const ReplayListing& listing);
[[nodiscard]] ReplayListing FromWire(const protocol::ReplayListingWire& listing);

/// The captures a replay server's Replay list names, as the list.
[[nodiscard]] protocol::ReplayListWire ToWire(const std::vector<ReplayListing>& listings);
[[nodiscard]] std::vector<ReplayListing> FromWire(const protocol::ReplayListWire& list);

[[nodiscard]] protocol::ReplayRequestWire ToWire(const ReplayRequest& request);
[[nodiscard]] ReplayRequest FromWire(const protocol::ReplayRequestWire& request);

/// request as the protocol carries it: a Reenact request when it names a
/// spawn, a Join request otherwise.
[[nodiscard]] protocol::MessageWire ToWire(const JoinRequest& request);
[[nodiscard]] JoinRequest FromWire(const protocol::JoinRequestWire& request);
/// A Captured player's Reenact request: a join that names its spawn (ADR-0050).
[[nodiscard]] JoinRequest FromWire(const protocol::ReenactRequestWire& request);

/// A tick's commands, oldest first, as the one message that carries them: the
/// Carried ones (augusta/link_types.h) alone. Its Seen tick is the newest of theirs.
[[nodiscard]] protocol::CommandsWire ToWire(std::span<const SequencedCommand> commands);

/// The commands of one message, oldest first, each with its own Seen time's
/// tick, never before tick 0.
[[nodiscard]] std::vector<SequencedCommand> FromWire(const protocol::CommandsWire& message);

[[nodiscard]] protocol::ReadyWire ToWire(const Ready& ready);
[[nodiscard]] protocol::ReplayListRequestWire ToWire(ReplayListRequest request);

/// message as what a server says to a client, or nullopt if it is one only a
/// client sends.
[[nodiscard]] std::optional<ServerMessage> ServerMessageFromWire(const protocol::MessageWire& message);

/// A capture's header, in this engine's format version.
[[nodiscard]] protocol::CaptureHeaderWire ToWire(const capture::Header& header);
[[nodiscard]] capture::Header FromWire(const protocol::CaptureHeaderWire& header);

/// The Join of player, at offset 0.
[[nodiscard]] protocol::CaptureRecordWire ToWire(capture::Player player, const capture::Join& join);
[[nodiscard]] capture::Join FromWire(const protocol::CapturedJoinWire& join);

/// One event of a capture at its offset. A Command's Seen time travels as its
/// seen_offset alone.
[[nodiscard]] protocol::CaptureRecordWire ToWire(const capture::AtOffset<capture::Event>& event);
/// The event of record at its offset, or nothing for a header or a Join, which are not events.
[[nodiscard]] std::optional<capture::AtOffset<capture::Event>> EventFromWire(const protocol::CaptureRecordWire& record);

}  // namespace augusta::link

#endif  // AUGUSTA_LINK_MESSAGES_H_
