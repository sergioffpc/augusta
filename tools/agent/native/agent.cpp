#include "agent.h"

#include <cstdint>
#include <expected>
#include <filesystem>
#include <format>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "augusta/assets.h"
#include "augusta/command.h"
#include "augusta/failure.h"
#include "augusta/harness.h"
#include "augusta/intent.h"
#include "augusta/map.h"
#include "augusta/physics.h"
#include "augusta/prediction.h"
#include "augusta/runner.h"
#include "report.h"

namespace augusta::agent {

namespace {

prediction::World WorldWithMap(const std::vector<physics::CollisionMesh>& map) {
  prediction::World world;
  if (auto added = map::AddCollision(world, map); !added) {
    throw std::runtime_error(
        std::format("augusta_agent: map collision rejected: {}", physics::DescribeCollisionMeshError(added.error())));
  }
  return world;
}

}  // namespace

std::expected<LoadedPack, LoadPackError> LoadPack(const std::filesystem::path& pack_path,
                                                  const std::filesystem::path& public_key_path) {
  // Verified before anything connects, as augustac does (ADR-0018).
  auto pack = assets::LoadVerifiedPack(pack_path, public_key_path);
  if (!pack) {
    return std::unexpected(pack.error());
  }
  auto map = map::LoadCollision(*pack);
  if (!map) {
    return std::unexpected(map.error());
  }
  return LoadedPack{.hash = pack->Hash(), .map = *std::move(map)};
}

std::string DescribeLoadPackError(const LoadPackError& error, const std::filesystem::path& pack_path,
                                  const std::filesystem::path& public_key_path) {
  if (const auto* verified = std::get_if<assets::VerifiedPackError>(&error)) {
    return assets::DescribeVerifiedPackError(*verified, pack_path, public_key_path);
  }
  return map::DescribeMapError(std::get<map::MapError>(error));
}

Agent::Agent(const std::string& server, const LoadedPack& pack, const std::string& character, Readiness readiness,
             ReportSink report)
    : readiness_(readiness),
      report_(std::move(report)),
      session_(harness::SessionConfig{.server = {.address = server}, .client_pack = pack.hash, .character = character},
               WorldWithMap(pack.map)) {
  runner_.emplace(
      session_,
      harness::RunnerHooks{
          .next_command = [this] { return intents_.NextCommand(*session_.GetServerView(), OwnBody()); },
          .on_tick =
              [this](const harness::PredictedTick& tick) {
                const std::scoped_lock lock(own_mutex_);
                own_ = tick.state.local_body;
              },
          .on_network_round = [this] { AfterNetworkRound(); },
          .on_failure = [this](const failure::Failure& failure) { ReportFailure(failure::DescribeFailure(failure)); },
      });
}

std::uint64_t Agent::SetRaw(const command::Command& command) {
  const std::uint64_t intent = next_intent_.fetch_add(1);
  intents_.SetRaw(command, [this, intent](harness::IntentEnding ending) {
    report_(IntentEnded{.intent = intent, .ending = ending});
  });
  return intent;
}

void Agent::Ready() {
  const std::shared_ptr<const harness::ServerView> view = session_.GetServerView();
  if (view->GetPhase() == harness::Phase::kLobby && view->lobby.has_value()) {
    session_.ReportReady(view->lobby->version);
  }
}

AgentView Agent::View() const { return AgentView{.server = session_.GetServerView(), .own = OwnBody()}; }

physics::BodyState Agent::OwnBody() const {
  const std::scoped_lock lock(own_mutex_);
  return own_;
}

void Agent::AfterNetworkRound() {
  const std::shared_ptr<const harness::ServerView> view = session_.GetServerView();
  if (readiness_ == Readiness::kForEveryRoster) {
    ReportReadyForNewRoster(*view);
  }
  ReportEvents(*view);
  if (const auto failed = session_.GetFailure(); failed.has_value()) {
    ReportFailure(harness::DescribeFailure(*failed));
  }
}

void Agent::ReportReadyForNewRoster(const harness::ServerView& view) {
  if (view.GetPhase() != harness::Phase::kLobby || !view.lobby.has_value() || ready_version_ == view.lobby->version) {
    return;
  }
  session_.ReportReady(view.lobby->version);
  ready_version_ = view.lobby->version;
}

// In the order they come in a Match: its start, what happens in it (the Deaths
// that end it among them), its end.
void Agent::ReportEvents(const harness::ServerView& view) {
  if (view.match_start.has_value() && started_reported_ != view.matches_started) {
    started_reported_ = view.matches_started;
    report_(*view.match_start);
  }
  for (harness::Shot& shot : session_.TakeShots(view)) {
    report_(shot);
  }
  for (harness::HitConfirmation& hit : session_.TakeHitConfirmations(view)) {
    report_(hit);
  }
  for (harness::Death& death : session_.TakeDeaths(view)) {
    report_(death);
  }
  if (!view.in_match && view.match_end.has_value() && ended_reported_ != view.matches_started) {
    ended_reported_ = view.matches_started;
    report_(*view.match_end);
  }
}

void Agent::ReportFailure(std::string description) {
  if (!failure_reported_.exchange(true)) {
    report_(AgentFailure{.description = std::move(description)});
  }
}

}  // namespace augusta::agent
