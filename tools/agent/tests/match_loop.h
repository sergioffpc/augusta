#ifndef AUGUSTA_AGENT_TESTS_MATCH_LOOP_H_
#define AUGUSTA_AGENT_TESTS_MATCH_LOOP_H_

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "agent.h"
#include "augusta/netcode_stats.h"

/// \file
/// The C++ tests' load test (ADR-0013, ADR-0052): what examples/load_test.py
/// does, composing the same Intents, so ctest plays whole Matches with no
/// Python. It connects one Agent, then, once the server has told it the
/// scenario's Player count, the rest. In a Match each wanders near its spawn a
/// leg at a time (Move; MoveTo back once strayed), aims at the nearest living
/// other player (AimAt) and fires in Bursts (FireBursts). Every random choice
/// comes from the seed, the next Agent's from the next number.
namespace augusta::agent::testing {

/// What a run of Agents needs.
struct MatchLoopConfig {
  /// The server to connect to, as "host:port".
  std::string server;
  /// The client pack, the same for every Agent.
  const LoadedPack* pack = nullptr;
  std::string character;
  /// How many Match ends every Agent must see, 1 or more.
  std::uint32_t matches = 1;
  /// How long the run may take before it fails.
  std::chrono::steady_clock::duration timeout{};
  /// The first Agent's seed.
  std::uint32_t seed = 0;
};

/// How a run ended.
enum class Verdict : std::uint8_t {
  /// Every Agent saw every Match end the run asked for.
  kSucceeded,
  /// An Agent failed first.
  kFailed,
  /// The timeout passed first.
  kTimedOut,
};

struct MatchLoopResult {
  Verdict verdict = Verdict::kTimedOut;
  /// The first Agent's failure, for kFailed.
  std::string failure;
  /// What each Agent's prediction and fire came to, in the order they connected.
  std::vector<harness::NetcodeStats> agents;
};

/// Plays a run to its verdict, then closes its Agents. Throws
/// std::runtime_error if physics rejects a mesh of the pack's Map.
[[nodiscard]] MatchLoopResult PlayMatches(const MatchLoopConfig& config);

}  // namespace augusta::agent::testing

#endif  // AUGUSTA_AGENT_TESTS_MATCH_LOOP_H_
