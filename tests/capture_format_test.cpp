#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <ios>
#include <istream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <streambuf>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <boost/uuid/uuid.hpp>
#include <gtest/gtest.h>

#include "augusta/assets.h"
#include "augusta/capture.h"
#include "augusta/capture_records.h"
#include "augusta/command.h"
#include "augusta/failure.h"
#include "augusta/limits.h"
#include "augusta/physics.h"
#include "augusta/protocol.h"
#include "frame.h"

// A Match capture's file (ADR-0050) through the capture module's interface:
// captures written with its writer and read back, files it refuses, and what
// it will not encode. A malformed file is built from the module's own encoded
// pieces, written out of order, or from a valid capture's bytes, changed.
namespace {

using augusta::capture::AtOffset;
using augusta::capture::Capture;
using augusta::capture::Death;
using augusta::capture::DescribeReadError;
using augusta::capture::Encoded;
using augusta::capture::EncodeError;
using augusta::capture::EncodeEvent;
using augusta::capture::EncodeHeader;
using augusta::capture::EncodeJoin;
using augusta::capture::Event;
using augusta::capture::Header;
using augusta::capture::Join;
using augusta::capture::Leave;
using augusta::capture::MatchEnd;
using augusta::capture::ReadCapture;
using augusta::capture::ReadError;
using augusta::capture::ReadErrorCode;
using augusta::capture::RecordTooLong;
using augusta::capture::StartsWithMagic;
using augusta::capture::Write;
using augusta::capture::WriteCapture;

// A Session ID, a UUIDv7, told apart by its last byte.
constexpr boost::uuids::uuid Session(std::uint8_t last) {
  return boost::uuids::uuid(
      {0x01, 0x90, 0xA1, 0xB2, 0xC3, 0xD4, 0x70, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, last});
}

// The captured Match's ID, a UUIDv7.
constexpr boost::uuids::uuid kMatch = boost::uuids::uuid(
    {0x01, 0x90, 0xA1, 0xB2, 0xC3, 0xD5, 0x7A, 0xBC, 0x8D, 0xEF, 0x01, 0x23, 0x45, 0x67, 0x89, 0xAB});

constexpr std::chrono::sys_time<std::chrono::milliseconds> kStarted{std::chrono::milliseconds{1'791'000'000'123}};

Header TheHeader() {
  Header header{.match = kMatch, .engine_version = "0.4.0", .tick_rate_hz = 60, .started = kStarted};
  header.server_pack.fill(std::byte{0x11});
  header.client_pack.fill(std::byte{0x22});
  return header;
}

std::vector<Join> TwoJoins() {
  return {Join{.session = Session(7), .character = "soldier", .spawn = {1.0F, 0.0F, -2.5F}},
          Join{.session = Session(9), .character = "scout", .spawn = {-4.0F, 0.25F, 3.0F}}};
}

// A Command whose every field survives the protocol's grids exactly.
augusta::command::Command Walk() {
  augusta::command::Command command;
  command.movement.direction = {0.0F, 0.0F, -1.0F};
  command.movement.sprint = true;
  command.movement.desired_stance = augusta::physics::Stance::kCrouching;
  command.yaw = 0.5F;
  command.pitch = -0.25F;
  command.ads = true;
  command.fire = true;
  command.reload = false;
  command.seen_fraction = 0.5F;
  return command;
}

AtOffset<Event> At(std::uint32_t offset, Event event) { return {.offset = offset, .event = std::move(event)}; }

// Two players, every event kind, ended by end.
Capture TwoPlayers(MatchEnd end) {
  return Capture{.header = TheHeader(),
                 .joins = TwoJoins(),
                 .events = {At(0, augusta::capture::Command{.player = 1, .seen_offset = -2, .command = Walk()}),
                            At(1, augusta::capture::Command{.player = 2, .seen_offset = 0, .command = Walk()}),
                            At(3, Death{.victim = 2, .killer = 1}), At(3, Leave{.player = 2}), At(5, end)},
                 .torn = false};
}

std::string Bytes(const Capture& capture) {
  std::ostringstream out(std::ios::binary);
  EXPECT_TRUE(WriteCapture(out, capture).has_value());
  return out.str();
}

std::string Bytes(const std::vector<Encoded>& pieces) {
  std::ostringstream out(std::ios::binary);
  for (const Encoded& piece : pieces) {
    Write(out, piece);
  }
  return out.str();
}

std::expected<Capture, ReadError> Read(const std::string& bytes) {
  std::istringstream in(bytes, std::ios::binary);
  return ReadCapture(in);
}

// A stream that holds bytes, then fails as a disk would where they end.
class FailingBuffer : public std::streambuf {
 public:
  explicit FailingBuffer(std::string bytes) : bytes_(std::move(bytes)) {
    setg(bytes_.data(), bytes_.data(), bytes_.data() + bytes_.size());
  }

