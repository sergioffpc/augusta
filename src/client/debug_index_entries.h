#ifndef AUGUSTA_CLIENT_DEBUG_INDEX_ENTRIES_H_
#define AUGUSTA_CLIENT_DEBUG_INDEX_ENTRIES_H_

#include <cstdint>
#include <string_view>
#include <vector>

#include "augusta/debug_index.h"

/// \file
/// Which debug tools the client Runtime's Debug index lists (ADR-0075), private
/// to the Runtime: the decision, by the client's mode, apart from the Runtime
/// that shows the index and does what a switch asks. A tool is listed only in
/// the modes that can feed it. Compiled only in a Development build (ADR-0074).
namespace augusta::client {

/// The Debug index's entry for the Debug HUD, its first (`debug.hud`).
inline constexpr std::string_view kDebugHudEntry = "hud";

/// What a client runs as. A player who dies spectates in the same mode.
enum class ClientMode : std::uint8_t {
  kPlayer,
  /// A Captured player (`--reenact`, ADR-0050).
  kCapturedPlayer,
  /// `--replay` (ADR-0051): no predicted body of its own.
  kReplayViewer,
};

/// Which listed tools start on, as the `debug` config section has them.
struct DebugIndexStart {
  bool hud = true;
  bool profiling_panel = false;
  bool physics_panel = false;
};

/// The Debug index's entries, in the `debug` section's fixed order: the Debug
/// HUD, the Profiling panel, then the Physics panel, which only a mode with a
/// predicted body and a Server view lists (never a Replay viewer). Each panel's
/// entry is named by its panel's id.
[[nodiscard]] std::vector<debug::IndexEntry> DebugIndexEntries(const DebugIndexStart& start, ClientMode mode);

}  // namespace augusta::client

#endif  // AUGUSTA_CLIENT_DEBUG_INDEX_ENTRIES_H_
