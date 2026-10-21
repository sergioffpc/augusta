#ifndef AUGUSTA_LINK_MESSAGES_H_
#define AUGUSTA_LINK_MESSAGES_H_

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <variant>
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

/// The type of payload, a message Encode made, as protocol::TypeOf reads it
/// from its bytes. One Decode took may carry fields this build does not know
/// before its member, so its type is its decoded message's, the overload below.
[[nodiscard]] MessageType TypeOf(std::span<const std::byte> payload);
[[nodiscard]] MessageType TypeOf(const protocol::MessageWire& message);

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
void FillRecipientFields(protocol::AuthoritativeStateWire& state, const AuthoritativeState& recipient);
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

/// message as what a client asks a server, or nullopt if it is one only a
/// server sends.
[[nodiscard]] std::optional<ClientMessage> ClientMessageFromWire(const protocol::MessageWire& message);

// ---- A Match capture's records, as the bytes its file holds (ADR-0050) ----

/// What every capture's file starts with.
inline constexpr std::span<const std::byte, protocol::kCaptureMagic.size()> kCaptureMagic{protocol::kCaptureMagic};

/// The capture format this engine writes and reads: the one byte its file holds
/// after the magic, before any record.
inline constexpr std::uint8_t kCaptureFormatVersion = protocol::kCaptureFormatVersion;

/// A Join as its record says it: its player's number and its offset, which the
/// format holds to 1, 2, ... in order and to 0, with the Join itself.
struct CapturedJoin {
  std::uint32_t offset = 0;
  capture::Player player = 0;
  capture::Join join;

  bool operator==(const CapturedJoin&) const = default;
};

/// One record of a capture's file, in the engine's types: its header, a Join or
/// an event at its offset.
using CaptureRecord = std::variant<capture::Header, CapturedJoin, capture::AtOffset<capture::Event>>;

/// The payload of each record, as a capture's file holds it after its frame's
/// length: header, the Join of player at offset 0, or one event at its offset
/// (a Command's Seen time as its seen_offset alone). If a field is beyond what
/// the protocol carries, no payload but a failure::Code::kInvariantViolated
/// naming the record's type (`capture_record_type=`): the writer's broken
/// invariant.
[[nodiscard]] std::expected<std::vector<std::byte>, failure::Failure> EncodeCaptureHeader(
    const capture::Header& header);
[[nodiscard]] std::expected<std::vector<std::byte>, failure::Failure> EncodeCaptureJoin(capture::Player player,
                                                                                        const capture::Join& join);
[[nodiscard]] std::expected<std::vector<std::byte>, failure::Failure> EncodeCaptureEvent(
    const capture::AtOffset<capture::Event>& event);

/// The record payload holds, or nullopt if it does not decode.
[[nodiscard]] std::optional<CaptureRecord> DecodeCaptureRecord(std::span<const std::byte> payload);

}  // namespace augusta::link

#endif  // AUGUSTA_LINK_MESSAGES_H_