 protected:
  int_type underflow() override { throw std::runtime_error("the disk failed"); }

 private:
  std::string bytes_;
};

// Requirements: US-21
TEST(CaptureFormatTest, ACaptureWrittenWholeReadsBackEqual) {
  for (const MatchEnd& end : {MatchEnd{.winner = 1}, MatchEnd{.winner = std::nullopt}}) {
    const Capture written = TwoPlayers(end);
    EXPECT_EQ(Read(Bytes(written)), written);
  }
}

// A Match end with no winner is a Draw, written and read as one.
// Requirements: US-21
TEST(CaptureFormatTest, AMatchEndWithoutAWinnerReadsBackAsADraw) {
  const auto read = Read(Bytes(TwoPlayers(MatchEnd{.winner = std::nullopt})));
  ASSERT_TRUE(read.has_value());
  EXPECT_EQ(std::get<MatchEnd>(read->events.back().event).winner, std::nullopt);
}

// Player n's Join is at index n - 1.
// Requirements: US-21
TEST(CaptureFormatTest, EachPlayersJoinIsAtItsNumberLessOne) {
  const auto read = Read(Bytes(TwoPlayers(MatchEnd{})));
  ASSERT_TRUE(read.has_value());
  ASSERT_EQ(read->joins.size(), 2U);
  EXPECT_EQ(read->joins[0].session, Session(7));
  EXPECT_EQ(read->joins[1].session, Session(9));
}

// A capture's header names its Match by the Match ID.
// Requirements: US-21
TEST(CaptureFormatTest, ACapturesHeaderReadsBackWithItsMatchId) {
  const auto read = Read(Bytes(TwoPlayers(MatchEnd{})));
  ASSERT_TRUE(read.has_value());
  EXPECT_EQ(read->header.match, kMatch);
}

// A capture with no events, stopped before its Match end, reads as it was.
// Requirements: US-21
TEST(CaptureFormatTest, ACaptureStoppedBeforeItsMatchEndReadsBackEqual) {
  const Capture written{.header = TheHeader(), .joins = TwoJoins(), .events = {}, .torn = false};
  EXPECT_EQ(Read(Bytes(written)), written);
}

// Requirements: NFR-12
TEST(CaptureFormatTest, AFileWithoutTheMagicIsNoCapture) {
  std::string bytes = Bytes(TwoPlayers(MatchEnd{}));
  bytes[0] = 'P';
  EXPECT_EQ(Read(bytes).error().code, ReadErrorCode::kNotACapture);
  EXPECT_EQ(Read("AUG").error().code, ReadErrorCode::kNotACapture);
  EXPECT_EQ(Read("").error().code, ReadErrorCode::kNotACapture);
}

// A file is the magic, then its format version as one raw byte, then its
// records, the header first.
// Requirements: US-21
TEST(CaptureFormatTest, AFileStartsWithTheMagicThenItsFormatVersion) {
  const std::string bytes = Bytes(TwoPlayers(MatchEnd{}));
  const std::string magic(reinterpret_cast<const char*>(augusta::protocol::kCaptureMagic.data()),
                          augusta::protocol::kCaptureMagic.size());
  ASSERT_GT(bytes.size(), magic.size());
  EXPECT_EQ(bytes.substr(0, magic.size()), magic);
  EXPECT_EQ(static_cast<std::uint8_t>(bytes[magic.size()]), 4);
}

// Another format version is refused by name, so whoever runs the process is
// told which version the file is.
// Requirements: NFR-12
TEST(CaptureFormatTest, AnotherFormatVersionIsRefusedNamingIt) {
  for (const std::uint8_t version : {0, 2, 3, 5, 255}) {
    std::string bytes = Bytes(TwoPlayers(MatchEnd{}));
    bytes[augusta::protocol::kCaptureMagic.size()] = static_cast<char>(version);
    const ReadError error = Read(bytes).error();
    EXPECT_EQ(error, (ReadError{.code = ReadErrorCode::kUnsupported, .format_version = version}));
    EXPECT_TRUE(DescribeReadError(error).contains(std::format("is of format version {};", version)))
        << DescribeReadError(error);
  }
}

// A v3 file, the format before the Match ID and the 16-byte Session IDs, is
// refused by its version before any record is read.
// Requirements: NFR-12
TEST(CaptureFormatTest, AVersion3FileIsRefused) {
  std::string v3 = Bytes(TwoPlayers(MatchEnd{}));
  v3[augusta::protocol::kCaptureMagic.size()] = 3;
  EXPECT_EQ(Read(v3).error(), (ReadError{.code = ReadErrorCode::kUnsupported, .format_version = 3}));
}

// A v2 file has no version byte: its magic is followed by its header record's
// length, which a reader takes for the version.
// Requirements: NFR-12
TEST(CaptureFormatTest, AVersion2FileIsRefused) {
  const std::string written = Bytes(TwoPlayers(MatchEnd{}));
  const std::size_t magic = augusta::protocol::kCaptureMagic.size();
  // A v2 header record, framed by its one-byte length: its type 1, its format
  // version 2, then the 79 bytes of the fields v3 kept (its engine version
  // "0.4.0" after its length, two pack hashes, a tick rate and a start time).
  const std::string record = std::string{1, 2} + std::string(79, 'x');
  const std::string v2 = written.substr(0, magic) + static_cast<char>(record.size()) + record;
  EXPECT_EQ(Read(v2).error(), (ReadError{.code = ReadErrorCode::kUnsupported,
                                         .format_version = static_cast<std::uint8_t>(record.size())}));
}

// Requirements: NFR-12
TEST(CaptureFormatTest, AMagicWithoutAHeaderIsRefused) {
  const std::string bytes = Bytes(TwoPlayers(MatchEnd{}));
  const ReadError unsupported{.code = ReadErrorCode::kUnsupported, .format_version = std::nullopt};
  EXPECT_EQ(Read(bytes.substr(0, augusta::protocol::kCaptureMagic.size())).error(), unsupported);
  EXPECT_EQ(Read(bytes.substr(0, augusta::protocol::kCaptureMagic.size() + 1)).error(), unsupported);
  EXPECT_FALSE(DescribeReadError(unsupported).contains("is of format version"));
}

// Each way a file's records may come out of the format's order (ADR-0050).
// Requirements: NFR-12
TEST(CaptureFormatTest, RecordsOutOfOrderAreRefused) {
  const Encoded header = EncodeHeader(TheHeader()).value();
  const Encoded first = EncodeJoin(1, TwoJoins()[0]).value();
  const Encoded second = EncodeJoin(2, TwoJoins()[1]).value();
  const auto event = [](std::uint32_t offset, Event what) { return EncodeEvent(At(offset, std::move(what))).value(); };
  // The header's frame alone, past the magic and the format version.
  const std::string header_again = Bytes({header}).substr(augusta::protocol::kCaptureMagic.size() + 1);

  const std::vector<std::pair<std::string, std::string>> cases = {
      {"a Join after an event", Bytes({header, first, event(0, Leave{.player = 1}), second})},
      {"players not numbered from 1", Bytes({header, second})},
      {"a player numbered twice", Bytes({header, first, first})},
      {"offsets out of order", Bytes({header, first, event(5, Leave{.player = 1}), event(4, Leave{.player = 1})})},
      {"an event naming a player no Join did", Bytes({header, first, event(0, Leave{.player = 2})})},
      {"a Death by a player no Join did", Bytes({header, first, event(0, Death{.victim = 1, .killer = 3})})},
      {"a winner no Join named", Bytes({header, first, event(0, MatchEnd{.winner = 2})})},
      {"an event after the Match end", Bytes({header, first, event(1, MatchEnd{}), event(1, Leave{.player = 1})})},
      {"a second header", Bytes({header, first}) + header_again},
  };
  for (const auto& [name, bytes] : cases) {
    EXPECT_EQ(Read(bytes).error().code, ReadErrorCode::kMalformed) << name;
  }
}

// A Match holds at most limits::kMaxPlayers players, so a capture of one holds
// at most that many Joins: one with more is no capture of a Match, which the
// protocol could not carry.
// Requirements: NFR-12
TEST(CaptureFormatTest, MoreJoinsThanAMatchHoldsAreRefused) {
  Capture full = TwoPlayers(MatchEnd{});
  full.joins.clear();
  for (std::size_t i = 0; i < augusta::limits::kMaxPlayers; ++i) {
    full.joins.push_back(
        Join{.session = Session(static_cast<std::uint8_t>(i + 1)), .character = "soldier", .spawn = {}});
  }
  Capture over = full;
  over.joins.push_back(Join{.session = Session(0xFF), .character = "soldier", .spawn = {}});

  EXPECT_EQ(Read(Bytes(full)), full);
  EXPECT_EQ(Read(Bytes(over)).error().code, ReadErrorCode::kMalformed);
}

// Requirements: NFR-12
TEST(CaptureFormatTest, ARecordThatDoesNotDecodeIsRefused) {
  std::string bytes = Bytes(TwoPlayers(MatchEnd{}));
  // A frame of one byte, a tag of a wire type protobuf lacks.
  bytes += std::string{'\x01', '\x7F'};
  EXPECT_EQ(Read(bytes).error().code, ReadErrorCode::kMalformed);
}

// Requirements: US-21
TEST(CaptureFormatTest, ALastRecordCutShortIsDroppedAndReported) {
  const Capture written = TwoPlayers(MatchEnd{});
  const std::string bytes = Bytes(written);
  const auto torn = Read(bytes.substr(0, bytes.size() - 1));
  ASSERT_TRUE(torn.has_value());
  EXPECT_TRUE(torn->torn);
  Capture expected = written;
  expected.events.pop_back();
  expected.torn = true;
  EXPECT_EQ(*torn, expected);
}

// A stream that fails is unreadable wherever it does, never a whole or torn capture.
// Requirements: NFR-12
TEST(CaptureFormatTest, AFailingStreamIsUnreadable) {
  const std::string bytes = Bytes(TwoPlayers(MatchEnd{}));
  for (const std::size_t held :
       {std::size_t{0}, std::size_t{3}, augusta::protocol::kCaptureMagic.size() + 2, bytes.size() - 1, bytes.size()}) {
    FailingBuffer buffer(bytes.substr(0, held));
    std::istream in(&buffer);
    EXPECT_EQ(ReadCapture(in).error().code, ReadErrorCode::kUnreadable) << held << " bytes before it failed";
  }
}

// Requirements: NFR-12
TEST(CaptureFormatTest, AStreamNeverOpenedIsUnreadable) {
  std::istringstream in;
  in.setstate(std::ios::failbit);
  EXPECT_EQ(ReadCapture(in).error().code, ReadErrorCode::kUnreadable);
}

// What each piece's size on disk says is what Write writes of it.
// Requirements: US-21
TEST(CaptureFormatTest, AnEncodedPiecesSizeOnDiskIsWhatIsWritten) {
  const Capture capture = TwoPlayers(MatchEnd{.winner = 2});
  std::vector<Encoded> pieces;
  pieces.push_back(EncodeHeader(capture.header).value());
  pieces.push_back(EncodeJoin(1, capture.joins[0]).value());
  for (const AtOffset<Event>& event : capture.events) {
    pieces.push_back(EncodeEvent(event).value());
  }
  std::size_t total = 0;
  for (const Encoded& piece : pieces) {
    EXPECT_EQ(Bytes({piece}).size(), piece.SizeOnDisk());
    total += piece.SizeOnDisk();
  }
  EXPECT_EQ(Bytes(pieces).size(), total);
  // The header's includes the magic and the format version.
  EXPECT_GT(pieces.front().SizeOnDisk(), augusta::protocol::kCaptureMagic.size() + 1);
}

// A field the codec cannot carry is the writer's broken invariant, named by its
// record type; nothing of the capture is written.
// Requirements: NFR-18
TEST(CaptureFormatTest, AFieldTheCodecCannotCarryIsACodecFailure) {
  Capture capture = TwoPlayers(MatchEnd{});
  capture.joins[1].character = std::string(augusta::protocol::kMaxCharacterNameLength + 1, 'x');

  const auto join = EncodeJoin(2, capture.joins[1]);
  ASSERT_FALSE(join.has_value());
  const auto* failure = std::get_if<augusta::failure::Failure>(&join.error());
  ASSERT_NE(failure, nullptr);
  EXPECT_EQ(failure->code, augusta::failure::Code::kInvariantViolated);
  ASSERT_EQ(failure->context.size(), 1U);
  EXPECT_EQ(failure->context[0].key, "capture_record_type");
  EXPECT_EQ(failure->context[0].value,
            std::to_string(static_cast<int>(augusta::protocol::CaptureRecordTypeWire::kJoin)));

  std::ostringstream out(std::ios::binary);
  const auto written = WriteCapture(out, capture);
  ASSERT_FALSE(written.has_value());
  EXPECT_TRUE(std::holds_alternative<augusta::failure::Failure>(written.error()));
  EXPECT_TRUE(out.str().empty());

  Header header = TheHeader();
  header.engine_version = std::string(augusta::protocol::kMaxEngineVersionLength + 1, 'v');
  const auto encoded = EncodeHeader(header);
  ASSERT_FALSE(encoded.has_value());
  EXPECT_TRUE(std::holds_alternative<augusta::failure::Failure>(encoded.error()));
}

// No record the codec accepts passes a frame's limit (#463), so the limit is
// reached under the module's interface, at its framing.
// Requirements: US-21
TEST(CaptureFormatTest, APayloadLongerThanAFrameHoldsIsARecordTooLong) {
  const std::vector<std::byte> longest(augusta::capture::kMaxFramePayload, std::byte{0xAB});
  const auto framed = augusta::capture::Framed(longest);
  ASSERT_TRUE(framed.has_value());
  EXPECT_EQ(framed->size(), longest.size() + 1);

  const std::vector<std::byte> too_long(augusta::capture::kMaxFramePayload + 1, std::byte{0xAB});
  const auto refused = augusta::capture::Framed(too_long);
  ASSERT_FALSE(refused.has_value());
  EXPECT_TRUE(std::holds_alternative<RecordTooLong>(refused.error()));
}

// Requirements: US-21
TEST(CaptureFormatTest, TheMagicCheckAcceptsACaptureAndRefusesAnythingElse) {
  std::istringstream capture(Bytes(TwoPlayers(MatchEnd{})), std::ios::binary);
  EXPECT_TRUE(StartsWithMagic(capture));

  for (const std::string& other : {std::string{}, std::string("AUGCAP"), std::string("AUGPACK\r\n and more")}) {
    std::istringstream in(other, std::ios::binary);
    EXPECT_FALSE(StartsWithMagic(in)) << other;
  }
  FailingBuffer buffer("AUG");
  std::istream failing(&buffer);
  EXPECT_FALSE(StartsWithMagic(failing));
}

// Requirements: US-21
TEST(CaptureFormatTest, EveryCaptureFileEndsInItsExtension) { EXPECT_EQ(augusta::capture::kExtension, ".acap"); }

}  // namespace
