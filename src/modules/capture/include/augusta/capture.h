#ifndef AUGUSTA_CAPTURE_H_
#define AUGUSTA_CAPTURE_H_

#include <cstddef>
#include <cstdint>
#include <expected>
#include <istream>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "augusta/capture_records.h"
#include "augusta/failure.h"

/// \file
/// A Match capture's file (ADR-0050), in one place for every side that reads or
/// writes one: the server's Capturer, Replay and the Replay catalog, a Captured
/// player's harness::ReadScript, and test fixtures. A capture is its header, its
/// Joins and its events, each event at its offset in ticks from the Match's
/// first tick, in the engine's types. ReadCapture reads one and holds it to the
/// format's rules; EncodeHeader, EncodeJoin and EncodeEvent encode one piece of
/// it for Write, and WriteCapture writes a whole one.
///
/// A file is the capture's magic, then its format version as one raw byte, then
/// each record's payload after its length in one byte, the header first, in the
/// Networking Protocol's encoding: the Link (augusta/link_messages.h) turns each
/// record of the engine's types (augusta/capture_records.h) into its payload and
/// back, so this module names no protocol type (ADR-0038). Reading checks the
/// magic, then the format version before any record, then the records' order
/// (every Join first, numbered from 1, no more of them than a Match holds,
/// limits::kMaxPlayers, then the events in offset order, each naming a player a
/// Join did, nothing after the Match end). Pure stream I/O, on
/// whichever thread holds the stream.
namespace augusta::capture {

/// What every capture's file name ends in.
inline constexpr std::string_view kExtension = ".acap";

/// Why a capture does not read, wherever it is read: Replay and the Replay
/// catalog, and a Captured player's harness::ReadScript, report the same errors.
enum class ReadErrorCode : std::uint8_t {
  /// The stream could not be read, or was never opened.
  kUnreadable,
  /// It does not start with a capture's magic.
  kNotACapture,
  /// It is of a format version this engine does not read, or its header is missing.
  kUnsupported,
  /// A record does not decode, comes out of order, names a player no Join did,
  /// or is a Join past the most players a Match holds.
  kMalformed,
};

/// Why a capture does not read: its code, and what a message about it names
/// beyond the code.
struct ReadError {
  ReadErrorCode code = ReadErrorCode::kUnreadable;
  /// The format version the file names, for kUnsupported when it names one
  /// this engine does not read: a file of format 2, which has no version byte,
  /// names its header record's length. nullopt when its header is missing.
  std::optional<std::uint8_t> format_version = std::nullopt;

  bool operator==(const ReadError&) const = default;
};

/// What to tell whoever runs the process about error, naming the format
/// version it refuses, if any, beside the one this engine reads.
[[nodiscard]] std::string DescribeReadError(const ReadError& error);

/// Whether in starts with a capture's magic. Reads as far as the magic goes.
[[nodiscard]] bool StartsWithMagic(std::istream& in);

struct Capture {
  Header header;
  /// Its players, in player order.
  std::vector<Join> joins;
  /// Its events, in offset order.
  std::vector<AtOffset<Event>> events;
  /// Whether the file ended partway through a record, which was dropped: its
  /// writer stopped while writing it.
  bool torn = false;

  bool operator==(const Capture&) const = default;
};

/// Reads the capture in, held to the format's rules. A last record cut short is
/// dropped and reported in Capture::torn; a stream that fails, wherever it
/// does, is kUnreadable, never a whole or torn capture.
[[nodiscard]] std::expected<Capture, ReadError> ReadCapture(std::istream& in);

/// A record longer than a capture's frame holds: everything before it reads,
/// nothing after it would.
struct RecordTooLong {};

/// Why a piece of a capture does not encode: a failure::Failure for a field the
/// codec cannot carry (failure::Code::kInvariantViolated, a broken invariant of
/// the writer's), or a record too long for a frame.
using EncodeError = std::variant<failure::Failure, RecordTooLong>;

/// A piece of a capture as it is on disk, for Write.
class Encoded {
 public:
  [[nodiscard]] std::size_t SizeOnDisk() const { return bytes_.size(); }

 private:
  friend std::expected<Encoded, EncodeError> EncodeHeader(const Header& header);
  friend std::expected<Encoded, EncodeError> EncodeJoin(Player player, const Join& join);
  friend std::expected<Encoded, EncodeError> EncodeEvent(const AtOffset<Event>& event);
  friend void Write(std::ostream& out, const Encoded& encoded);

  explicit Encoded(std::vector<std::byte> bytes) : bytes_(std::move(bytes)) {}

  std::vector<std::byte> bytes_;
};

/// The capture's magic, this engine's format version, then header: what a file starts with.
[[nodiscard]] std::expected<Encoded, EncodeError> EncodeHeader(const Header& header);

/// The Join of player, numbered from 1 in the Match start's order, at offset 0.
[[nodiscard]] std::expected<Encoded, EncodeError> EncodeJoin(Player player, const Join& join);

/// One event, after every Join.
[[nodiscard]] std::expected<Encoded, EncodeError> EncodeEvent(const AtOffset<Event>& event);

/// Writes encoded to out, whose state then says whether it was.
void Write(std::ostream& out, const Encoded& encoded);

/// Writes capture to out whole - its header, its Joins, then its events - and
/// out's state then says whether it was; nothing is written unless every piece
/// encodes. Its torn flag is not written: a capture read back is torn only if
/// out was cut short. capture is held to nothing ReadCapture holds it to: one
/// out of the format's order is written as it is, and reads back kMalformed.
[[nodiscard]] std::expected<void, EncodeError> WriteCapture(std::ostream& out, const Capture& capture);

}  // namespace augusta::capture

#endif  // AUGUSTA_CAPTURE_H_
