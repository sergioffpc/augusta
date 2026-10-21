#ifndef AUGUSTA_CAPTURE_RECORDS_H_
#define AUGUSTA_CAPTURE_RECORDS_H_

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>

#include "augusta/assets.h"
#include "augusta/command.h"
#include "augusta/math.h"
#include "augusta/uuid.h"

/// \file
/// What a Match capture's records say (ADR-0050), in the engine's types: its
/// header, its Joins and its events. Apart from augusta/capture.h, which reads
/// and writes a capture's file, so the Link (augusta/link_messages.h) converts
/// them to the protocol's records and back without depending on the file's
/// reading and writing, which is built over it. Plain values.
namespace augusta::capture {

/// What a captured Match ran on and when it started.
struct Header {
  /// The capturing engine's version (augusta::EngineVersion).
  std::string engine_version;
  assets::PackHash server_pack{};
  assets::PackHash client_pack{};
  std::uint8_t tick_rate_hz = 0;
  /// When the Match started, in UTC.
  std::chrono::sys_time<std::chrono::milliseconds> started;

  bool operator==(const Header&) const = default;
};

/// A player's number in a capture: from 1, in its Match start's order, as
/// `augustac --reenact --player` names it. Player n's Join is Capture::joins[n - 1].
using Player = std::uint8_t;

/// A player of the Match start, where it spawned on the position grid.
struct Join {
  /// Its Session's ID (a UUIDv7, unique across every server).
  uuid::Uuid session{};
  /// Its Character, by its name in the scenario's manifest (ADR-0042).
  std::string character;
  math::Vec3 spawn{};

  bool operator==(const Join&) const = default;
};

/// A Command its player sent, as the server's command queue handed it to the World.
struct Command {
  Player player = 0;
  /// Its Seen time's tick, as an offset from the Match's first tick: negative
  /// for a State sent before the Match started. command.seen_tick is 0.
  std::int32_t seen_offset = 0;
  command::Command command{};

  bool operator==(const Command&) const = default;
};

/// A player whose connection ended mid-Match.
struct Leave {
  Player player = 0;

  bool operator==(const Leave&) const = default;
};

struct Death {
  Player victim = 0;
  Player killer = 0;

  bool operator==(const Death&) const = default;
};

struct MatchEnd {
  /// nullopt for a Draw.
  std::optional<Player> winner;

  bool operator==(const MatchEnd&) const = default;
};

using Event = std::variant<Command, Leave, Death, MatchEnd>;

/// An event at its offset in ticks from the Match's first tick.
template <typename T>
struct AtOffset {
  std::uint32_t offset = 0;
  T event{};

  bool operator==(const AtOffset&) const = default;
};

}  // namespace augusta::capture

#endif  // AUGUSTA_CAPTURE_RECORDS_H_
