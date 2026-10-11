#include "match_loop.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <numbers>
#include <optional>
#include <random>
#include <string>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#include "agent.h"
#include "augusta/command.h"
#include "augusta/harness.h"
#include "augusta/intent.h"
#include "augusta/math.h"
#include "augusta/physics.h"
#include "report.h"

namespace augusta::agent::testing {

namespace {

using Clock = std::chrono::steady_clock;

// How often the run looks again at what each Agent has been told, to decide:
// load_test.py's DECIDE_S.
constexpr auto kDecideInterval = std::chrono::milliseconds(50);
// How far an Agent may stray from its spawn, in metres, before it heads back:
// it knows nothing of the Map's edges.
constexpr float kLeashM = 3.0F;
// A leg lasts one to three seconds; of every ten legs, three sprint, and seven
// stand, two crouch and one goes prone.
constexpr std::uint32_t kMinLegSeconds = 1;
constexpr std::uint32_t kLegSecondsChoices = 3;
constexpr std::uint32_t kOutOf = 10;
constexpr std::uint32_t kSprinting = 3;
constexpr std::uint32_t kStanding = 7;
constexpr std::uint32_t kCrouching = 2;
// How many values mt19937 draws from, as a double: what maps a draw onto [0, 1).
constexpr double kDrawRange = static_cast<double>(std::numeric_limits<std::uint32_t>::max()) + 1.0;
constexpr double kFullTurn = 2.0 * std::numbers::pi;

physics::Stance StanceFor(std::uint32_t draw) {
  if (draw < kStanding) {
    return physics::Stance::kStanding;
  }
  if (draw < kStanding + kCrouching) {
    return physics::Stance::kCrouching;
  }
  return physics::Stance::kProne;
}

// What one Agent has reported that the run needs: kept by its sink on the
// Runner's threads, read on the run's.
class Reports {
 public:
  ReportSink Sink() {
    return [this](Report report) { Take(std::move(report)); };
  }

  [[nodiscard]] std::uint32_t MatchesEnded() const {
    const std::scoped_lock lock(mutex_);
    return matches_ended_;
  }

  [[nodiscard]] std::optional<std::string> Failure() const {
    const std::scoped_lock lock(mutex_);
    return failure_;
  }

  // Whether the Intent numbered intent has ended other than Replaced, told
  // once.
  [[nodiscard]] bool TakeEndedOnItsOwn(std::uint64_t intent) {
    const std::scoped_lock lock(mutex_);
    return std::erase(ended_on_their_own_, intent) > 0;
  }

 private:
  void Take(Report report) {
    const std::scoped_lock lock(mutex_);
    if (std::holds_alternative<harness::MatchEnd>(report)) {
      ++matches_ended_;
    } else if (const auto* ended = std::get_if<IntentEnded>(&report)) {
      if (ended->ending != harness::IntentEnding::kReplaced) {
        ended_on_their_own_.push_back(ended->intent);
      }
    } else if (auto* failed = std::get_if<AgentFailure>(&report)) {
      failure_ = std::move(failed->description);
    }
  }

  mutable std::mutex mutex_;
  std::uint32_t matches_ended_ = 0;
  std::optional<std::string> failure_;
  std::vector<std::uint64_t> ended_on_their_own_;
};

// What one Agent decides in a Match, from its view and its own seed: what
// load_test.py's Policy does.
class Tactics {
 public:
  Tactics(Agent& agent, Reports& reports, std::uint32_t seed) : agent_(agent), reports_(reports), random_(seed) {}

  void Decide(Clock::time_point now) {
    const AgentView view = agent_.View();
    const harness::ServerView& server = *view.server;
    // Outside a Match and once dead, it decides nothing: the Harness holds its
    // Intents, sending nothing they say, until it plays again.
    if (!server.OwnAlive() || !server.authoritative.has_value()) {
      return;
    }
    const harness::EntityId own = *server.OwnEntity();
    const auto& bodies = server.authoritative->bodies;
    const auto own_body = std::ranges::find(bodies, own, &harness::EntityBody::entity);
    if (own_body == bodies.end()) {
      return;
    }
    if (server.matches_started != match_) {
      StartMatch(server, now);
    }
    Wander(now);
    HeadBackIfStrayed(view.own, now);
    TakeOnNearest(server, own, own_body->body.position);
  }

 private:
  void StartMatch(const harness::ServerView& server, Clock::time_point now) {
    // The last Match's leg and way back lead nowhere in this one, and its
    // target is gone.
    match_ = server.matches_started;
    leg_ends_ = now;
    heading_back_.reset();
    aim_.reset();
    aimed_at_.reset();
    spawn_.reset();
    const auto& players = server.match_start->players;
    if (const auto found = std::ranges::find(players, server.accepted->session, &harness::MatchPlayer::session);
        found != players.end()) {
      spawn_ = found->spawn;
    }
  }

  void Wander(Clock::time_point now) {
    if (heading_back_.has_value() || now < leg_ends_) {
      return;
    }
    const double turn = static_cast<double>(random_()) / kDrawRange;
    const auto yaw = static_cast<float>((turn * kFullTurn) - std::numbers::pi);
    sprint_ = random_() % kOutOf < kSprinting;
    stance_ = StanceFor(random_() % kOutOf);
    leg_ends_ = now + std::chrono::seconds(kMinLegSeconds + (random_() % kLegSecondsChoices));
    agent_.SetMovement(
        harness::Move{.direction = command::ViewDirection(yaw, 0.0F), .sprint = sprint_, .stance = stance_});
  }

