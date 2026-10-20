#include <chrono>
#include <cstdint>
#include <ostream>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

#include "augusta/agent.h"
#include "augusta/assets.h"
#include "augusta/link.h"
#include "augusta/netcode_stats.h"
#include "example_run.h"
#include "match_loop.h"
#include "testing_server.h"

// The netcode under an impaired link (ADR-0013): whole runs of Agents,
// composing Intents as load_test.py does, against a host::Host in the same
// process, with the transport's simulated latency, jitter, loss and reordering
// on every packet either side sends, held
// to bounds on what reconciliation (ADR-0004) and lag compensation (ADR-0044)
// make of them. Real time, minutes a run, so the nightly runs them (label
// netcode), not every pull request.
namespace {

using augusta::agent::LoadedPack;
using augusta::agent::testing::ExampleClientPack;
using augusta::agent::testing::ExamplePacks;
using augusta::agent::testing::MatchLoopResult;
using augusta::agent::testing::PlayMatches;
using augusta::agent::testing::RunAgainst;
using augusta::agent::testing::Server;
using augusta::agent::testing::Verdict;
using augusta::harness::NetcodeStats;
using augusta::link::SimulatedConditions;

[[maybe_unused]] ::testing::Environment* const kNetworkingEnvironment =
    ::testing::AddGlobalTestEnvironment(new augusta::agent::testing::NetworkingEnvironment);

// One set of link conditions. Both sides are in this process, so each applies
// to the server's packets and the clients' alike: a round trip is twice the
// latency, and a packet either way may be jittered, lost or reordered.
struct Profile {
  std::string_view name;
  SimulatedConditions conditions;
};

void PrintTo(const Profile& profile, std::ostream* out) { *out << profile.name; }

// From a good broadband link up to the agreed worst case: NFR-02's latency and
// beyond, up to the 150 ms round trip lag compensation's 300 ms cap is set to
// cover (ADR-0044). An Agent's AimAt aims at where the newest Authoritative
// State puts its target and the Harness reports that state's tick as its Seen
// time, so its delay is about a round trip, the jitter both ways and the
// couple of ticks its commands wait queued on the server: it leaves out the
// Interpolation delay a real client adds. A real client's budget is held by
// the budget test (tests/shooters_delay_budget_test.cpp) instead.
constexpr Profile kBroadband{
    .name = "Broadband",
    .conditions = {.latency_ms = 20,
                   .jitter_mean_ms = 5,
                   .jitter_max_ms = 20,
                   .loss_percent = 1.0F,
                   .reorder_percent = 1.0F,
                   .reorder_delay_ms = 10},
};
constexpr Profile kNfr02{
    .name = "Nfr02",
    .conditions = {.latency_ms = 50,
                   .jitter_mean_ms = 10,
                   .jitter_max_ms = 30,
                   .loss_percent = 5.0F,
                   .reorder_percent = 2.0F,
                   .reorder_delay_ms = 20},
};
constexpr Profile kWorstCase{
    .name = "WorstCase",
    .conditions = {.latency_ms = 75,
                   .jitter_mean_ms = 10,
                   .jitter_max_ms = 30,
                   .loss_percent = 10.0F,
                   .reorder_percent = 5.0F,
                   .reorder_delay_ms = 20},
};

// Enough players for every Match to see a few fights, and Matches enough for
// a few hundred rounds and some thousand ticks in all, the Lobby between them
// crossed under the conditions too.
constexpr std::uint8_t kPlayers = 4;
constexpr std::uint32_t kMatches = 5;
constexpr auto kTimeout = std::chrono::minutes(5);

// The bounds every profile is held to (ADR-0013). A correction of 2 m or more
// is shown at once rather than slid away (ADR-0004), so none may reach it. At
// every profile here about 1% of Match ticks correct and over two thirds of the
// rounds fired hit: the bounds leave room for a noisy runner, and fail when
// either goes several times worse.
constexpr float kMaxCorrectionM = 2.0F;
constexpr float kMaxCorrectionRate = 0.05F;
constexpr float kMinHitRate = 0.4F;

class ImpairedLinkTest : public ::testing::TestWithParam<Profile> {
 protected:
  void TearDown() override { augusta::link::SimulateNetworkConditions({}); }
};

// Requirements: NFR-02, NFR-06
TEST_P(ImpairedLinkTest, AgentsPlayThroughWithBoundedCorrectionsAndConfirmedHits) {
  const Server server((ExamplePacks() / "server").replace_extension(augusta::assets::kPackExtension),
                      ExamplePacks() / "test.pub", kPlayers);
  const LoadedPack pack = ExampleClientPack();
  augusta::link::SimulateNetworkConditions(GetParam().conditions);

  const MatchLoopResult result = PlayMatches(RunAgainst(server.Address(), pack, kMatches, kTimeout));

  // Nothing disconnects: every Agent sees every Match end.
  ASSERT_EQ(result.verdict, Verdict::kSucceeded) << result.failure;
  NetcodeStats all;
  for (const NetcodeStats& player : result.agents) {
    EXPECT_LT(player.largest_correction_m, kMaxCorrectionM);
    all += player;
  }
  RecordProperty("correction_rate", std::to_string(all.CorrectionRate()));
  RecordProperty("hit_rate", std::to_string(all.HitRate()));
  EXPECT_LE(all.CorrectionRate(), kMaxCorrectionRate);
  // Every Match ends by last player standing, so rounds were fired and hit.
  EXPECT_GE(all.HitRate(), kMinHitRate);
}
INSTANTIATE_TEST_SUITE_P(Profiles, ImpairedLinkTest, ::testing::Values(kBroadband, kNfr02, kWorstCase),
                         [](const ::testing::TestParamInfo<Profile>& info) { return std::string(info.param.name); });

}  // namespace
