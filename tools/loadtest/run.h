#ifndef AUGUSTA_LOADTEST_RUN_H_
#define AUGUSTA_LOADTEST_RUN_H_

#include <chrono>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "augusta/harness.h"
#include "augusta/physics.h"
#include "netcode_stats.h"

/// \file
/// One run of Scripted players (scripted_player.h) against a server: as many
/// as its scenario's Player count, each on a harness::Session under a
/// harness::Runner, played through a number of Match ends. What augusta-loadtest
/// (main.cpp) does once it has read its config and its pack, and what a test
/// does against a server::Host in the same process.
///
/// A Scripted player loads nothing to draw anyone, so it reports Ready for
/// every Roster as soon as it is sent. The run's threads are the Runners'
/// (two per player) and the caller's, which watches them until the verdict.
namespace augusta::loadtest {

/// What a run needs.
struct RunConfig {
  /// The server to connect to, the client pack's hash and the character, the
  /// same for every Scripted player.
  harness::SessionConfig session;
  /// The Map's collision, loaded from the client pack: each Scripted player
  /// predicts against it.
  std::vector<physics::CollisionMesh> map;
  /// How many Match ends every Scripted player must see, 1 or more.
  std::uint32_t matches = 1;
  /// How long the run may take before it fails.
  std::chrono::steady_clock::duration timeout{};
  /// The first Scripted player's seed; each next one takes the next number.
  std::uint32_t seed = 0;
};

/// How far one Scripted player has got.
struct PlayerProgress {
  /// How many Match ends it has seen (MatchesEnded).
  std::uint32_t matches_ended = 0;
  /// Whether its Session or its Runner has failed.
  bool failed = false;
};

/// How a run stands.
enum class Verdict : std::uint8_t {
  /// Still playing.
  kRunning,
  /// Every Scripted player has seen every Match end the run asked for.
  kSucceeded,
  /// A Scripted player's Session or Runner failed first.
  kFailed,
  /// The timeout passed first.
  kTimedOut,
};

/// A word for verdict, for logs.
[[nodiscard]] std::string_view DescribeVerdict(Verdict verdict);

/// How many Match ends view tells of: one for every match started for this
/// player, but the one it is still in.
[[nodiscard]] std::uint32_t MatchesEnded(const harness::ServerView& view);

/// How a run stands with players as far as they have got, out of the
/// player_count it needs (the scenario's Player count, once known; 0 before),
/// each to see matches Match ends; timed_out once the run's timeout has
/// passed. Success wins over a failure or a timeout that comes with it: the
/// run has done what it was for.
[[nodiscard]] Verdict Judge(std::span<const PlayerProgress> players, std::uint32_t player_count, std::uint32_t matches,
                            bool timed_out);

/// How a run ended.
struct RunResult {
  /// Never kRunning.
  Verdict verdict = Verdict::kRunning;
  /// What each Scripted player's prediction and fire came to, in the order
  /// they connected.
  std::vector<NetcodeStats> players;
};

/// Plays a run to its verdict: connects one Scripted player, then, once the
/// server has admitted it and told it the Player count, the rest, and stops
/// them all once Judge has a verdict. Logs each player's failure, each Match
/// end it sees and, at the end, its NetcodeStats. Throws std::runtime_error if
/// physics rejects a mesh of the map.
[[nodiscard]] RunResult RunScriptedPlayers(const RunConfig& config);

}  // namespace augusta::loadtest

#endif  // AUGUSTA_LOADTEST_RUN_H_