  void HeadBackIfStrayed(const physics::BodyState& own, Clock::time_point now) {
    if (heading_back_.has_value()) {
      // Back, or as near as it can get: the next leg is drawn as any other.
      if (reports_.TakeEndedOnItsOwn(*heading_back_)) {
        heading_back_.reset();
        leg_ends_ = now;
      }
      return;
    }
    if (!spawn_.has_value() || std::hypot(own.position.x - spawn_->x, own.position.z - spawn_->z) <= kLeashM) {
      return;
    }
    // Ending the leg under way, until it is back or can get no nearer.
    heading_back_ = agent_.SetMovement(harness::MoveTo{.point = *spawn_, .sprint = sprint_, .stance = stance_});
  }

  void TakeOnNearest(const harness::ServerView& server, harness::EntityId own, const math::Vec3& position) {
    std::optional<harness::EntityId> nearest;
    float nearest_m = std::numeric_limits<float>::max();
    for (const harness::EntityBody& other : server.authoritative->bodies) {
      if (other.entity == own || std::ranges::contains(server.dead, other.entity)) {
        continue;
      }
      if (const float distance = math::Length(other.body.position - position); distance < nearest_m) {
        nearest = other.entity;
        nearest_m = distance;
      }
    }
    if (!nearest.has_value()) {
      if (firing_) {
        agent_.ClearTrigger();
        firing_ = false;
      }
      return;
    }
    // An AimAt whose target is gone is aimed again, at the nearest then.
    if (aim_.has_value() && reports_.TakeEndedOnItsOwn(*aim_)) {
      aimed_at_.reset();
    }
    if (nearest != aimed_at_) {
      aim_ = agent_.SetAim(harness::AimAt{.target = *nearest});
      aimed_at_ = nearest;
    }
    if (!firing_) {
      agent_.SetTrigger(harness::FireBursts{});
      firing_ = true;
    }
  }

  Agent& agent_;
  Reports& reports_;
  // The standard fixes mt19937's sequence, unlike its distributions', so every
  // platform draws the same numbers from the same seed.
  std::mt19937 random_;
  std::uint32_t match_ = 0;
  std::optional<math::Vec3> spawn_;
  Clock::time_point leg_ends_;
  bool sprint_ = false;
  physics::Stance stance_ = physics::Stance::kStanding;
  // The MoveTo heading back to the spawn, while it is under way.
  std::optional<std::uint64_t> heading_back_;
  // The AimAt under way, and its target.
  std::optional<std::uint64_t> aim_;
  std::optional<harness::EntityId> aimed_at_;
  bool firing_ = false;
};

// One Agent of the run, its Reports and its Tactics. Not movable: the Agent's
// threads report to its Reports.
class RunAgent {
 public:
  RunAgent(const MatchLoopConfig& config, std::uint32_t seed)
      : agent_(config.server, *config.pack, config.character, Readiness::kForEveryRoster, reports_.Sink()),
        tactics_(agent_, reports_, seed) {}

  RunAgent(const RunAgent&) = delete;
  RunAgent& operator=(const RunAgent&) = delete;
  RunAgent(RunAgent&&) = delete;
  RunAgent& operator=(RunAgent&&) = delete;
  ~RunAgent() = default;

  Agent& GetAgent() { return agent_; }
  const Reports& GetReports() const { return reports_; }
  void Decide(Clock::time_point now) { tactics_.Decide(now); }

 private:
  // First, so it outlives the Agent that reports to it.
  Reports reports_;
  Agent agent_;
  Tactics tactics_;
};

// The scenario's Player count, once the server has admitted agent.
std::optional<std::uint8_t> PlayerCount(const Agent& agent) {
  const AgentView view = agent.View();
  if (!view.server->accepted.has_value()) {
    return std::nullopt;
  }
  return view.server->accepted->parameters.player_count;
}

}  // namespace

MatchLoopResult PlayMatches(const MatchLoopConfig& config) {
  const Clock::time_point deadline = Clock::now() + config.timeout;
  std::vector<std::unique_ptr<RunAgent>> players;
  players.push_back(std::make_unique<RunAgent>(config, config.seed));
  std::optional<std::uint8_t> player_count;

  MatchLoopResult result;
  while (true) {
    const Clock::time_point now = Clock::now();
    if (!player_count.has_value()) {
      player_count = PlayerCount(players.front()->GetAgent());
      while (player_count.has_value() && players.size() < *player_count) {
        players.push_back(std::make_unique<RunAgent>(config, config.seed + static_cast<std::uint32_t>(players.size())));
      }
    }
    const bool all_done = player_count.has_value() && std::ranges::all_of(players, [&](const auto& player) {
                            return player->GetReports().MatchesEnded() >= config.matches;
                          });
    if (all_done) {
      result.verdict = Verdict::kSucceeded;
      break;
    }
    if (const auto failed = std::ranges::find_if(
            players, [](const auto& player) { return player->GetReports().Failure().has_value(); });
        failed != players.end()) {
      result.verdict = Verdict::kFailed;
      result.failure = *(*failed)->GetReports().Failure();
      break;
    }
    if (now >= deadline) {
      result.verdict = Verdict::kTimedOut;
      break;
    }
    for (const std::unique_ptr<RunAgent>& player : players) {
      player->Decide(now);
    }
    std::this_thread::sleep_for(kDecideInterval);
  }
  for (const std::unique_ptr<RunAgent>& player : players) {
    result.agents.push_back(player->GetAgent().Stats().netcode);
  }
  return result;
}

}  // namespace augusta::agent::testing
