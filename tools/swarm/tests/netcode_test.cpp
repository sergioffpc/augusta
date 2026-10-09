#include <chrono>
#include <cstdint>
#include <ostream>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

#include "augusta/netcode_stats.h"
#include "augusta/networking.h"
#include "example_server.h"
#include "run.h"

// The netcode under an impaired link (ADR-0013): whole runs of Scripted players
// against a server::Host in the same process, with the transport's simulated
// latency, jitter, loss and reordering on every packet either side sends, held
// to bounds on what reconciliation (ADR-0004) and lag compensation (ADR-0044)
// make of them. Real time, minutes a run, so the nightly runs them (label
// netcode), not every pull request.
namespace {

using augusta::harness::NetcodeStats;
using augusta::networking::SimulatedConditions;
using augusta::swarm::RunResult;
using augusta::swarm::RunScriptedPlayers;
using augusta::swarm::Verdict;
using augusta::swarm::testing::ExampleServer;

[[maybe_unused]] ::testing::Environment* const kNetworkingEnvironment =
    ::testing::AddGlobalTestEnvironment(new augusta::swarm::testing::NetworkingEnvironment);

// One set of link conditions. Both sides are in this process, so each applies
// to the server's packets and the clients' alike: a round trip is twice the
// latency, and a packet either way may be jittered, lost or reordered.
struct Profile {
  std::string_view name;
  SimulatedConditions conditions;
};

void PrintTo(const Profile& profile, std::ostream* out) { *out << profile.name; }

// From a good broadband link up to the agreed worst case: NFR-02's latency and
// beyond, as far as a Scripted player's Shooter's delay stays within lag
// compensation's 250 ms cap (ADR-0044). A Scripted player aims at where the
// newest Authoritative State puts its target and reports that state's tick as
// its Seen time, so its delay is about a round trip, the jitter both ways and
// the couple of ticks its commands wait queued on the server.
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
  void TearDown() override { augusta::networking::SimulateNetworkConditions({}); }
};

// Requirements: NFR-02, NFR-06
TEST_P(ImpairedLinkTest, ScriptedPlayersPlayThroughWithBoundedCorrectionsAndConfirmedHits) {
  const ExampleServer server(kPlayers);
  augusta::networking::SimulateNetworkConditions(GetParam().conditions);

  const RunResult result = RunScriptedPlayers(server.RunOf(kMatches, kTimeout));

  // Nothing disconnects: every player sees every Match end.
  ASSERT_EQ(result.verdict, Verdict::kSucceeded);
  NetcodeStats all;
  for (const NetcodeStats& player : result.players) {
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
