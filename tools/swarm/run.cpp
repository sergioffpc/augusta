#include "run.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <format>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <vector>

#include "augusta/harness.h"
#include "augusta/logging.h"
#include "augusta/physics.h"
#include "augusta/prediction.h"
#include "augusta/runner.h"
#include "augusta/supervisor.h"
#include "netcode_stats.h"
#include "scripted_player.h"

namespace augusta::swarm {

namespace {

// How often the caller's thread looks in on the players: well under a tick,
// so a new Roster is answered with Ready within one.
constexpr auto kWatchInterval = std::chrono::milliseconds(5);

prediction::World WorldWithMap(const std::vector<physics::CollisionMesh>& map) {
  prediction::World world;
  for (const physics::CollisionMesh& mesh : map) {
    if (auto added = world.AddCollisionMesh(mesh); !added) {
      throw std::runtime_error(
          std::format("augusta-swarm: map collision rejected: {}", physics::DescribeCollisionMeshError(added.error())));
    }
  }
  return world;
}

// One Scripted player and the Session and Runner it plays through. Not movable:
// the Runner's threads hold references to the rest.
class Player {
 public:
  Player(std::size_t index, const RunConfig& config)
      : index_(index),
        session_(config.session, WorldWithMap(config.map)),
        script_(config.seed + static_cast<std::uint32_t>(index)) {
    runner_.emplace(
        session_, harness::RunnerHooks{
                      .next_command = [this] { return script_.NextCommand(*session_.GetServerView(), predicted_own_); },
                      .on_tick = [this](const harness::PredictedTick& tick) { Tally(tick); },
                      .on_network_round = {},
                  });
  }

  Player(const Player&) = delete;
  Player& operator=(const Player&) = delete;
  Player(Player&&) = delete;
  Player& operator=(Player&&) = delete;

  [[nodiscard]] std::shared_ptr<const harness::ServerView> View() const { return session_.GetServerView(); }

  // Reports Ready for view's Roster once: a Scripted player has nothing to load
  // for anyone in it.
  void GetReady(const harness::ServerView& view) {
    if (view.GetPhase() != harness::Phase::kLobby || !view.lobby.has_value() || ready_version_ == view.lobby->version) {
      return;
    }
    session_.ReportReady(view.lobby->version);
    ready_version_ = view.lobby->version;
  }

  // What its prediction and fire have come to so far.
  [[nodiscard]] NetcodeStats Stats() const {
    const std::scoped_lock lock(tally_mutex_);
    return tally_.Stats();
  }

  // Takes the Hit confirmations it has been sent since the last call, for its Stats.
  void CollectHitConfirmations() {
    const std::size_t confirmations = session_.TakeHitConfirmations().size();
    const std::scoped_lock lock(tally_mutex_);
    tally_.RecordHitConfirmations(confirmations);
  }

  // How far it has got as of view, logging a Match end or a failure the first
  // time it is seen.
  PlayerProgress Progress(const harness::ServerView& view) {
    const PlayerProgress progress{.matches_ended = MatchesEnded(view), .failed = Failed()};
    if (progress.matches_ended > logged_matches_ended_) {
      LI("subsystem=swarm event=match_ended player={} matches_ended={} won={}", index_, progress.matches_ended,
         view.match_end.has_value() && view.match_end->winner == view.accepted->session);
      logged_matches_ended_ = progress.matches_ended;
    }
    return progress;
  }

 private:
  // The Runner's Prediction thread, each Tick.
  void Tally(const harness::PredictedTick& tick) {
    predicted_own_ = tick.state.local_body;
    const bool in_match = session_.GetPhase() == harness::Phase::kMatch;
    const std::scoped_lock lock(tally_mutex_);
    tally_.RecordTick(tick.state, in_match);
  }

  bool Failed() {
    if (const auto worker = runner_->Failure(); worker.has_value()) {
      LogFailureOnce(supervisor::DescribeWorkerFailure(*worker));
      return true;
    }
    if (const auto session = session_.GetFailure(); session.has_value()) {
      LogFailureOnce(harness::DescribeFailure(*session));
      return true;
    }
    return false;
  }

