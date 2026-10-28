#include "augusta/protocol.h"

#include <array>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <new>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <boost/uuid/uuid.hpp>
#include <gtest/gtest.h>

#include "augusta/grid.h"
#include "augusta/limits.h"
#include "augusta/math.h"
#include "augusta/tick.h"
#include "augusta/version.h"
#include "protobuf_bytes.h"

// ThreadSanitizer's runtime defines operator new and delete itself, so a build
// under it cannot replace them: there the allocation count is not kept and the
// test reading it is skipped.
#if defined(__SANITIZE_THREAD__)
#define AUGUSTA_COUNTS_ALLOCATIONS 0
#elif defined(__has_feature)
#if __has_feature(thread_sanitizer)
#define AUGUSTA_COUNTS_ALLOCATIONS 0
#endif
#endif
#ifndef AUGUSTA_COUNTS_ALLOCATIONS
#define AUGUSTA_COUNTS_ALLOCATIONS 1
#endif

// MSVC's checked iterators (a debug build's) give every vector an allocation
// of its own beside its elements.
#if defined(_ITERATOR_DEBUG_LEVEL) && _ITERATOR_DEBUG_LEVEL > 0
#define AUGUSTA_VECTORS_ALLOCATE_ONLY_ELEMENTS 0
#else
#define AUGUSTA_VECTORS_ALLOCATE_ONLY_ELEMENTS 1
#endif

// Every byte operator new has handed out in this binary, and in how many
// allocations. Decode gives back no message when it refuses one, so this is
// the only way to see what it allocated on the way.
namespace {
std::size_t bytes_allocated = 0;
std::size_t allocations = 0;
}  // namespace

#if AUGUSTA_COUNTS_ALLOCATIONS
void* operator new(std::size_t size) {
  bytes_allocated += size;
  ++allocations;
  if (void* block = std::malloc(size == 0 ? 1 : size)) {
    return block;
  }
  throw std::bad_alloc();
}

void operator delete(void* block) noexcept { std::free(block); }

void operator delete(void* block, std::size_t /*size*/) noexcept { std::free(block); }
#endif

