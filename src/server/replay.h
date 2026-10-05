#ifndef AUGUSTA_SERVER_REPLAY_H_
#define AUGUSTA_SERVER_REPLAY_H_

#include <cstdint>
#include <expected>
#include <optional>
#include <string_view>

#include "augusta/tick.h"
#include "content.h"
#include "recording.h"

/// \file
/// Replaying a match recording (recording.h, ADR-0050): a fresh
/// SimulationWorld, built from the content the recording was made on, is handed
/// every tick's recorded input in turn, and each tick must resolve the recorded
/// outcome. What "the same" means is the Tolerance's: bit for bit on the build
/// that recorded it, within a grid step of position on any other (PhysX and
/// the compilers are not deterministic across builds, ADR-0045). Used by the
/// augusta_replay tool and by the golden match test; no I/O of its own.
namespace augusta::server {

/// How far a replayed outcome may be from the recorded one.
struct Tolerance {
  /// How far, in metres per axis, a body's position and a Shot's origin may be
  /// off; a body's velocity, derived from two positions a tick apart, may be off
  /// by twice this per tick. Every other value must be equal. 0 asks for the
  /// same bits.
  float position = 0.0F;
};

/// For a replay on the build that recorded it.
inline constexpr Tolerance kSameBuild{};

/// For a replay on another build or platform: one step of the position grid
/// (ADR-0038), the most a body that rounds onto it differently can be off.
inline constexpr Tolerance kAcrossBuilds{.position = 1.0F / 1024.0F};

/// What of a tick's outcome a replay resolved differently, in the order they
/// are checked; or what kept it from replaying the tick at all.
enum class DivergenceKind : std::uint8_t {
  /// The recording names a Character the content lacks.
  kUnknownCharacter,
  kSpawns,
  kBodies,
  kShots,
  kHits,
  kDeaths,
  kMatchEnd,
};

/// A short lowercase description of kind, for whoever runs the replay.
[[nodiscard]] std::string_view DescribeDivergenceKind(DivergenceKind kind);

/// The first part of replayed that differs from recorded beyond tolerance, for
/// a tick of delta_time seconds; nullopt when none does.
[[nodiscard]] std::optional<DivergenceKind> FindDivergence(const TickOutcome& recorded, const TickOutcome& replayed,
                                                           float delta_time, const Tolerance& tolerance);

/// The first tick of a replay whose outcome is not the recorded one.
struct Divergence {
  /// The tick, as the World numbers it, from 1.
  tick::Tick tick = 0;
  DivergenceKind kind = DivergenceKind::kBodies;
  TickOutcome recorded;
  /// Empty when kind is kUnknownCharacter: the tick did not run.
  TickOutcome replayed;
};

/// Replays recording on a World built from content, loaded from the server
/// pack the recording names, and returns how many ticks resolved their
/// recorded outcome within tolerance: all of them, or the first that did not.
[[nodiscard]] std::expected<tick::Tick, Divergence> Replay(const Recording& recording, Content content,
                                                           const Tolerance& tolerance);

}  // namespace augusta::server

#endif  // AUGUSTA_SERVER_REPLAY_H_
