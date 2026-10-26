#ifndef AUGUSTA_PROTOCOL_ENCODER_H_
#define AUGUSTA_PROTOCOL_ENCODER_H_

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

#include <boost/uuid/uuid.hpp>

#include "augusta.pb.h"
#include "augusta/grid.h"
#include "augusta/protocol.h"
#include "generated_messages.h"

/// \file
/// The encoder's half of the codec: how Encode maps each message's *Wire to its
/// generated augusta.Message member, checking the grids' ranges, the protocol's
/// limits and its text's UTF-8 as it does. A Match capture's records
/// (capture_records.h) map their fields with the same pieces, and Readdress a
/// recipient's own fields. Private to the module (ADR-0007, ADR-0038).
namespace augusta::protocol {

/// What Encode maps a *Wire to its generated message with. The first field it is
/// handed that the protocol cannot carry is remembered, so a mapping sets all of
/// a message's fields and the caller asks once at the end, and nothing of a
/// message with such a field is ever serialized.
class Outbound {
 public:
  /// Whether holds, remembering error if it does not.
  bool Check(bool holds, EncodeError error) {
    if (!holds && !error_.has_value()) {
      error_ = error;
    }
    return holds;
  }

  /// value, an enumerator that must lie between first and last, as its number.
  template <typename Enum>
  std::uint32_t FromEnum(Enum value, Enum first, Enum last) {
    const auto number = static_cast<std::uint32_t>(value);
    Check(number >= static_cast<std::uint32_t>(first) && number <= static_cast<std::uint32_t>(last),
          EncodeError::kInvalidEnum);
    return number;
  }

  /// flags, of which only the bits of mask may be set.
  std::uint32_t FromFlags(std::uint8_t flags, std::uint8_t mask) {
    Check((flags & ~mask) == 0, EncodeError::kReservedBits);
    return flags;
  }

  /// text, which may be at most max_length bytes long, and must be UTF-8.
  std::string_view Text(std::string_view text, std::size_t max_length) {
    Check(text.size() <= max_length, EncodeError::kFieldTooLong);
    Check(IsUtf8(text), EncodeError::kNotUtf8);
    return text;
  }

  /// Whether a list of size elements is at most max long.
  bool Fits(std::size_t size, std::size_t max) { return Check(size <= max, EncodeError::kFieldTooLong); }

  [[nodiscard]] std::optional<EncodeError> Error() const { return error_; }

 private:
  std::optional<EncodeError> error_;
};

/// value as a whole count of grid's step (ADR-0038), held within its range.
std::int32_t Steps(float value, const math::Grid& grid);

std::string_view BytesOf(const PackHashWire& hash);

std::string_view BytesOf(const boost::uuids::uuid& id);

void Write(const CommandWire& wire, pb::Command& command, Outbound& out);

/// An Authoritative State's recipient's own fields, none of which has a limit,
/// so none fails. A rifle's two times travel as floats, not on a grid: its
/// owner replays its commands from them, with the function the server stepped
/// them with, and must start from exactly what the server had. Its Recoil
/// offset is kept on the angle grid by that function, so its counts are exactly
/// what the server had too.
void WriteOwn(const AuthoritativeStateWire& wire, pb::AuthoritativeState& state);

/// One overload per message: the envelope's member, then its fields.
struct Encoder {
  pb::Message& message;
  Outbound& out;

  void operator()(const JoinRequestWire& wire) const;
  void operator()(const JoinAcceptedWire& wire) const;
  void operator()(const JoinRefusedWire& wire) const;
  void operator()(const CommandsWire& wire) const;
  void operator()(const AuthoritativeStateWire& wire) const;
  void operator()(const LobbyWire& wire) const;
  void operator()(const ReadyWire& wire) const;
  void operator()(const MatchStartWire& wire) const;
  void operator()(const MatchEndWire& wire) const;
  void operator()(const ShotWire& wire) const;
  void operator()(const HitConfirmationWire& wire) const;
  void operator()(const DeathWire& wire) const;
  void operator()(const ReplayListRequestWire& wire) const;
  void operator()(const ReplayListWire& wire) const;
  void operator()(const ReplayRequestWire& wire) const;
  void operator()(const ReenactRequestWire& wire) const;
#if AUGUSTA_DEBUG_TOOLS
  void operator()(const DebugSubscribeWire& wire) const;
  void operator()(const ShotJudgementWire& wire) const;
#endif
};

}  // namespace augusta::protocol

#endif  // AUGUSTA_PROTOCOL_ENCODER_H_