  void LogFailureOnce(std::string_view reason) {
    if (!failure_logged_) {
      LE("subsystem=swarm event=player_failed player={} reason=\"{}\"", index_, reason);
      failure_logged_ = true;
    }
  }

  std::size_t index_;
  harness::Session session_;
  ScriptedPlayer script_;
  // Where the last Tick's prediction left its own body, for its script to aim
  // from. The Runner's Prediction thread only.
  physics::BodyState predicted_own_{};
  std::optional<std::uint32_t> ready_version_;
  std::uint32_t logged_matches_ended_ = 0;
  bool failure_logged_ = false;
  // Ticked on the Prediction thread, read and handed Hit confirmations on the caller's.
  mutable std::mutex tally_mutex_;
  NetcodeTally tally_;
  // Last, so its threads are joined before anything they use goes.
  std::optional<harness::Runner> runner_;
};

// What each player's prediction and fire came to, logged one line a player.
std::vector<NetcodeStats> StatsOf(const std::vector<std::unique_ptr<Player>>& players) {
  std::vector<NetcodeStats> stats;
  for (std::size_t index = 0; index < players.size(); ++index) {
    // Those that arrived since the watch last looked count too.
    players[index]->CollectHitConfirmations();
    const NetcodeStats& player = stats.emplace_back(players[index]->Stats());
    LI("subsystem=swarm event=player_netcode player={} match_ticks={} corrections={} largest_correction_m={:.3f} "
       "rounds_fired={} hit_confirmations={}",
       index, player.match_ticks, player.corrections, player.largest_correction_m, player.rounds_fired,
       player.hit_confirmations);
  }
  return stats;
}

}  // namespace

std::string_view DescribeVerdict(Verdict verdict) {
  switch (verdict) {
    case Verdict::kRunning:
      return "running";
    case Verdict::kSucceeded:
      return "succeeded";
    case Verdict::kFailed:
      return "failed";
    case Verdict::kTimedOut:
      return "timed out";
  }
  return "unknown";
}

std::uint32_t MatchesEnded(const harness::ServerView& view) {
  return view.in_match ? view.matches_started - 1 : view.matches_started;
}

Verdict Judge(std::span<const PlayerProgress> players, std::uint32_t player_count, std::uint32_t matches,
              bool timed_out) {
  const bool all_done =
      player_count > 0 && players.size() == player_count &&
      std::ranges::all_of(players, [&](const PlayerProgress& player) { return player.matches_ended >= matches; });
  if (all_done) {
    return Verdict::kSucceeded;
  }
  if (std::ranges::any_of(players, &PlayerProgress::failed)) {
    return Verdict::kFailed;
  }
  return timed_out ? Verdict::kTimedOut : Verdict::kRunning;
}

RunResult RunScriptedPlayers(const RunConfig& config) {
  const auto deadline = std::chrono::steady_clock::now() + config.timeout;
  std::vector<std::unique_ptr<Player>> players;
  players.push_back(std::make_unique<Player>(0, config));
  // The scenario's Player count, once the server has admitted the first player.
  std::uint32_t player_count = 0;

  std::vector<PlayerProgress> progress;
  while (true) {
    progress.clear();
    for (const std::unique_ptr<Player>& player : players) {
      const std::shared_ptr<const harness::ServerView> view = player->View();
      player->GetReady(*view);
      player->CollectHitConfirmations();
      progress.push_back(player->Progress(*view));
    }
    if (player_count == 0) {
      const std::shared_ptr<const harness::ServerView> view = players.front()->View();
      if (const auto& accepted = view->accepted; accepted.has_value()) {
        player_count = accepted->parameters.player_count;
        LI("subsystem=swarm event=player_count_known player_count={}", player_count);
        while (players.size() < player_count) {
          players.push_back(std::make_unique<Player>(players.size(), config));
        }
      }
    }
    const Verdict verdict = Judge(progress, player_count, config.matches, std::chrono::steady_clock::now() >= deadline);
    if (verdict != Verdict::kRunning) {
      LI("subsystem=swarm event=run_finished verdict=\"{}\" players={} matches={}", DescribeVerdict(verdict),
         players.size(), config.matches);
      return RunResult{.verdict = verdict, .players = StatsOf(players)};
    }
    std::this_thread::sleep_for(kWatchInterval);
  }
}

}  // namespace augusta::swarm