// The codec is pure: every case here is bytes in, message or error out.
namespace {

using augusta::limits::kMaxPlayers;
using augusta::math::SnapAngle;
using augusta::math::SnapDirection;
using augusta::math::SnapFraction;
using augusta::math::SnapPosition;
using augusta::math::SnapStamina;
using augusta::math::SnapVelocity;
using augusta::math::Vec3;
using augusta::protocol::AuthoritativeStateWire;
using augusta::protocol::BodyPartWire;
using augusta::protocol::BodyStateWire;
using augusta::protocol::BytesWire;
using augusta::protocol::CaptureHeaderWire;
using augusta::protocol::CommandsWire;
using augusta::protocol::CommandWire;
using augusta::protocol::DeathWire;
using augusta::protocol::Decode;
using augusta::protocol::DecodeCaptureRecord;
using augusta::protocol::DecodeError;
using augusta::protocol::Encode;
using augusta::protocol::EncodeCaptureRecord;
using augusta::protocol::EncodeError;
using augusta::protocol::EntityIdWire;
using augusta::protocol::EntityStateWire;
using augusta::protocol::HitConfirmationWire;
using augusta::protocol::JoinAcceptedWire;
using augusta::protocol::JoinRefusalWire;
using augusta::protocol::JoinRefusedWire;
using augusta::protocol::JoinRequestWire;
using augusta::protocol::kMaxCommandsPerMessage;
using augusta::protocol::kMaxEngineVersionLength;
using augusta::protocol::LobbyWire;
using augusta::protocol::MatchEndWire;
using augusta::protocol::MatchPlayerWire;
using augusta::protocol::MatchStartWire;
using augusta::protocol::MessageTypeWire;
using augusta::protocol::MessageWire;
using augusta::protocol::PackHashWire;
using augusta::protocol::ParametersWire;
using augusta::protocol::Readdress;
using augusta::protocol::ReadyWire;
using augusta::protocol::ReenactRequestWire;
using augusta::protocol::ReplayListingWire;
using augusta::protocol::ReplayListRequestWire;
using augusta::protocol::ReplayListWire;
using augusta::protocol::ReplayRequestWire;
using augusta::protocol::RifleWire;
using augusta::protocol::RosterEntryWire;
using augusta::protocol::SequencedCommandWire;
using augusta::protocol::SessionIdWire;
using augusta::protocol::ShotWire;
using augusta::protocol::StanceWire;
using augusta::protocol::WeaponStateWire;

using augusta::protobuf_bytes::Cat;
using augusta::protobuf_bytes::kLengthDelimitedType;
using augusta::protobuf_bytes::LengthField;
using augusta::protobuf_bytes::Repeat;
using augusta::protobuf_bytes::SignedField;
using augusta::protobuf_bytes::Tag;
using augusta::protobuf_bytes::TextField;
using augusta::protobuf_bytes::Varint;
using augusta::protobuf_bytes::VarintField;

// A Session ID, a UUIDv7's 16 bytes, told apart by its last byte.
SessionIdWire Session(std::uint8_t last) {
  return SessionIdWire(
      {0x01, 0x90, 0xA1, 0xB2, 0xC3, 0xD4, 0x70, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, last});
}

// session's 16 bytes, as a bytes field carries them.
BytesWire BytesOf(const SessionIdWire& session) {
  BytesWire bytes;
  for (const std::uint8_t byte : session) {
    bytes.push_back(static_cast<std::byte>(byte));
  }
  return bytes;
}

// The payload of a message of type whose body holds fields.
BytesWire Envelope(MessageTypeWire type, const BytesWire& fields) {
  return LengthField(static_cast<int>(type), fields);
}

// payload followed by one zero byte, which protobuf reads as the tag of a field
// numbered 0, which no message has.
BytesWire WithAZeroByte(BytesWire payload) {
  payload.push_back(std::byte{0});
  return payload;
}

// Every refusal the protocol has.
constexpr std::array<JoinRefusalWire, 9> kEveryRefusal = {
    JoinRefusalWire::kVersionMismatch, JoinRefusalWire::kLobbyFull,        JoinRefusalWire::kUnknownCharacter,
    JoinRefusalWire::kMatchInProgress, JoinRefusalWire::kPackMismatch,     JoinRefusalWire::kReplayServer,
    JoinRefusalWire::kUnknownCapture,  JoinRefusalWire::kNotAReplayServer, JoinRefusalWire::kReenactmentsNotAccepted};

// A client pack hash of 1, 2, 3 ... 32, so its bytes are told apart on the wire.
PackHashWire CountingPackHash() {
  PackHashWire hash{};
  for (std::size_t i = 0; i < hash.size(); ++i) {
    hash[i] = static_cast<std::byte>(i + 1);
  }
  return hash;
}

// A Run ID of 0x11, 0x12 ... 0x20, so its bytes are told apart on the wire.
boost::uuids::uuid CountingRunId() {
  boost::uuids::uuid id{};
  std::uint8_t next = 0x11U;
  for (std::uint8_t& byte : id) {
    byte = next++;
  }
  return id;
}

// CountingRunId's 16 bytes, as a length-delimited field's payload.
std::string CountingRunIdBytes() {
  const boost::uuids::uuid id = CountingRunId();
  return {reinterpret_cast<const char*>(id.begin()), id.size()};
}

// A Match ID of 1, 2, 3 ... 16, so its bytes are told apart on the wire.
boost::uuids::uuid CountingMatchId() {
  boost::uuids::uuid id{};
  std::uint8_t next = 1;
  for (std::uint8_t& byte : id) {
    byte = next++;
  }
  return id;
}

// One message of every type, each one Encode encodes.
std::vector<MessageWire> EveryMessage() {
  return {JoinRequestWire{},
          JoinAcceptedWire{},
          JoinRefusedWire{.reason = JoinRefusalWire::kLobbyFull},
          CommandsWire{},
          AuthoritativeStateWire{},
          LobbyWire{},
          ReadyWire{},
          MatchStartWire{},
          MatchEndWire{},
          ShotWire{},
          HitConfirmationWire{},
          DeathWire{},
          ReplayListRequestWire{},
          ReplayListWire{},
          ReplayRequestWire{},
          ReenactRequestWire{}};
}

MessageWire RoundTrip(const MessageWire& message) {
  const auto decoded = Decode(Encode(message).value());
  EXPECT_TRUE(decoded.has_value());
  return decoded.value_or(MessageWire{});
}

// Requirements: NFR-20
TEST(ProtocolTest, JoinRequestRoundTrips) {
  const auto decoded = RoundTrip(JoinRequestWire{.engine_version = "0.1.0", .character = ""});

  ASSERT_TRUE(std::holds_alternative<JoinRequestWire>(decoded));
  EXPECT_EQ(std::get<JoinRequestWire>(decoded).engine_version, "0.1.0");
}

// Requirements: NFR-20
TEST(ProtocolTest, JoinRequestWithTheLongestVersionRoundTrips) {
  const std::string longest(kMaxEngineVersionLength, 'v');

  const auto decoded = RoundTrip(JoinRequestWire{.engine_version = longest, .character = ""});

  EXPECT_EQ(std::get<JoinRequestWire>(decoded).engine_version, longest);
}

// Either kind of build's version, `+dev` and all (ADR-0074), fits the wire.
// Requirements: US-01, NFR-20
TEST(ProtocolTest, TheEngineVersionFitsAJoinRequest) {
  EXPECT_LE(augusta::EngineVersion().size(), kMaxEngineVersionLength) << augusta::EngineVersion();
}

// Requirements: US-01, NFR-20
TEST(ProtocolTest, JoinRequestCarriesTheClientPackHash) {
  const auto decoded =
      RoundTrip(JoinRequestWire{.engine_version = "0.1.0", .client_pack = CountingPackHash(), .character = ""});

  EXPECT_EQ(std::get<JoinRequestWire>(decoded).client_pack, CountingPackHash());
}

// Requirements: US-16, NFR-20
TEST(ProtocolTest, JoinRequestCarriesTheChosenCharacter) {
  const auto decoded = RoundTrip(JoinRequestWire{.engine_version = "0.1.0", .character = "soldier"});

  EXPECT_EQ(std::get<JoinRequestWire>(decoded).character, "soldier");
}

// Requirements: NFR-20
TEST(ProtocolTest, JoinRequestWithTheLongestCharacterRoundTrips) {
  const std::string longest(augusta::protocol::kMaxCharacterNameLength, 'c');

  const auto decoded = RoundTrip(JoinRequestWire{.engine_version = "0.1.0", .character = longest});

  EXPECT_EQ(std::get<JoinRequestWire>(decoded).character, longest);
}

// Requirements: NFR-12
TEST(ProtocolTest, ACharacterLongerThanAllowedIsTooLong) {
  const std::string too_long(augusta::protocol::kMaxCharacterNameLength + 1, 'c');

  EXPECT_EQ(Decode(Envelope(MessageTypeWire::kJoinRequest, TextField(3, too_long))).error(),
            DecodeError::kFieldTooLong);
}

// Requirements: NFR-20
TEST(ProtocolTest, JoinRequestWithAnEmptyVersionRoundTrips) {
  const auto decoded = RoundTrip(JoinRequestWire{});

  EXPECT_EQ(std::get<JoinRequestWire>(decoded).engine_version, "");
}

// The client's Run ID, which the server logs at admission (ADR-0038).
// Requirements: NFR-19
TEST(ProtocolTest, AJoinRequestCarriesItsRunId) {
  const auto decoded = RoundTrip(JoinRequestWire{.engine_version = "0.1.0", .character = "", .run = CountingRunId()});

  EXPECT_EQ(std::get<JoinRequestWire>(decoded).run, CountingRunId());
}

// Requirements: NFR-19
TEST(ProtocolTest, AJoinRequestsRunIdIsItsFourthFieldsSixteenBytes) {
  const auto decoded = Decode(Envelope(MessageTypeWire::kJoinRequest, TextField(4, CountingRunIdBytes())));

  EXPECT_EQ(decoded.value(),
            MessageWire(JoinRequestWire{.engine_version = "", .character = "", .run = CountingRunId()}));
}

// Requirements: NFR-19
TEST(ProtocolTest, AJoinRequestWithNoRunIdReadsAsTheNilOne) {
  const auto decoded = Decode(Envelope(MessageTypeWire::kJoinRequest, TextField(1, "0.1.0")));

  EXPECT_TRUE(std::get<JoinRequestWire>(decoded.value()).run.is_nil());
}

// Requirements: NFR-12
TEST(ProtocolTest, ARunIdOfAnyOtherLengthThanSixteenBytesIsOutOfRange) {
  const std::string bytes = CountingRunIdBytes();

  EXPECT_EQ(Decode(Envelope(MessageTypeWire::kJoinRequest, TextField(4, bytes.substr(1)))).error(),
            DecodeError::kOutOfRange);
  EXPECT_EQ(Decode(Envelope(MessageTypeWire::kJoinRequest, TextField(4, bytes + "x"))).error(),
            DecodeError::kOutOfRange);
}

// actual is what Decode gave back for expected: its numbers on their grids.
void ExpectSnappedBody(const BodyStateWire& actual, const BodyStateWire& expected) {
  EXPECT_EQ(actual.position, SnapPosition(expected.position));
  EXPECT_EQ(actual.velocity, SnapVelocity(expected.velocity));
  EXPECT_EQ(actual.stance, expected.stance);
  EXPECT_EQ(actual.stamina, SnapStamina(expected.stamina));
  EXPECT_EQ(actual.flags, expected.flags);
}

EntityStateWire BodyAt(std::uint32_t entity, float x) {
  EntityStateWire body{.entity = static_cast<EntityIdWire>(entity)};
  body.body.position = Vec3(x, 1.0F, -2.5F);
  body.body.velocity = Vec3(0.5F, 0.0F, 3.0F);
  body.body.stance = augusta::protocol::StanceWire::kCrouching;
  body.body.stamina = 0.75F;
  return body;
}

// ADR-0050: a Captured player's first message is a Reenact request, the Join
// request's fields in the Join request's encoding, then its spawn.
// Requirements: US-21
TEST(ProtocolTest, AReenactRequestRoundTripsWithItsSpawn) {
  const ReenactRequestWire request{.engine_version = "0.1.0",
                                   .client_pack = CountingPackHash(),
                                   .character = "soldier",
                                   .spawn = Vec3(10.0F, 0.5F, -20.0F),
                                   .run = CountingRunId()};

  const auto decoded = RoundTrip(request);

  ASSERT_TRUE(std::holds_alternative<ReenactRequestWire>(decoded));
  EXPECT_EQ(std::get<ReenactRequestWire>(decoded), request);
}

// Requirements: US-21
TEST(ProtocolTest, AReenactRequestIsAJoinRequestsFieldsUnderItsOwnTypeThenTheSpawn) {
  const BytesWire join_fields = Cat({TextField(1, "0.1.0"), TextField(3, "soldier")});

  const auto decoded = Decode(
      Envelope(MessageTypeWire::kReenactRequest, Cat({join_fields, SignedField(4, 1024), SignedField(6, -2048)})));

  EXPECT_EQ(static_cast<int>(MessageTypeWire::kReenactRequest), 17);
  EXPECT_EQ(decoded.value(), MessageWire(ReenactRequestWire{
                                 .engine_version = "0.1.0", .character = "soldier", .spawn = Vec3(1.0F, 0.0F, -2.0F)}));
}

// Requirements: US-21
TEST(ProtocolTest, AReenactRequestsRunIdIsItsSeventhField) {
  const auto decoded = Decode(Envelope(MessageTypeWire::kReenactRequest, TextField(7, CountingRunIdBytes())));

  EXPECT_EQ(decoded.value(),
            MessageWire(ReenactRequestWire{.engine_version = "", .character = "", .run = CountingRunId()}));
}

// Requirements: NFR-12
TEST(ProtocolTest, AReenactRequestsVersionAndCharacterAreBoundAsAJoinRequestsAre) {
  ReenactRequestWire long_version;
  long_version.engine_version = std::string(kMaxEngineVersionLength + 1, 'v');
  ReenactRequestWire long_character;
  long_character.character = std::string(augusta::protocol::kMaxCharacterNameLength + 1, 'c');

  EXPECT_EQ(Encode(long_version).error(), EncodeError::kFieldTooLong);
  EXPECT_EQ(Encode(long_character).error(), EncodeError::kFieldTooLong);
}

// Requirements: US-01, US-22, NFR-20
TEST(ProtocolTest, JoinAcceptedRoundTrips) {
  JoinAcceptedWire sent{
      .session = Session(0xD4),
      .tick_rate_hz = 30,
      .parameters = {.stamina = {.deplete_per_second = 0.2F, .regen_per_second = 0.1F, .forced_walk_below = 0.05F},
                     .rifle = {.rounds_per_minute = 600.0F,
                               .muzzle_velocity = 800.0F,
                               .reload_seconds = 2.5F,
                               .recoil_recovery_per_second = 0.2F,
                               .ads_recoil_scale = 0.5F,
                               .ads_field_of_view = 0.7F,
                               .recoil_pattern = {{.pitch = 0.01F, .yaw = 0.002F}, {.pitch = 0.008F, .yaw = -0.002F}},
                               .magazine_capacity = 30},
                     .ammo = {.gravity = 9.81F,
                              .max_range = 1000.0F,
                              .head_damage = 100.0F,
                              .torso_damage = 34.0F,
                              .limb_damage = 25.0F},
                     .starting_health = 100.0F,
                     .player_count = 5},
      .character = "sniper"};

  const auto decoded = RoundTrip(sent);

  ASSERT_TRUE(std::holds_alternative<JoinAcceptedWire>(decoded));
  const auto& received = std::get<JoinAcceptedWire>(decoded);
  EXPECT_EQ(received.session, sent.session);
  EXPECT_EQ(received.tick_rate_hz, sent.tick_rate_hz);
  EXPECT_EQ(received.parameters, sent.parameters);
  EXPECT_EQ(received.character, sent.character);
}

// Requirements: NFR-12
TEST(ProtocolTest, ACharacterInJoinAcceptedLongerThanTheLimitIsTooLong) {
  const std::string too_long(augusta::protocol::kMaxCharacterNameLength + 1, 'c');

  EXPECT_EQ(Decode(Envelope(MessageTypeWire::kJoinAccepted, TextField(4, too_long))).error(),
            DecodeError::kFieldTooLong);
}

// Requirements: US-01, NFR-20
TEST(ProtocolTest, JoinRefusedRoundTripsEveryReason) {
  for (const JoinRefusalWire reason : kEveryRefusal) {
    const auto decoded = RoundTrip(JoinRefusedWire{.reason = reason});

    ASSERT_TRUE(std::holds_alternative<JoinRefusedWire>(decoded));
    EXPECT_EQ(std::get<JoinRefusedWire>(decoded).reason, reason);
  }
}

// The refusal a payload carries as a number.
JoinRefusalWire RefusalNumbered(std::uint64_t number) {
  return std::get<JoinRefusedWire>(Decode(Envelope(MessageTypeWire::kJoinRefused, VarintField(1, number))).value())
      .reason;
}

// A full Lobby is refused with the value a full match was, under its new name.
// Requirements: NFR-20
TEST(ProtocolTest, ALobbyFullRefusalKeepsTheWireValueOfAFullMatch) {
  EXPECT_EQ(RefusalNumbered(2), JoinRefusalWire::kLobbyFull);
  EXPECT_EQ(RefusalNumbered(4), JoinRefusalWire::kMatchInProgress);
}

// Requirements: US-16, NFR-20
TEST(ProtocolTest, LobbyRoundTrips) {
  const LobbyWire sent{
      .version = 42,
      .roster = {RosterEntryWire{.session = Session(7), .character = "soldier"},
                 RosterEntryWire{.session = Session(9),
                                 .character = std::string(augusta::protocol::kMaxCharacterNameLength, 'c')}}};

  const auto decoded = RoundTrip(sent);

  ASSERT_TRUE(std::holds_alternative<LobbyWire>(decoded));
  const auto& received = std::get<LobbyWire>(decoded);
  EXPECT_EQ(received.version, sent.version);
  ASSERT_EQ(received.roster.size(), sent.roster.size());
  for (std::size_t i = 0; i < sent.roster.size(); ++i) {
    EXPECT_EQ(received.roster[i].session, sent.roster[i].session);
    EXPECT_EQ(received.roster[i].character, sent.roster[i].character);
  }
}

// Requirements: NFR-06, NFR-20
TEST(ProtocolTest, AnEmptyLobbyAndAFullOneRoundTrip) {
  EXPECT_TRUE(std::get<LobbyWire>(RoundTrip(LobbyWire{.version = 1, .roster = {}})).roster.empty());

  LobbyWire full{.version = 1, .roster = {}};
  full.roster.resize(kMaxPlayers);
  EXPECT_EQ(std::get<LobbyWire>(RoundTrip(full)).roster.size(), kMaxPlayers);
}

// Requirements: NFR-12
TEST(ProtocolTest, MorePlayersInALobbyThanItHoldsIsTooLong) {
  const BytesWire entry = LengthField(2, LengthField(3, BytesOf(Session(7))));

  EXPECT_TRUE(Decode(Envelope(MessageTypeWire::kLobby, Repeat(entry, kMaxPlayers))).has_value());
  EXPECT_EQ(Decode(Envelope(MessageTypeWire::kLobby, Repeat(entry, kMaxPlayers + 1))).error(),
            DecodeError::kFieldTooLong);
}

// Requirements: NFR-12
TEST(ProtocolTest, ACharacterInALobbyLongerThanTheLimitIsTooLong) {
  const std::string too_long(augusta::protocol::kMaxCharacterNameLength + 1, 'c');

  EXPECT_EQ(Decode(Envelope(MessageTypeWire::kLobby, LengthField(2, TextField(2, too_long)))).error(),
            DecodeError::kFieldTooLong);
}

// A player whose body is entity 100 more than its session, so the two never pass for each other.
MatchPlayerWire MatchPlayer(std::uint8_t session, std::string character, float x) {
  return MatchPlayerWire{.spawn = Vec3(x, 0.5F, -8.0F),
                         .session = Session(session),
                         .entity = static_cast<EntityIdWire>(session + 100U),
                         .character = std::move(character)};
}

// Requirements: US-16, NFR-20
TEST(ProtocolTest, MatchStartRoundTripsWithEveryPlayersCharacterAndSpawnPoint) {
  const MatchStartWire sent{.players = {MatchPlayer(3, "soldier", 4.0F), MatchPlayer(4, "sniper", -12.345F)},
                            .first_tick = 0x1'0000'0007ULL};

  const auto decoded = RoundTrip(sent);

  ASSERT_TRUE(std::holds_alternative<MatchStartWire>(decoded));
  const auto& received = std::get<MatchStartWire>(decoded);
  ASSERT_EQ(received.players.size(), sent.players.size());
  for (std::size_t i = 0; i < sent.players.size(); ++i) {
    EXPECT_EQ(received.players[i].session, sent.players[i].session);
    EXPECT_EQ(received.players[i].entity, sent.players[i].entity);
    EXPECT_EQ(received.players[i].character, sent.players[i].character);
    EXPECT_EQ(received.players[i].spawn, SnapPosition(sent.players[i].spawn));
  }
  EXPECT_EQ(received.first_tick, sent.first_tick);
}

// Requirements: NFR-19
TEST(ProtocolTest, MatchStartRoundTripsWithItsMatchId) {
  const auto decoded = RoundTrip(MatchStartWire{.players = {}, .first_tick = 0, .match = CountingMatchId()});

  EXPECT_EQ(std::get<MatchStartWire>(decoded).match, CountingMatchId());
}

// A Match ID is 16 bytes, or absent; any other length is none a server writes.
// Requirements: NFR-12
TEST(ProtocolTest, AMatchIdOfAnyOtherLengthThanSixteenBytesIsOutOfRange) {
  const BytesWire fifteen(15, std::byte{1});
  const BytesWire seventeen(17, std::byte{1});

  EXPECT_EQ(Decode(Envelope(MessageTypeWire::kMatchStart, LengthField(3, fifteen))).error(), DecodeError::kOutOfRange);
  EXPECT_EQ(Decode(Envelope(MessageTypeWire::kMatchStart, LengthField(3, seventeen))).error(),
            DecodeError::kOutOfRange);
}

// Requirements: NFR-06, NFR-20
TEST(ProtocolTest, AMatchStartOfAFullMatchRoundTrips) {
  MatchStartWire sent;
  sent.players.assign(kMaxPlayers, MatchPlayer(1, "soldier", 0.0F));

  EXPECT_EQ(std::get<MatchStartWire>(RoundTrip(sent)).players.size(), kMaxPlayers);
}

// Requirements: NFR-12
TEST(ProtocolTest, MorePlayersInAMatchStartThanAMatchHoldsIsTooLong) {
  EXPECT_EQ(Decode(Envelope(MessageTypeWire::kMatchStart, Repeat(LengthField(1, {}), kMaxPlayers + 1))).error(),
            DecodeError::kFieldTooLong);
}

// Requirements: NFR-20
TEST(ProtocolTest, ReadyRoundTripsTheVersionItWasLoadedFor) {
  const auto decoded = RoundTrip(ReadyWire{.version = 0xA1B2C3D4U});

  ASSERT_TRUE(std::holds_alternative<ReadyWire>(decoded));
  EXPECT_EQ(std::get<ReadyWire>(decoded).version, 0xA1B2C3D4U);
}

// Requirements: US-14, NFR-20
TEST(ProtocolTest, MatchEndRoundTripsItsWinnersSession) {
  const auto decoded = RoundTrip(MatchEndWire{.winner = Session(0xD4)});

  ASSERT_TRUE(std::holds_alternative<MatchEndWire>(decoded));
  EXPECT_EQ(std::get<MatchEndWire>(decoded).winner, Session(0xD4));
}

// No Session ID is the nil UUID, so a winner of all zeros is a draw
// (ADR-0038): one with no winner at all, as an absent field reads as nil.
// Requirements: US-14, NFR-20
TEST(ProtocolTest, AMatchEndThatIsADrawNamesTheNilSession) {
  EXPECT_EQ(std::get<MatchEndWire>(RoundTrip(MatchEndWire{.winner = augusta::protocol::kDraw})).winner,
            augusta::protocol::kDraw);
  EXPECT_EQ(Decode(Envelope(MessageTypeWire::kMatchEnd, {})).value(), MessageWire(MatchEndWire{}));
}

// A Session ID travels as its 16 bytes, in RFC 9562's order (ADR-0038).
// Requirements: NFR-19, NFR-20
TEST(ProtocolTest, ASessionIdTravelsAsItsSixteenBytes) {
  EXPECT_EQ(Decode(Envelope(MessageTypeWire::kMatchEnd, LengthField(2, BytesOf(Session(0x2A))))).value(),
            MessageWire(MatchEndWire{.winner = Session(0x2A)}));
  EXPECT_EQ(Encode(MessageWire(MatchEndWire{.winner = Session(0x2A)})).value(),
            Envelope(MessageTypeWire::kMatchEnd, LengthField(2, BytesOf(Session(0x2A)))));
}

// Requirements: NFR-12
TEST(ProtocolTest, ASessionIdOfAnyLengthButSixteenBytesIsOutOfRange) {
  for (const std::size_t length : {1U, 15U, 17U}) {
    const BytesWire session(length, std::byte{0x7F});
    EXPECT_EQ(Decode(Envelope(MessageTypeWire::kJoinAccepted, LengthField(5, session))).error(),
              DecodeError::kOutOfRange)
        << length;
    EXPECT_EQ(Decode(Envelope(MessageTypeWire::kMatchEnd, LengthField(2, session))).error(), DecodeError::kOutOfRange)
        << length;
  }
}

// Requirements: US-19, NFR-20
TEST(ProtocolTest, AShotRoundTripsWithItsShooterTickOriginAndDirection) {
  const ShotWire shot{.tick = 1200,
                      .origin = Vec3(12.5F, 1.75F, -40.0F),
                      .shooter = static_cast<EntityIdWire>(0xA1B2C3D4U),
                      .yaw = -1.5F,
                      .pitch = 0.25F};

  const auto decoded = RoundTrip(shot);

  ASSERT_TRUE(std::holds_alternative<ShotWire>(decoded));
  EXPECT_EQ(std::get<ShotWire>(decoded), shot);
}

// Its origin and its direction travel as counts of the grids a body's position
// and a command's view do (ADR-0038).
// Requirements: NFR-20
TEST(ProtocolTest, AShotsOriginAndDirectionTravelAsCountsOfTheirGrids) {
  const BytesWire shot = Cat({VarintField(1, 1200), VarintField(2, 7), SignedField(3, 1024), SignedField(5, -1),
                              SignedField(6, 2097152), SignedField(7, -1)});

  EXPECT_EQ(Decode(Envelope(MessageTypeWire::kShot, shot)).value(),
            MessageWire(ShotWire{.tick = 1200,
                                 .origin = Vec3(1.0F, 0.0F, -1.0F / 1024.0F),
                                 .shooter = static_cast<EntityIdWire>(7),
                                 .yaw = 1.0F,
                                 .pitch = -1.0F / 2097152.0F}));
}

// Requirements: US-19, NFR-20
TEST(ProtocolTest, AHitConfirmationRoundTripsWithItsTargetBodyPartAndDamage) {
  for (const BodyPartWire part : {BodyPartWire::kHead, BodyPartWire::kTorso, BodyPartWire::kLimb}) {
    const HitConfirmationWire hit{.target = static_cast<EntityIdWire>(0xA1B2C3D4U), .damage = 37.5F, .part = part};

    const auto decoded = RoundTrip(hit);

    ASSERT_TRUE(std::holds_alternative<HitConfirmationWire>(decoded));
    EXPECT_EQ(std::get<HitConfirmationWire>(decoded), hit);
  }
}

// Requirements: NFR-12
TEST(ProtocolTest, AHitConfirmationsBodyPartOutsideItsRangeIsInvalid) {
  EXPECT_TRUE(Decode(Envelope(MessageTypeWire::kHitConfirmation, VarintField(2, 3))).has_value());
  for (const std::uint64_t bad : {0U, 4U, 255U}) {
    EXPECT_EQ(Decode(Envelope(MessageTypeWire::kHitConfirmation, VarintField(2, bad))).error(),
              DecodeError::kInvalidEnum)
        << bad;
  }
}

// Requirements: US-13, NFR-20
TEST(ProtocolTest, ADeathRoundTripsWithItsVictimKillerBodyPartAndDirection) {
  for (const BodyPartWire part : {BodyPartWire::kHead, BodyPartWire::kTorso, BodyPartWire::kLimb}) {
    const DeathWire death{.victim = static_cast<EntityIdWire>(0xA1B2C3D4U),
                          .killer = static_cast<EntityIdWire>(7),
                          .yaw = -1.5F,
                          .pitch = 0.25F,
                          .part = part};

    const auto decoded = RoundTrip(death);

    ASSERT_TRUE(std::holds_alternative<DeathWire>(decoded));
    EXPECT_EQ(std::get<DeathWire>(decoded), death);
  }
}

// Requirements: NFR-12
TEST(ProtocolTest, ADeathsBodyPartOutsideItsRangeIsInvalid) {
  EXPECT_TRUE(Decode(Envelope(MessageTypeWire::kDeath, VarintField(3, 1))).has_value());
  for (const std::uint64_t bad : {0U, 4U, 255U}) {
    EXPECT_EQ(Decode(Envelope(MessageTypeWire::kDeath, VarintField(3, bad))).error(), DecodeError::kInvalidEnum) << bad;
  }
}

// Requirements: NFR-12
TEST(ProtocolTest, ACharacterInAMatchStartLongerThanTheLimitIsTooLong) {
  const std::string too_long(augusta::protocol::kMaxCharacterNameLength + 1, 'c');

  EXPECT_EQ(Decode(Envelope(MessageTypeWire::kMatchStart, LengthField(1, TextField(3, too_long)))).error(),
            DecodeError::kFieldTooLong);
}

// Requirements: NFR-12
TEST(ProtocolTest, AnEmptyPayloadIsOfNoTypeThisBuildKnows) {
  EXPECT_EQ(Decode(BytesWire{}).error(), DecodeError::kUnknownType);
}

// The envelope's members number from 1, one per message: a member past them
// all (999, below a capture record's 1000) is a field this build does not know,
// ignored, which leaves the envelope with no body.
// Requirements: NFR-12
TEST(ProtocolTest, AnEnvelopeWithNoMemberThisBuildKnowsIsOfAnUnknownType) {
  EXPECT_TRUE(Decode(Envelope(MessageTypeWire::kReenactRequest, {})).has_value());
  EXPECT_EQ(Decode(LengthField(999, {})).error(), DecodeError::kUnknownType);
  EXPECT_EQ(Decode(VarintField(1, 1)).error(), DecodeError::kUnknownType);
}

// A capture record's envelope numbers its members from 1000 (ADR-0007), so a
// record is never a message.
// Requirements: NFR-12
TEST(ProtocolTest, ACaptureRecordIsOfAnUnknownType) {
  const BytesWire leave = Cat({VarintField(1, 12), VarintField(2, 3)});

  EXPECT_EQ(Decode(LengthField(1004, leave)).error(), DecodeError::kUnknownType);
}

// Requirements: NFR-12
TEST(ProtocolTest, EveryTruncationOfEveryMessageIsMalformedNotACrash) {
  const std::array<MessageWire, 16> messages = {
      JoinRequestWire{.engine_version = "0.1.0", .character = "soldier"},
      ReenactRequestWire{.engine_version = "0.1.0", .character = "soldier", .spawn = Vec3(1.0F, 2.0F, 3.0F)},
      JoinAcceptedWire{.session = Session(7), .character = "soldier"},
      JoinRefusedWire{.reason = JoinRefusalWire::kMatchInProgress},
      CommandsWire{.commands = {SequencedCommandWire{.sequence = 1}, {.sequence = 2}}},
      AuthoritativeStateWire{.tick = 3, .bodies = {EntityStateWire{}, {}}, .queued_commands = 2},
      LobbyWire{.version = 2, .roster = {RosterEntryWire{.character = "a"}, {.character = "b"}}},
      MatchStartWire{.players = {MatchPlayer(1, "soldier", 0.0F), MatchPlayer(2, "a", 1.0F)}},
      ReadyWire{.version = 0x01020304U},
      ShotWire{.tick = 9, .origin = Vec3(1.0F, 2.0F, 3.0F), .shooter = static_cast<EntityIdWire>(7)},
      HitConfirmationWire{.target = static_cast<EntityIdWire>(7), .damage = 20.0F, .part = BodyPartWire::kHead},
      DeathWire{.victim = static_cast<EntityIdWire>(7), .killer = static_cast<EntityIdWire>(8)},
      MatchEndWire{.winner = Session(3)},
      ReplayListWire{.replays = {ReplayListingWire{.characters = {"soldier"}, .name = "a.acap", .ticks = 9}}},
      ReplayRequestWire{.engine_version = "0.1.0", .capture = "a.acap"}};
  for (const MessageWire& message : messages) {
    const BytesWire whole = Encode(message).value();
    for (std::size_t length = 1; length < whole.size(); ++length) {
      const BytesWire cut(whole.begin(), whole.begin() + static_cast<std::ptrdiff_t>(length));
      EXPECT_EQ(Decode(cut).error(), DecodeError::kMalformed)
          << "type " << static_cast<int>(augusta::protocol::TypeOf(message)) << " cut to " << length;
    }
  }
}

// Requirements: NFR-12
TEST(ProtocolTest, ALengthPointingPastThePayloadIsMalformedWithoutReadingIt) {
  // Claims 32 bytes of version, supplies 2.
  const BytesWire version = Cat({Tag(1, kLengthDelimitedType), Varint(32), BytesWire{std::byte{'a'}, std::byte{'b'}}});

  EXPECT_EQ(Decode(Envelope(MessageTypeWire::kJoinRequest, version)).error(), DecodeError::kMalformed);
}

// Requirements: NFR-12
TEST(ProtocolTest, AVersionLongerThanAllowedIsTooLongEvenWhenAllOfItIsPresent) {
  const std::string longest(kMaxEngineVersionLength, 'v');

  EXPECT_TRUE(Decode(Envelope(MessageTypeWire::kJoinRequest, TextField(1, longest))).has_value());
  EXPECT_EQ(Decode(Envelope(MessageTypeWire::kJoinRequest, TextField(1, longest + "v"))).error(),
            DecodeError::kFieldTooLong);
}

// Requirements: NFR-12
TEST(ProtocolTest, ARefusalReasonOutsideTheEnumerationIsInvalid) {
  for (const std::uint64_t bad : {0U, 10U, 0xFFU}) {
    EXPECT_EQ(Decode(Envelope(MessageTypeWire::kJoinRefused, VarintField(1, bad))).error(), DecodeError::kInvalidEnum)
        << bad;
  }
}

// A refusal that names no reason holds the enumeration's unspecified 0, which
// no sender writes.
// Requirements: NFR-12
TEST(ProtocolTest, ARefusalWithNoReasonIsInvalid) {
  EXPECT_EQ(Decode(Envelope(MessageTypeWire::kJoinRefused, {})).error(), DecodeError::kInvalidEnum);
}

// Requirements: NFR-12
TEST(ProtocolTest, AZeroByteAfterAnyMessageIsMalformed) {
  for (const MessageWire& message : EveryMessage()) {
    EXPECT_EQ(Decode(WithAZeroByte(Encode(message).value())).error(), DecodeError::kMalformed)
        << static_cast<int>(augusta::protocol::TypeOf(message));
  }
}

// The two refusals a replay server gives are appended after every older one,
// which keep their wire values (ADR-0051).
// Requirements: NFR-20
TEST(ProtocolTest, AReplayServersRefusalsFollowEveryOlderOne) {
  EXPECT_EQ(RefusalNumbered(6), JoinRefusalWire::kReplayServer);
  EXPECT_EQ(RefusalNumbered(7), JoinRefusalWire::kUnknownCapture);
  EXPECT_EQ(RefusalNumbered(8), JoinRefusalWire::kNotAReplayServer);
}

// Requirements: NFR-20
TEST(ProtocolTest, AReplayListRequestIsItsTypeAlone) {
  EXPECT_EQ(Encode(ReplayListRequestWire{}).value(), Envelope(MessageTypeWire::kReplayListRequest, {}));
  EXPECT_TRUE(std::holds_alternative<ReplayListRequestWire>(RoundTrip(ReplayListRequestWire{})));
}

// Requirements: US-21
TEST(ProtocolTest, AReplayListRoundTripsEachCapturesNameStartLengthAndCharacters) {
  const ReplayListWire list{
      .replays = {ReplayListingWire{.started_unix_ms = 1'791'000'000'123,
                                    .characters = {"soldier", "sniper"},
                                    .name = "20261009T120000123Z-0001.acap",
                                    .ticks = 7200,
                                    .tick_rate_hz = 60},
                  ReplayListingWire{
                      .started_unix_ms = -5, .characters = {}, .name = "b.acap", .ticks = 0, .tick_rate_hz = 30}}};

  EXPECT_EQ(RoundTrip(list), MessageWire{list});
}

// Requirements: NFR-20
TEST(ProtocolTest, AnEmptyReplayListRoundTrips) {
  EXPECT_EQ(RoundTrip(ReplayListWire{}), MessageWire{ReplayListWire{}});
}

// Requirements: NFR-12
TEST(ProtocolTest, AReplayListsLongNamesAndListsAreTooLong) {
  const std::string longest(augusta::protocol::kMaxCaptureNameLength, 'n');
  EXPECT_TRUE(Encode(ReplayListWire{.replays = {ReplayListingWire{.characters = {}, .name = longest}}}).has_value());
  EXPECT_EQ(Encode(ReplayListWire{.replays = {ReplayListingWire{.characters = {}, .name = longest + "n"}}}).error(),
            EncodeError::kFieldTooLong);
  ReplayListingWire crowded;
  crowded.characters.assign(kMaxPlayers + 1, "soldier");
  EXPECT_EQ(Encode(ReplayListWire{.replays = {crowded}}).error(), EncodeError::kFieldTooLong);
  ReplayListWire many;
  many.replays.resize(augusta::protocol::kMaxReplayListings + 1);
  EXPECT_EQ(Encode(many).error(), EncodeError::kFieldTooLong);
}

// How many bytes Decode allocates for payload, whatever it makes of it.
std::size_t BytesDecodeAllocates(const BytesWire& payload) {
  const std::size_t before = bytes_allocated;
  static_cast<void>(Decode(payload));
  return bytes_allocated - before;
}

// A length is a claim the bytes after it must back: a payload claiming a
// million bytes of Replay list allocates no more than one claiming one byte.
// Requirements: NFR-12
TEST(ProtocolTest, ALengthPastTheEndOfThePayloadIsMalformedWithoutAllocatingForIt) {
  if (!AUGUSTA_COUNTS_ALLOCATIONS) {
    GTEST_SKIP() << "operator new is ThreadSanitizer's in this build, so allocations are not counted";
  }
  const auto claiming = [](std::uint64_t length) {
    return Cat({Tag(static_cast<int>(MessageTypeWire::kReplayList), kLengthDelimitedType), Varint(length)});
  };
  const BytesWire claims_one = claiming(1);
  const BytesWire claims_most = claiming(1'000'000);

  EXPECT_EQ(Decode(claims_most).error(), DecodeError::kMalformed);
  EXPECT_EQ(BytesDecodeAllocates(claims_most), BytesDecodeAllocates(claims_one));
}

// Requirements: US-21, NFR-20
TEST(ProtocolTest, AReplayRequestRoundTripsItsVersionPackCaptureAndRunId) {
  const ReplayRequestWire request{.engine_version = "0.1.0",
                                  .client_pack = CountingPackHash(),
                                  .capture = "20261009T120000123Z-0001.acap",
                                  .run = CountingRunId()};

  EXPECT_EQ(RoundTrip(request), MessageWire{request});
}

// A Replay viewer's Run ID, as a Join request's (ADR-0051).
// Requirements: NFR-19
TEST(ProtocolTest, AReplayRequestsRunIdIsItsFourthField) {
  const auto decoded = Decode(Envelope(MessageTypeWire::kReplayRequest, TextField(4, CountingRunIdBytes())));

  EXPECT_EQ(decoded.value(),
            MessageWire(ReplayRequestWire{.engine_version = "", .capture = "", .run = CountingRunId()}));
}

// A capture is named, never pathed: the codec carries any name up to its
// limit, and the replay server matches it against its listing (ADR-0051).
// Requirements: NFR-12
TEST(ProtocolTest, AReplayRequestsCaptureNameLongerThanAllowedIsTooLong) {
  const std::string longest(augusta::protocol::kMaxCaptureNameLength, 'n');
  EXPECT_TRUE(Encode(ReplayRequestWire{.engine_version = "", .client_pack = {}, .capture = longest}).has_value());
  EXPECT_EQ(Encode(ReplayRequestWire{.engine_version = "", .client_pack = {}, .capture = longest + "n"}).error(),
            EncodeError::kFieldTooLong);

  EXPECT_EQ(Decode(Envelope(MessageTypeWire::kReplayRequest, TextField(3, longest + "n"))).error(),
            DecodeError::kFieldTooLong);
}

// The Replay view is gone, its pitch and ADS each body's (ADR-0038): a payload
// of its old member is one this build lacks.
// Requirements: NFR-12
TEST(ProtocolTest, TheReplayViewsOldMemberIsUnknown) {
  EXPECT_EQ(Decode(LengthField(16, VarintField(1, 4))).error(), DecodeError::kUnknownType);
}

// A command with every field set to something other than its default.
SequencedCommandWire BusyCommand(augusta::tick::Sequence sequence) {
  SequencedCommandWire sequenced{.sequence = sequence};
  sequenced.command.direction = Vec3(0.5F, -0.25F, 1.0F);
  sequenced.command.yaw = 3.5F;
  sequenced.command.pitch = -1.25F;
  sequenced.command.flags = CommandWire::kSprint | CommandWire::kAds | CommandWire::kFire | CommandWire::kReload;
  sequenced.command.desired_stance = augusta::protocol::StanceWire::kProne;
  sequenced.command.seen_age = static_cast<std::uint8_t>(sequence);
  sequenced.command.seen_fraction = 0.3F;
  return sequenced;
}

// actual is what Decode gave back for expected: its numbers on their grids.
void ExpectSameCommand(const SequencedCommandWire& actual, const SequencedCommandWire& expected) {
  EXPECT_EQ(actual.sequence, expected.sequence);
  EXPECT_EQ(actual.command.direction, SnapDirection(expected.command.direction));
  EXPECT_EQ(actual.command.yaw, SnapAngle(expected.command.yaw));
  EXPECT_EQ(actual.command.pitch, SnapAngle(expected.command.pitch));
  EXPECT_EQ(actual.command.flags, expected.command.flags);
  EXPECT_EQ(actual.command.desired_stance, expected.command.desired_stance);
  EXPECT_EQ(actual.command.seen_age, expected.command.seen_age);
  EXPECT_EQ(actual.command.seen_fraction, SnapFraction(expected.command.seen_fraction));
}

// Requirements: NFR-20
TEST(ProtocolTest, CommandsRoundTripWithEveryField) {
  const CommandsWire sent{.commands = {BusyCommand(41), BusyCommand(42), SequencedCommandWire{.sequence = 43}},
                          .seen_tick = 123456};

  const auto decoded = RoundTrip(sent);

  ASSERT_TRUE(std::holds_alternative<CommandsWire>(decoded));
  const CommandsWire& received = std::get<CommandsWire>(decoded);
  EXPECT_EQ(received.seen_tick, sent.seen_tick);
  ASSERT_EQ(received.commands.size(), sent.commands.size());
  for (std::size_t i = 0; i < sent.commands.size(); ++i) {
    ExpectSameCommand(received.commands[i], sent.commands[i]);
  }
}

// Requirements: NFR-20
TEST(ProtocolTest, CommandsWithNoCommandsRoundTrip) {
  const auto decoded = RoundTrip(CommandsWire{});

  EXPECT_TRUE(std::get<CommandsWire>(decoded).commands.empty());
}

// Requirements: NFR-20
TEST(ProtocolTest, CommandsCarryTheMostAMessageAllows) {
  CommandsWire sent;
  for (std::uint32_t i = 0; i < kMaxCommandsPerMessage; ++i) {
    sent.commands.push_back(BusyCommand(i + 1));
  }

  EXPECT_EQ(std::get<CommandsWire>(RoundTrip(sent)).commands.size(), kMaxCommandsPerMessage);
}

// Requirements: NFR-12
TEST(ProtocolTest, MoreCommandsThanAMessageAllowsIsTooLong) {
  const BytesWire command = LengthField(1, VarintField(1, 1));

  EXPECT_TRUE(Decode(Envelope(MessageTypeWire::kCommands, Repeat(command, kMaxCommandsPerMessage))).has_value());
  EXPECT_EQ(Decode(Envelope(MessageTypeWire::kCommands, Repeat(command, kMaxCommandsPerMessage + 1))).error(),
            DecodeError::kFieldTooLong);
}

// Every message a client sends, each of its lists full, its strings at their
// longest and its numbers at their widest, fits what a server takes in.
// Requirements: NFR-05
TEST(ProtocolTest, EveryClientMessageAtItsLargestFitsTheClientPayloadBound) {
  const float far = augusta::math::FromSteps(augusta::math::kPositionGrid.min, augusta::math::kPositionGrid);
  const float far_direction =
      augusta::math::FromSteps(augusta::math::kDirectionGrid.min, augusta::math::kDirectionGrid);
  const float far_angle = augusta::math::FromSteps(augusta::math::kAngleGrid.min, augusta::math::kAngleGrid);
  const float last_fraction = augusta::math::FromSteps(augusta::math::kFractionGrid.max, augusta::math::kFractionGrid);
  const SequencedCommandWire widest_command{
      .sequence = std::numeric_limits<augusta::tick::Sequence>::max(),
      .command = {.direction = Vec3(far_direction, far_direction, far_direction),
                  .yaw = far_angle,
                  .pitch = far_angle,
                  .seen_fraction = last_fraction,
                  .flags = CommandWire::kSprint | CommandWire::kAds | CommandWire::kFire | CommandWire::kReload,
                  .desired_stance = StanceWire::kProne,
                  .seen_age = std::numeric_limits<std::uint8_t>::max()}};
  PackHashWire pack{};
  pack.fill(std::byte{0xFF});
  const std::string version(kMaxEngineVersionLength, 'v');
  const std::string character(augusta::protocol::kMaxCharacterNameLength, 'c');
  const boost::uuids::uuid run = Session(0xFF);

  const std::vector<MessageWire> largest{
      JoinRequestWire{.engine_version = version, .client_pack = pack, .character = character, .run = run},
      CommandsWire{.commands = std::vector<SequencedCommandWire>(kMaxCommandsPerMessage, widest_command),
                   .seen_tick = std::numeric_limits<augusta::tick::Tick>::max()},
      ReadyWire{.version = std::numeric_limits<std::uint32_t>::max()},
      ReplayListRequestWire{},
      ReplayRequestWire{.engine_version = version,
                        .client_pack = pack,
                        .capture = std::string(augusta::protocol::kMaxCaptureNameLength, 'n'),
                        .run = run},
      ReenactRequestWire{.engine_version = version,
                         .client_pack = pack,
                         .character = character,
                         .spawn = Vec3(far, far, far),
                         .run = run},
  };

  for (const MessageWire& message : largest) {
    const auto encoded = Encode(message);
    ASSERT_TRUE(encoded.has_value()) << message.index();
    EXPECT_LE(encoded->size(), augusta::protocol::kMaxClientPayloadSize) << message.index();
  }
}

// Requirements: NFR-20
TEST(ProtocolTest, ANonFiniteNumberIsSentAsZeroOrItsNearestBound) {
  SequencedCommandWire sequenced = BusyCommand(1);
  sequenced.command.yaw = std::numeric_limits<float>::quiet_NaN();
  sequenced.command.direction.x = std::numeric_limits<float>::infinity();
  sequenced.command.direction.y = -std::numeric_limits<float>::infinity();

  const auto decoded = std::get<CommandsWire>(RoundTrip(CommandsWire{.commands = {sequenced}}));

  EXPECT_EQ(decoded.commands[0].command.yaw, 0.0F);
  EXPECT_EQ(decoded.commands[0].command.direction.x, SnapDirection(Vec3(1000.0F, 0.0F, 0.0F)).x);
  EXPECT_EQ(decoded.commands[0].command.direction.y, SnapDirection(Vec3(0.0F, -1000.0F, 0.0F)).y);
}

// The payload of a Commands message of one command whose fields are fields.
BytesWire OneCommand(const BytesWire& fields) {
  return Envelope(MessageTypeWire::kCommands, LengthField(1, Cat({VarintField(1, 1), fields})));
}

// The one command of a payload OneCommand made.
CommandWire CommandOf(const BytesWire& payload) {
  return std::get<CommandsWire>(Decode(payload).value()).commands.at(0).command;
}

// The angle grid's step, 2^-21 rad.
constexpr float kAngleStep = 1.0F / 2097152.0F;

// Requirements: NFR-20
TEST(ProtocolTest, YawAndPitchTravelAsCountsOfTwoToTheMinus21Radians) {
  const CommandWire command = CommandOf(OneCommand(Cat({SignedField(5, 1), SignedField(6, -1)})));

  EXPECT_EQ(command.yaw, kAngleStep);
  EXPECT_EQ(command.pitch, -kAngleStep);
}

// Requirements: NFR-20
TEST(ProtocolTest, AnAngleIsClampedWithinFourRadiansAndANaNIsZero) {
  EXPECT_EQ(SnapAngle(100.0F), 4.0F - kAngleStep);
  EXPECT_EQ(SnapAngle(-100.0F), -4.0F);
  EXPECT_EQ(SnapAngle(std::numeric_limits<float>::quiet_NaN()), 0.0F);
}

// Half a step: under 0.2 mm at 800 m, so the network adds no aim error a
// weapon's spread would notice.
// Requirements: NFR-20
TEST(ProtocolTest, AnAnglesRoundingErrorIsAtMostHalfAStep) {
  constexpr float kMaxError = kAngleStep / 2.0F;
  static_assert(kMaxError * 800.0F < 0.0002F);
  for (const float value : {0.1F, -0.1F, 1.0F / 3.0F, 1.2345678F, -2.7182817F, 3.1415927F, -3.9999F}) {
    EXPECT_LE(std::abs(SnapAngle(value) - value), kMaxError) << value;
  }
}

// The flags in the low four bits, the stance in the two above them (ADR-0038).
// Requirements: NFR-20
TEST(ProtocolTest, ACommandsFlagsAndStanceShareOneBitfield) {
  const CommandWire command = CommandOf(OneCommand(VarintField(7, 0b0010'1001)));

  EXPECT_EQ(command.flags, CommandWire::kSprint | CommandWire::kReload);
  EXPECT_EQ(command.desired_stance, StanceWire::kProne);
}

// What the shooter was shown (ADR-0044): the newest tick once for the whole
// message, and each command's own as how far before it, with its fraction in
// 256ths.
// Requirements: NFR-20
TEST(ProtocolTest, ACommandsSeenTimeTravelsAsItsAgeAndItsFractionAndTheMessagesSeenTick) {
  const BytesWire command = LengthField(1, Cat({VarintField(1, 1), VarintField(8, 3), VarintField(9, 192)}));

  const auto decoded = std::get<CommandsWire>(
      Decode(Envelope(MessageTypeWire::kCommands, Cat({command, VarintField(2, 1200)}))).value());

  EXPECT_EQ(decoded.seen_tick, 1200U);
  EXPECT_EQ(decoded.commands.at(0).command.seen_age, 3);
  EXPECT_EQ(decoded.commands.at(0).command.seen_fraction, 0.75F);
}

// Requirements: NFR-20
TEST(ProtocolTest, ASeenTimesFractionIsHeldBelowOneAndANaNIsZero) {
  EXPECT_EQ(SnapFraction(0.5F), 0.5F);
  EXPECT_EQ(SnapFraction(1.0F), 255.0F / 256.0F);
  EXPECT_EQ(SnapFraction(7.0F), 255.0F / 256.0F);
  EXPECT_EQ(SnapFraction(-1.0F), 0.0F);
  EXPECT_EQ(SnapFraction(std::numeric_limits<float>::quiet_NaN()), 0.0F);
}

// Requirements: NFR-12
TEST(ProtocolTest, ACommandsStanceOutsideItsRangeIsInvalid) {
  EXPECT_TRUE(Decode(OneCommand(VarintField(7, 0b0010'0000))).has_value());
  EXPECT_EQ(Decode(OneCommand(VarintField(7, 0b0011'0000))).error(), DecodeError::kInvalidEnum);
}

// Requirements: NFR-12
TEST(ProtocolTest, ACommandsBitsAboveItsFlagsAndStanceAreReserved) {
  for (const std::uint64_t bad : {0b0100'0000U, 0b1000'0000U, 0x8000'0000U}) {
    EXPECT_EQ(Decode(OneCommand(VarintField(7, bad))).error(), DecodeError::kReservedBits) << bad;
  }
}

// Requirements: NFR-12
TEST(ProtocolTest, ACommandsSeenAgeWiderThanAByteIsOutOfRange) {
  EXPECT_EQ(CommandOf(OneCommand(VarintField(8, 255))).seen_age, 255);
  EXPECT_EQ(Decode(OneCommand(VarintField(8, 256))).error(), DecodeError::kOutOfRange);
}

// A count one past its grid's bound is none a sender, clamping, ever writes.
// Requirements: NFR-12
TEST(ProtocolTest, ACommandsCountsPastTheirGridsAreOutOfRange) {
  constexpr std::int32_t kAngleMax = (1 << 23) - 1;
  EXPECT_TRUE(Decode(OneCommand(SignedField(5, kAngleMax))).has_value());
  EXPECT_EQ(Decode(OneCommand(SignedField(5, kAngleMax + 1))).error(), DecodeError::kOutOfRange);
  EXPECT_EQ(Decode(OneCommand(SignedField(6, -kAngleMax - 2))).error(), DecodeError::kOutOfRange);
  EXPECT_EQ(Decode(OneCommand(SignedField(2, 32768))).error(), DecodeError::kOutOfRange);
  EXPECT_EQ(Decode(OneCommand(VarintField(9, 256))).error(), DecodeError::kOutOfRange);
}

// Requirements: NFR-20
TEST(ProtocolTest, AuthoritativeStateRoundTrips) {
  AuthoritativeStateWire sent{.tick = 900, .bodies = {}, .acknowledged_sequence = 875};
  for (std::uint32_t i = 0; i < 3; ++i) {
    EntityStateWire body{.entity = static_cast<EntityIdWire>(10 + i)};
    body.body.position = Vec3(1.0F + static_cast<float>(i), 2.0F, -3.5F);
    body.body.velocity = Vec3(0.0F, -9.81F, 3.0F);
    body.body.stance = static_cast<augusta::protocol::StanceWire>(i);
    body.body.stamina = 0.25F * static_cast<float>(i);
    body.yaw = 1.25F - static_cast<float>(i);
    sent.bodies.push_back(body);
  }

  const auto decoded = RoundTrip(sent);

  ASSERT_TRUE(std::holds_alternative<AuthoritativeStateWire>(decoded));
  const auto& received = std::get<AuthoritativeStateWire>(decoded);
  EXPECT_EQ(received.tick, sent.tick);
  EXPECT_EQ(received.acknowledged_sequence, sent.acknowledged_sequence);
  ASSERT_EQ(received.bodies.size(), sent.bodies.size());
  for (std::size_t i = 0; i < sent.bodies.size(); ++i) {
    EXPECT_EQ(received.bodies[i].entity, sent.bodies[i].entity);
    ExpectSnappedBody(received.bodies[i].body, sent.bodies[i].body);
    EXPECT_EQ(received.bodies[i].yaw, sent.bodies[i].yaw);
  }
}

// The payload of an Authoritative State of one body whose fields are fields.
BytesWire OneBody(const BytesWire& fields) {
  return Envelope(MessageTypeWire::kAuthoritativeState, LengthField(2, Cat({VarintField(1, 1), fields})));
}

// The one body of a payload OneBody made.
EntityStateWire BodyOf(const BytesWire& payload) {
  return std::get<AuthoritativeStateWire>(Decode(payload).value()).bodies.at(0);
}

// Where a body faces travels on the grid a command's view does (ADR-0038).
// Requirements: NFR-20
TEST(ProtocolTest, ABodysYawTravelsAsACountOfTheAngleGrid) {
  EXPECT_EQ(BodyOf(OneBody(SignedField(10, 2097152))).yaw, 1.0F);
}

// Where a body's view looks up or down travels on the grid its yaw does
// (ADR-0038), over the grid's whole range.
// Requirements: NFR-12, NFR-20
TEST(ProtocolTest, ABodysPitchTravelsAsACountOfTheAngleGrid) {
  constexpr std::int32_t kAngleMax = (1 << 23) - 1;
  EXPECT_EQ(BodyOf(OneBody(SignedField(11, 2097152))).pitch, 1.0F);
  EXPECT_EQ(BodyOf(OneBody(SignedField(11, kAngleMax))).pitch, 4.0F - kAngleStep);
  EXPECT_EQ(BodyOf(OneBody(SignedField(11, -kAngleMax - 1))).pitch, -4.0F);
  EXPECT_EQ(Decode(OneBody(SignedField(11, kAngleMax + 1))).error(), DecodeError::kOutOfRange);
}

// A pitch past the angle grid's range travels as its bound.
// Requirements: NFR-20
TEST(ProtocolTest, ABodysPitchAndAdsRoundTripAtBothEndsOfTheRange) {
  AuthoritativeStateWire sent;
  EntityStateWire up = BodyAt(1, 1.0F);
  up.pitch = 1.5F;
  up.body.flags = BodyStateWire::kAds;
  EntityStateWire down = BodyAt(2, 2.0F);
  down.pitch = -1.5F;
  EntityStateWire beyond = BodyAt(3, 3.0F);
  beyond.pitch = 9.0F;
  beyond.body.flags = BodyStateWire::kExhausted | BodyStateWire::kAds;
  sent.bodies = {up, down, beyond};

  const auto received = std::get<AuthoritativeStateWire>(RoundTrip(sent));

  ASSERT_EQ(received.bodies.size(), 3U);
  EXPECT_EQ(received.bodies[0].pitch, SnapAngle(1.5F));
  EXPECT_EQ(received.bodies[0].body.flags, BodyStateWire::kAds);
  EXPECT_EQ(received.bodies[1].pitch, SnapAngle(-1.5F));
  EXPECT_EQ(received.bodies[1].body.flags, 0);
  EXPECT_EQ(received.bodies[2].pitch, 4.0F - kAngleStep);
  EXPECT_EQ(received.bodies[2].body.flags, BodyStateWire::kExhausted | BodyStateWire::kAds);
}

// A body's Stride travels as its one byte (ADR-0038, ADR-0066), over its whole range.
// Requirements: NFR-20
TEST(ProtocolTest, ABodysStrideRoundTripsAtBothEndsOfItsByte) {
  AuthoritativeStateWire sent;
  EntityStateWire first = BodyAt(1, 1.0F);
  first.body.stride = 0;
  EntityStateWire last = BodyAt(2, 2.0F);
  last.body.stride = 255;
  EntityStateWire between = BodyAt(3, 3.0F);
  between.body.stride = 77;
  sent.bodies = {first, last, between};

  const auto received = std::get<AuthoritativeStateWire>(RoundTrip(sent));

  ASSERT_EQ(received.bodies.size(), 3U);
  EXPECT_EQ(received.bodies[0].body.stride, 0);
  EXPECT_EQ(received.bodies[1].body.stride, 255);
  EXPECT_EQ(received.bodies[2].body.stride, 77);
}

// No Stride is beyond a byte, so a count beyond one is refused (ADR-0038).
// Requirements: NFR-12, NFR-20
TEST(ProtocolTest, ABodysStrideBeyondAByteIsRefused) {
  EXPECT_EQ(BodyOf(OneBody(VarintField(12, 255))).body.stride, 255);
  EXPECT_EQ(Decode(OneBody(VarintField(12, 256))).error(), DecodeError::kOutOfRange);
}

// A body's change of stance travels as its previous stance and one byte of
// progress (ADR-0038, ADR-0066), the progress over its whole range.
// Requirements: NFR-20
TEST(ProtocolTest, ABodysChangeOfStanceRoundTripsAtBothEndsOfItsProgress) {
  AuthoritativeStateWire sent;
  EntityStateWire started = BodyAt(1, 1.0F);
  started.body.stance = StanceWire::kCrouching;
  started.body.previous_stance = StanceWire::kStanding;
  started.body.stance_progress = 0;
  EntityStateWire over = BodyAt(2, 2.0F);
  over.body.previous_stance = StanceWire::kProne;
  over.body.stance_progress = 255;
  EntityStateWire between = BodyAt(3, 3.0F);
  between.body.stance = StanceWire::kProne;
  between.body.previous_stance = StanceWire::kCrouching;
  between.body.stance_progress = 77;
  sent.bodies = {started, over, between};

  const auto received = std::get<AuthoritativeStateWire>(RoundTrip(sent));

  ASSERT_EQ(received.bodies.size(), 3U);
  EXPECT_EQ(received.bodies[0].body, started.body);
  EXPECT_EQ(received.bodies[1].body, over.body);
  EXPECT_EQ(received.bodies[2].body, between.body);
}

// The progress travels as what is still to go of the change, so a body whose
// change is over sends none; no count is beyond a byte, so one beyond is
// refused (ADR-0038).
// Requirements: NFR-12, NFR-20
TEST(ProtocolTest, ABodysStanceProgressTravelsAsTheByteStillToGo) {
  EXPECT_EQ(BodyOf(OneBody({})).body.stance_progress, BodyStateWire::kStanceProgressOver);
  EXPECT_EQ(BodyOf(OneBody(VarintField(13, 255))).body.stance_progress, 0);
  EXPECT_EQ(BodyOf(OneBody(VarintField(13, 55))).body.stance_progress, 200);
  EXPECT_EQ(Decode(OneBody(VarintField(13, 256))).error(), DecodeError::kOutOfRange);
}

// Each Encode builds its message on an arena local to the call, whose first
// block is on the stack, so the payload is its one allocation (ADR-0007).
// Requirements: NFR-01
TEST(ProtocolTest, AnAuthoritativeStateOfAFullMatchIsEncodedInOneAllocation) {
  if (!AUGUSTA_COUNTS_ALLOCATIONS) {
    GTEST_SKIP() << "operator new is ThreadSanitizer's in this build, so allocations are not counted";
  }
  if (!AUGUSTA_VECTORS_ALLOCATE_ONLY_ELEMENTS) {
    GTEST_SKIP() << "MSVC's checked iterators allocate for every vector, so the payload is more than one allocation";
  }
  AuthoritativeStateWire state{.tick = 9, .bodies = {}, .health = 100.0F, .acknowledged_sequence = 41};
  for (std::uint32_t player = 1; player <= kMaxPlayers; ++player) {
    state.bodies.push_back(BodyAt(player, static_cast<float>(player)));
  }
  const MessageWire message = state;

  const std::size_t before = allocations;
  const auto payload = Encode(message);

  EXPECT_EQ(allocations - before, 1U);
  EXPECT_TRUE(payload.has_value());
}

// Requirements: NFR-06, NFR-20
TEST(ProtocolTest, AuthoritativeStateWithAFullMatchRoundTrips) {
  AuthoritativeStateWire sent;
  sent.bodies.resize(kMaxPlayers);

  EXPECT_EQ(std::get<AuthoritativeStateWire>(RoundTrip(sent)).bodies.size(), kMaxPlayers);
}

// Requirements: NFR-02, NFR-20
TEST(ProtocolTest, AnUpdateTellsItsRecipientHowManyOfItsCommandsTheServerHolds) {
  for (const std::uint8_t queued : {std::uint8_t{0}, std::uint8_t{2}, std::uint8_t{255}}) {
    const auto received = std::get<AuthoritativeStateWire>(
        RoundTrip(AuthoritativeStateWire{.tick = 9, .bodies = {BodyAt(1, 1.0F)}, .queued_commands = queued}));

    EXPECT_EQ(received.queued_commands, queued);
    EXPECT_EQ(received.tick, 9U);
    EXPECT_EQ(received.bodies.size(), 1U);
  }
}

// The server's tick counts from its start and never starts over (ADR-0038), so
// one past the last 32 bits hold travels whole in every message that carries one.
// Requirements: NFR-20
TEST(ProtocolTest, ATickPastThirtyTwoBitsRoundTripsInEveryMessageThatCarriesOne) {
  constexpr augusta::tick::Tick kTick = augusta::tick::Tick{std::numeric_limits<std::uint32_t>::max()} + 1;

  EXPECT_EQ(std::get<AuthoritativeStateWire>(RoundTrip(AuthoritativeStateWire{.tick = kTick, .bodies = {}})).tick,
            kTick);
  EXPECT_EQ(std::get<ShotWire>(RoundTrip(ShotWire{.tick = kTick})).tick, kTick);
  EXPECT_EQ(std::get<CommandsWire>(RoundTrip(CommandsWire{.commands = {}, .seen_tick = kTick})).seen_tick, kTick);
}

// The recipient's rifle is what it replays its commands from, so its times
// arrive as the floats they were, off any grid, and its Recoil offset, which is
// on the angle grid, as it was.
// Requirements: NFR-02, NFR-20
TEST(ProtocolTest, AnUpdateTellsItsRecipientItsOwnRifleExactly) {
  const WeaponStateWire rifle{.cooldown = 0.1F - (1.0F / 60.0F),
                              .reload_remaining = 2.4833333F,
                              .recoil_pitch = 0.046875F,
                              .recoil_yaw = -0.00390625F,
                              .rounds = 27,
                              .burst_index = 3};

  const auto received = std::get<AuthoritativeStateWire>(
      RoundTrip(AuthoritativeStateWire{.tick = 9, .bodies = {BodyAt(1, 1.0F)}, .rifle = rifle, .queued_commands = 1}));

  EXPECT_EQ(received.rifle, rifle);
  EXPECT_EQ(received.queued_commands, 1U);
}

// Requirements: NFR-20
TEST(ProtocolTest, AnUpdateTellsItsRecipientItsOwnHealthExactly) {
  for (const float health : {100.0F, 37.25F, 0.1F, 0.0F}) {
    const auto received = std::get<AuthoritativeStateWire>(
        RoundTrip(AuthoritativeStateWire{.tick = 9, .bodies = {BodyAt(1, 1.0F)}, .health = health}));

    EXPECT_EQ(received.health, health);
  }
}

// A tick's update encoded once and readdressed to each recipient is, byte for
// byte, what encoding it addressed to that recipient makes: its tick and bodies
// kept, only the recipient's own fields its own.
// Requirements: NFR-20
TEST(ProtocolTest, AReaddressedUpdateIsTheUpdateEncodedForItsRecipient) {
  const AuthoritativeStateWire first{.tick = 9,
                                     .bodies = {BodyAt(1, 1.0F), BodyAt(2, -3.5F)},
                                     .rifle = {.cooldown = 0.25F, .rounds = 30},
                                     .health = 100.0F,
                                     .acknowledged_sequence = 41,
                                     .queued_commands = 2};
  AuthoritativeStateWire second = first;
  second.rifle = {.cooldown = 0.0F, .reload_remaining = 1.5F, .recoil_pitch = 0.046875F, .rounds = 7, .burst_index = 2};
  second.health = 12.5F;
  second.acknowledged_sequence = 0x0102030405060708ULL;
  second.queued_commands = 0;

  BytesWire payload = Encode(first).value();
  ASSERT_TRUE(Readdress(payload, second).has_value());

  EXPECT_EQ(payload, Encode(second).value());
}

// A recipient whose own fields are all zero has none on the wire: the
// readdressed update is its shared fields alone.
// Requirements: NFR-20
TEST(ProtocolTest, AnUpdateReaddressedToARecipientWithNothingOfItsOwnIsItsSharedFieldsAlone) {
  const AuthoritativeStateWire first{.tick = 9,
                                     .bodies = {BodyAt(1, 1.0F), BodyAt(2, -3.5F)},
                                     .rifle = {.cooldown = 0.25F, .rounds = 30},
                                     .health = 100.0F,
                                     .acknowledged_sequence = 41,
                                     .queued_commands = 2};
  const AuthoritativeStateWire nothing_own{.tick = first.tick, .bodies = first.bodies};

  BytesWire payload = Encode(first).value();
  ASSERT_TRUE(Readdress(payload, nothing_own).has_value());

  EXPECT_EQ(payload, Encode(nothing_own).value());
  EXPECT_EQ(Decode(payload).value(), MessageWire(nothing_own));
}

// Only the payload of an Authoritative State, framed as Encode frames it, has
// shared fields Readdress can keep: any other is the caller's broken invariant
// (ADR-0033), refused with the payload left as it was.
// Requirements: NFR-20
TEST(ProtocolTest, AReaddressOfAnythingButAnUpdateIsRefused) {
  const AuthoritativeStateWire recipient{.tick = 9, .bodies = {}, .health = 1.0F, .acknowledged_sequence = 3};
  const BytesWire update = Encode(AuthoritativeStateWire{.tick = 9, .bodies = {BodyAt(1, 1.0F)}}).value();
  const BytesWire shorter(update.begin(), update.end() - 1);
  BytesWire longer = update;
  longer.push_back(std::byte{0});
  const BytesWire shot = Encode(ShotWire{.tick = 9, .shooter = static_cast<EntityIdWire>(1)}).value();

  for (BytesWire payload : {BytesWire{}, BytesWire{update.front()}, shorter, longer, shot}) {
    const BytesWire before = payload;
    EXPECT_EQ(Readdress(payload, recipient).error(), EncodeError::kNotAnAuthoritativeState) << payload.size();
    EXPECT_EQ(payload, before);
  }
}

// Its Recoil offset travels as counts of the angle grid's step.
// Requirements: NFR-20
TEST(ProtocolTest, ARecipientsRecoilOffsetTravelsAsCountsOfTheAngleGrid) {
  const BytesWire own = Cat({SignedField(9, 32768), SignedField(10, -1)});

  const auto received =
      std::get<AuthoritativeStateWire>(Decode(Envelope(MessageTypeWire::kAuthoritativeState, own)).value());

  EXPECT_EQ(received.rifle.recoil_pitch, 1.0F / 64.0F);
  EXPECT_EQ(received.rifle.recoil_yaw, -kAngleStep);
}

// Requirements: NFR-12
TEST(ProtocolTest, MorePlayersThanAMatchHoldsIsTooLong) {
  const BytesWire body = LengthField(2, VarintField(1, 1));

  EXPECT_TRUE(Decode(Envelope(MessageTypeWire::kAuthoritativeState, Repeat(body, kMaxPlayers))).has_value());
  EXPECT_EQ(Decode(Envelope(MessageTypeWire::kAuthoritativeState, Repeat(body, kMaxPlayers + 1))).error(),
            DecodeError::kFieldTooLong);
}

// Requirements: NFR-12
TEST(ProtocolTest, APlayersStanceOutsideItsRangeIsInvalid) {
  EXPECT_TRUE(Decode(OneBody(VarintField(8, 2))).has_value());
  EXPECT_EQ(Decode(OneBody(VarintField(8, 3))).error(), DecodeError::kInvalidEnum);
}

// The one-byte counts an update carries for its recipient, carried in a wider
// varint, are none a sender writes.
// Requirements: NFR-12
TEST(ProtocolTest, AnUpdatesOneByteCountsWiderThanAByteAreOutOfRange) {
  for (const int field : {4, 5, 8}) {
    EXPECT_TRUE(Decode(Envelope(MessageTypeWire::kAuthoritativeState, VarintField(field, 255))).has_value()) << field;
    EXPECT_EQ(Decode(Envelope(MessageTypeWire::kAuthoritativeState, VarintField(field, 256))).error(),
              DecodeError::kOutOfRange)
        << field;
  }
}

// A count one past its grid's bound is none a sender, clamping, ever writes.
// Requirements: NFR-12
TEST(ProtocolTest, ABodysCountsPastTheirGridsAreOutOfRange) {
  constexpr std::int32_t kPositionMax = (1 << 23) - 1;
  EXPECT_TRUE(Decode(OneBody(SignedField(2, kPositionMax))).has_value());
  EXPECT_EQ(Decode(OneBody(SignedField(2, kPositionMax + 1))).error(), DecodeError::kOutOfRange);
  EXPECT_EQ(Decode(OneBody(SignedField(4, -kPositionMax - 2))).error(), DecodeError::kOutOfRange);
  EXPECT_EQ(Decode(OneBody(SignedField(5, 32768))).error(), DecodeError::kOutOfRange);
  EXPECT_TRUE(Decode(OneBody(VarintField(9, 65535))).has_value());
  EXPECT_EQ(Decode(OneBody(VarintField(9, 65536))).error(), DecodeError::kOutOfRange);
}

// Requirements: NFR-20
TEST(ProtocolTest, AnExhaustedBodyRoundTripsInEveryStance) {
  AuthoritativeStateWire sent;
  for (std::uint8_t stance = 0; stance < 3; ++stance) {
    EntityStateWire body = BodyAt(stance, 1.0F);
    body.body.stance = static_cast<augusta::protocol::StanceWire>(stance);
    body.body.flags = BodyStateWire::kExhausted;
    sent.bodies.push_back(body);
  }

  const auto received = std::get<AuthoritativeStateWire>(RoundTrip(sent));

  ASSERT_EQ(received.bodies.size(), sent.bodies.size());
  for (std::size_t i = 0; i < sent.bodies.size(); ++i) {
    ExpectSnappedBody(received.bodies[i].body, sent.bodies[i].body);
  }
}

// The stance in the low two bits, the exhausted flag in the one above them
// (ADR-0038).
// Requirements: NFR-20
TEST(ProtocolTest, ABodysExhaustedFlagSharesTheStanceBitfield) {
  const EntityStateWire exhausted = BodyOf(OneBody(VarintField(8, 0b110)));
  const EntityStateWire rested = BodyOf(OneBody(VarintField(8, 0b010)));

  EXPECT_EQ(exhausted.body.stance, StanceWire::kProne);
  EXPECT_EQ(exhausted.body.flags, BodyStateWire::kExhausted);
  EXPECT_EQ(rested.body.stance, StanceWire::kProne);
  EXPECT_EQ(rested.body.flags, 0);
}

// ADS in the bit above the exhausted flag (ADR-0038).
// Requirements: NFR-20
TEST(ProtocolTest, ABodysAdsFlagIsTheBitAboveItsExhaustedFlag) {
  const EntityStateWire aiming = BodyOf(OneBody(VarintField(8, 0b1001)));
  const EntityStateWire both = BodyOf(OneBody(VarintField(8, 0b1100)));

  EXPECT_EQ(aiming.body.stance, StanceWire::kCrouching);
  EXPECT_EQ(aiming.body.flags, BodyStateWire::kAds);
  EXPECT_EQ(both.body.stance, StanceWire::kStanding);
  EXPECT_EQ(both.body.flags, BodyStateWire::kExhausted | BodyStateWire::kAds);
}

// The grounded flag in the bit above ADS (ADR-0038, ADR-0062).
// Requirements: NFR-20
TEST(ProtocolTest, ABodysGroundedFlagIsTheBitAboveItsAdsFlag) {
  const EntityStateWire grounded = BodyOf(OneBody(VarintField(8, 0b10010)));
  const EntityStateWire all = BodyOf(OneBody(VarintField(8, 0b11100)));

  EXPECT_EQ(grounded.body.stance, StanceWire::kProne);
  EXPECT_EQ(grounded.body.flags, BodyStateWire::kGrounded);
  EXPECT_EQ(all.body.stance, StanceWire::kStanding);
  EXPECT_EQ(all.body.flags, BodyStateWire::kExhausted | BodyStateWire::kAds | BodyStateWire::kGrounded);
}

// The previous stance in bits 5-6, numbered as the stance is (ADR-0038).
// Requirements: NFR-12, NFR-20
TEST(ProtocolTest, ABodysPreviousStanceIsBitsFiveAndSix) {
  const EntityStateWire from_prone = BodyOf(OneBody(VarintField(8, 0b100'0001)));
  const EntityStateWire from_crouching = BodyOf(OneBody(VarintField(8, 0b010'0000)));

  EXPECT_EQ(from_prone.body.stance, StanceWire::kCrouching);
  EXPECT_EQ(from_prone.body.previous_stance, StanceWire::kProne);
  EXPECT_EQ(from_crouching.body.stance, StanceWire::kStanding);
  EXPECT_EQ(from_crouching.body.previous_stance, StanceWire::kCrouching);
  EXPECT_EQ(Decode(OneBody(VarintField(8, 0b110'0000))).error(), DecodeError::kInvalidEnum);
}

// Every bit above the previous stance.
// Requirements: NFR-12
TEST(ProtocolTest, ABodysBitsAboveItsStancesAndFlagsAreReserved) {
  for (unsigned bit = 7; bit < 32; ++bit) {
    EXPECT_EQ(Decode(OneBody(VarintField(8, 1ULL << bit))).error(), DecodeError::kReservedBits) << bit;
  }
}

// Requirements: NFR-20
TEST(ProtocolTest, APositionTravelsAsCountsOfMillimeterSteps) {
  const EntityStateWire body = BodyOf(OneBody(Cat({SignedField(2, 1024), SignedField(3, -1)})));

  EXPECT_EQ(body.body.position, Vec3(1.0F, -1.0F / 1024.0F, 0.0F));
}

// Requirements: NFR-20
TEST(ProtocolTest, ASnappedValueIsOnItsGridAndSnapsToItself) {
  for (const float value : {0.0F, 0.3F, -0.3F, 1.0F / 3.0F, 12.345F, -7.77F, 1000.001F}) {
    const Vec3 vector(value, -value, value * 0.5F);
    EXPECT_EQ(SnapPosition(SnapPosition(vector)), SnapPosition(vector)) << value;
    EXPECT_EQ(SnapVelocity(SnapVelocity(vector)), SnapVelocity(vector)) << value;
    EXPECT_EQ(SnapDirection(SnapDirection(vector)), SnapDirection(vector)) << value;
    EXPECT_EQ(SnapAngle(SnapAngle(value)), SnapAngle(value)) << value;
    EXPECT_EQ(SnapStamina(SnapStamina(value)), SnapStamina(value)) << value;
    EXPECT_NEAR(SnapPosition(vector).x, vector.x, 0.5F / 1024.0F) << value;
  }
}

// Requirements: NFR-20
TEST(ProtocolTest, AValueBeyondItsRangeIsSentAsTheBound) {
  EXPECT_EQ(SnapPosition(Vec3(1.0e6F, -1.0e6F, 0.0F)), Vec3(8192.0F - (1.0F / 1024.0F), -8192.0F, 0.0F));
  EXPECT_EQ(SnapVelocity(Vec3(100.0F, -100.0F, 0.0F)), Vec3(64.0F - (1.0F / 512.0F), -64.0F, 0.0F));
  EXPECT_EQ(SnapStamina(-0.5F), 0.0F);
}

// Requirements: NFR-12
TEST(ProtocolTest, DecodingAndEncodingAgainGivesTheSameBytes) {
  const AuthoritativeStateWire state{.tick = 1, .bodies = {BodyAt(1, 3.14159F)}, .acknowledged_sequence = 1};
  const BytesWire first = Encode(state).value();
  EXPECT_EQ(Encode(std::get<AuthoritativeStateWire>(Decode(first).value())).value(), first);

  const BytesWire commands = Encode(CommandsWire{.commands = {BusyCommand(1)}}).value();
  EXPECT_EQ(Encode(std::get<CommandsWire>(Decode(commands).value())).value(), commands);
}

// A message has one meaning, not one encoding (ADR-0007): a field this build
// does not know is ignored, at every level.
// Requirements: NFR-12
TEST(ProtocolTest, AnUnknownFieldAtTheTopLevelOrInsideABodyDecodesToTheSameMessage) {
  const BytesWire ready = Envelope(MessageTypeWire::kReady, VarintField(1, 7));

  EXPECT_EQ(Decode(Cat({ready, VarintField(99, 5)})).value(), MessageWire(ReadyWire{.version = 7}));
  EXPECT_EQ(Decode(Cat({TextField(99, "later"), ready})).value(), MessageWire(ReadyWire{.version = 7}));
  EXPECT_EQ(Decode(Envelope(MessageTypeWire::kReady, Cat({VarintField(1, 7), VarintField(9, 1)}))).value(),
            MessageWire(ReadyWire{.version = 7}));
  EXPECT_EQ(BodyOf(OneBody(Cat({SignedField(10, 2097152), LengthField(42, VarintField(1, 1))}))).yaw, 1.0F);
}

// An absent field or sub-message reads as its default (ADR-0007).
// Requirements: NFR-12
TEST(ProtocolTest, AnAbsentSubMessageDecodesToItsDefault) {
  EXPECT_EQ(Decode(Envelope(MessageTypeWire::kJoinAccepted, LengthField(5, BytesOf(Session(7))))).value(),
            MessageWire(JoinAcceptedWire{
                .session = Session(7), .parameters = ParametersWire{.player_count = 0}, .character = ""}));
  EXPECT_EQ(Decode(Envelope(MessageTypeWire::kReady, {})).value(), MessageWire(ReadyWire{}));
}

// A later build's Join request decodes, to be refused for its engine version,
// not dropped as a payload that does not decode (ADR-0038).
// Requirements: NFR-12
TEST(ProtocolTest, AJoinRequestWithAFieldThisBuildDoesNotKnowDecodes) {
  const BytesWire request = Cat({TextField(1, "9.0.0"), TextField(3, "soldier"), TextField(5, "a later field")});

  EXPECT_EQ(Decode(Envelope(MessageTypeWire::kJoinRequest, request)).value(),
            MessageWire(JoinRequestWire{.engine_version = "9.0.0", .character = "soldier"}));
}

// A pack hash is a hash's 32 bytes, or none at all, which reads as the all-zero
// one as an absent field does.
// Requirements: NFR-12
TEST(ProtocolTest, APackHashOfAnyOtherLengthIsOutOfRange) {
  const auto hash_of = [](std::size_t length) {
    return Envelope(MessageTypeWire::kJoinRequest, LengthField(2, BytesWire(length, std::byte{1})));
  };

  EXPECT_TRUE(Decode(hash_of(augusta::protocol::kPackHashSize)).has_value());
  EXPECT_EQ(std::get<JoinRequestWire>(Decode(hash_of(0)).value()).client_pack, PackHashWire{});
  EXPECT_EQ(Decode(hash_of(augusta::protocol::kPackHashSize - 1)).error(), DecodeError::kOutOfRange);
  EXPECT_EQ(Decode(hash_of(augusta::protocol::kPackHashSize + 1)).error(), DecodeError::kOutOfRange);
}

// Requirements: NFR-12
TEST(ProtocolTest, AJoinAcceptedsOneByteNumbersWiderThanAByteAreOutOfRange) {
  EXPECT_EQ(Decode(Envelope(MessageTypeWire::kJoinAccepted, VarintField(2, 256))).error(), DecodeError::kOutOfRange);
  EXPECT_EQ(Decode(Envelope(MessageTypeWire::kJoinAccepted, LengthField(3, VarintField(5, 256)))).error(),
            DecodeError::kOutOfRange);
}

// Requirements: NFR-12
TEST(ProtocolTest, AStringThatIsNotUtf8IsMalformed) {
  const BytesWire not_utf8{std::byte{0xC3}, std::byte{0x28}};
  const BytesWire utf8{std::byte{0xC3}, std::byte{0xA9}};
  const BytesWire listing = LengthField(1, LengthField(5, not_utf8));

  EXPECT_TRUE(Decode(Envelope(MessageTypeWire::kJoinRequest, LengthField(1, utf8))).has_value());
  EXPECT_EQ(Decode(Envelope(MessageTypeWire::kJoinRequest, LengthField(1, not_utf8))).error(), DecodeError::kMalformed);
  EXPECT_EQ(Decode(Envelope(MessageTypeWire::kJoinRequest, LengthField(3, not_utf8))).error(), DecodeError::kMalformed);
  EXPECT_EQ(Decode(Envelope(MessageTypeWire::kReplayList, listing)).error(), DecodeError::kMalformed);
  EXPECT_EQ(DecodeCaptureRecord(LengthField(1001, LengthField(3, not_utf8))).error(), DecodeError::kMalformed);
}

// The codec takes its counters' widths from augusta::tick, as the engine does,
// so the wire and the engine cannot drift apart.
// Requirements: NFR-20
TEST(ProtocolTickTest, TheCodecsCountersAreTheTicks) {
  static_assert(std::same_as<decltype(AuthoritativeStateWire::tick), augusta::tick::Tick>);
  static_assert(std::same_as<decltype(CommandsWire::seen_tick), augusta::tick::Tick>);
  static_assert(std::same_as<decltype(ShotWire::tick), augusta::tick::Tick>);
  static_assert(std::same_as<decltype(SequencedCommandWire::sequence), augusta::tick::Sequence>);
  static_assert(std::same_as<decltype(AuthoritativeStateWire::acknowledged_sequence), augusta::tick::Sequence>);
}

// A client repeats at least one unacknowledged command per message.
// Requirements: NFR-02
TEST(ProtocolTest, ACommandsMessageCarriesAtLeastOneCommand) { EXPECT_GE(kMaxCommandsPerMessage, 1U); }

// Requirements: NFR-12
TEST(ProtocolTest, EveryErrorHasADescription) {
  for (const DecodeError error : {DecodeError::kMalformed, DecodeError::kUnknownType, DecodeError::kInvalidEnum,
                                  DecodeError::kReservedBits, DecodeError::kOutOfRange, DecodeError::kFieldTooLong}) {
    EXPECT_FALSE(augusta::protocol::DescribeDecodeError(error).empty());
  }
}

// Encode refuses a message that breaks one of the protocol's limits, in every
// build, rather than relying on an assertion a Release build compiles out: it
// gives back no payload at all, so no peer is ever sent a cut-off or
// misframed one (ADR-0033).

// The error Encode gives message, which must be one it refuses.
EncodeError RefusalOf(const MessageWire& message) {
  const auto encoded = Encode(message);
  EXPECT_FALSE(encoded.has_value());
  return encoded.has_value() ? EncodeError{} : encoded.error();
}

// Requirements: NFR-20
TEST(ProtocolEncodeTest, AnEngineVersionLongerThanAllowedIsNotEncoded) {
  const JoinRequestWire request{.engine_version = std::string(kMaxEngineVersionLength + 1, 'v'), .character = "c"};

  EXPECT_EQ(RefusalOf(request), EncodeError::kFieldTooLong);
}

// Requirements: NFR-20
TEST(ProtocolEncodeTest, ACharacterLongerThanAllowedIsNotEncodedInAnyMessageThatCarriesOne) {
  const std::string too_long(augusta::protocol::kMaxCharacterNameLength + 1, 'c');

  EXPECT_EQ(RefusalOf(JoinRequestWire{.engine_version = "0.1.0", .character = too_long}), EncodeError::kFieldTooLong);
  EXPECT_EQ(RefusalOf(JoinAcceptedWire{.character = too_long}), EncodeError::kFieldTooLong);
  EXPECT_EQ(RefusalOf(LobbyWire{.version = 1, .roster = {RosterEntryWire{.character = too_long}}}),
            EncodeError::kFieldTooLong);
  EXPECT_EQ(RefusalOf(MatchStartWire{.players = {MatchPlayer(1, too_long, 0.0F)}}), EncodeError::kFieldTooLong);
}

// Requirements: NFR-20
TEST(ProtocolEncodeTest, TextThatIsNotUtf8IsNotEncoded) {
  const std::string not_utf8 = "\xC3\x28";
  ReplayListingWire listing;
  listing.characters = {not_utf8};
  CaptureHeaderWire header;
  header.engine_version = not_utf8;
  const ReplayListWire listed{.replays = {listing}};

  EXPECT_TRUE(Encode(JoinRequestWire{.engine_version = "0.1.0", .character = "s\xC3\xA9rgio"}).has_value());
  EXPECT_EQ(RefusalOf(JoinRequestWire{.engine_version = not_utf8, .character = "c"}), EncodeError::kNotUtf8);
  EXPECT_EQ(RefusalOf(JoinRequestWire{.engine_version = "0.1.0", .character = not_utf8}), EncodeError::kNotUtf8);
  EXPECT_EQ(RefusalOf(listed), EncodeError::kNotUtf8);
  EXPECT_EQ(EncodeCaptureRecord(header).error(), EncodeError::kNotUtf8);
}

// Requirements: NFR-20
TEST(ProtocolEncodeTest, AListLongerThanItsMessageAllowsIsNotEncoded) {
  RifleWire rifle;
  rifle.recoil_pattern.resize(augusta::limits::kMaxRecoilKicks + 1);
  ParametersWire parameters;
  parameters.rifle = rifle;

  EXPECT_EQ(RefusalOf(CommandsWire{.commands = std::vector<SequencedCommandWire>(kMaxCommandsPerMessage + 1)}),
            EncodeError::kFieldTooLong);
  EXPECT_EQ(RefusalOf(AuthoritativeStateWire{.bodies = std::vector<EntityStateWire>(kMaxPlayers + 1)}),
            EncodeError::kFieldTooLong);
  EXPECT_EQ(RefusalOf(LobbyWire{.version = 1, .roster = std::vector<RosterEntryWire>(kMaxPlayers + 1)}),
            EncodeError::kFieldTooLong);
  EXPECT_EQ(RefusalOf(MatchStartWire{.players = std::vector<MatchPlayerWire>(kMaxPlayers + 1)}),
            EncodeError::kFieldTooLong);
  EXPECT_EQ(RefusalOf(JoinAcceptedWire{.parameters = parameters, .character = {}}), EncodeError::kFieldTooLong);
}

// Requirements: NFR-20
TEST(ProtocolEncodeTest, AFlagBitTheFieldDoesNotHaveIsNotEncoded) {
  CommandWire command;
  command.flags = CommandWire::kReload << 1U;
  EntityStateWire body;
  body.body.flags = BodyStateWire::kGrounded << 1U;

  EXPECT_EQ(RefusalOf(CommandsWire{.commands = {SequencedCommandWire{.sequence = 1, .command = command}}}),
            EncodeError::kReservedBits);
  EXPECT_EQ(RefusalOf(AuthoritativeStateWire{.bodies = {body}}), EncodeError::kReservedBits);
}

// Requirements: NFR-20
TEST(ProtocolEncodeTest, AnEnumeratedValueTheEnumerationLacksIsNotEncoded) {
  constexpr auto kNoStance = static_cast<StanceWire>(3);
  constexpr auto kNoPart = static_cast<BodyPartWire>(4);
  CommandWire command;
  command.desired_stance = kNoStance;
  EntityStateWire body;
  body.body.stance = kNoStance;

  EXPECT_EQ(RefusalOf(CommandsWire{.commands = {SequencedCommandWire{.sequence = 1, .command = command}}}),
            EncodeError::kInvalidEnum);
  EXPECT_EQ(RefusalOf(AuthoritativeStateWire{.bodies = {body}}), EncodeError::kInvalidEnum);
  EXPECT_EQ(RefusalOf(JoinRefusedWire{}), EncodeError::kInvalidEnum);
  EXPECT_EQ(RefusalOf(JoinRefusedWire{.reason = static_cast<JoinRefusalWire>(10)}), EncodeError::kInvalidEnum);
  EXPECT_EQ(RefusalOf(HitConfirmationWire{.part = static_cast<BodyPartWire>(0)}), EncodeError::kInvalidEnum);
  EXPECT_EQ(RefusalOf(HitConfirmationWire{.part = kNoPart}), EncodeError::kInvalidEnum);
  EXPECT_EQ(RefusalOf(DeathWire{.part = kNoPart}), EncodeError::kInvalidEnum);
}

// The number of the field payload's first tag names.
int FirstFieldOf(const BytesWire& payload) {
  std::uint64_t tag = 0;
  for (std::size_t i = 0; i < payload.size(); ++i) {
    const auto byte = std::to_integer<std::uint64_t>(payload[i]);
    tag |= (byte & 0x7FU) << (7U * i);
    if ((byte & 0x80U) == 0) {
      break;
    }
  }
  return static_cast<int>(tag >> 3U);
}

// Requirements: NFR-20
TEST(ProtocolEncodeTest, EveryMessageIsTheEnvelopeMemberItsTypeNumbers) {
  for (const MessageWire& message : EveryMessage()) {
    EXPECT_EQ(static_cast<int>(augusta::protocol::TypeOf(message)), FirstFieldOf(Encode(message).value()));
  }
}

// Requirements: NFR-20
TEST(ProtocolEncodeTest, AnEncodedMessageIsOfTheTypeItsEnvelopeMemberNames) {
  for (const MessageWire& message : EveryMessage()) {
    EXPECT_EQ(augusta::protocol::TypeOf(Encode(message).value()), augusta::protocol::TypeOf(message));
  }
  BytesWire state =
      Encode(AuthoritativeStateWire{.tick = 1, .bodies = {BodyAt(1, 0.5F)}, .health = 1.0F, .acknowledged_sequence = 0})
          .value();
  ASSERT_TRUE(
      Readdress(state, AuthoritativeStateWire{.tick = 1, .bodies = {}, .health = 1.0F, .acknowledged_sequence = 3})
          .has_value());
  EXPECT_EQ(augusta::protocol::TypeOf(state), MessageTypeWire::kAuthoritativeState);
}

// Requirements: NFR-20
TEST(ProtocolEncodeTest, EveryEncodeErrorHasADescription) {
  for (const EncodeError error : {EncodeError::kFieldTooLong, EncodeError::kReservedBits, EncodeError::kInvalidEnum,
                                  EncodeError::kNotAnAuthoritativeState, EncodeError::kNotUtf8}) {
    EXPECT_FALSE(augusta::protocol::DescribeEncodeError(error).empty());
  }
}

}  // namespace
