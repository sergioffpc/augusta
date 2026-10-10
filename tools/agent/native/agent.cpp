#include "agent.h"

#include <expected>
#include <filesystem>
#include <format>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "augusta/assets.h"
#include "augusta/command.h"
#include "augusta/failure.h"
#include "augusta/harness.h"
#include "augusta/map.h"
#include "augusta/physics.h"
#include "augusta/prediction.h"
#include "augusta/runner.h"

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

std::expected<LoadedPack, std::string> LoadPack(const std::filesystem::path& pack_path,
                                                const std::filesystem::path& public_key_path) {
  // Verified before anything connects, as augustac does (ADR-0018).
  auto pack = assets::LoadVerifiedPack(pack_path, public_key_path);
  if (!pack) {
    return std::unexpected(assets::DescribeVerifiedPackError(pack.error(), pack_path, public_key_path));
  }
  auto map = map::LoadCollision(*pack);
  if (!map) {
    return std::unexpected(map::DescribeMapError(map.error()));
  }
  return LoadedPack{.hash = pack->Hash(), .map = *std::move(map)};
}

Agent::Agent(const std::string& server, const LoadedPack& pack, const std::string& character)
    : session_(harness::SessionConfig{.server = {.address = server}, .client_pack = pack.hash, .character = character},
               WorldWithMap(pack.map)) {
  runner_.emplace(session_,
                  harness::RunnerHooks{
                      .next_command = [this] { return intents_.NextCommand(*session_.GetServerView(), Own()); },
                      .on_tick =
                          [this](const harness::PredictedTick& tick) {
                            const std::scoped_lock lock(own_mutex_);
                            own_ = tick.state.local_body;
                          },
                      .on_network_round = [this] { GetReady(); },
                  });
}

void Agent::SetRaw(const command::Command& command) { intents_.SetRaw(command); }

AgentView Agent::View() const { return AgentView{.server = session_.GetServerView(), .own = Own()}; }

std::optional<std::string> Agent::Failure() const {
  if (const auto worker = runner_->Failure(); worker.has_value()) {
    return failure::DescribeFailure(*worker);
  }
  if (const auto session = session_.GetFailure(); session.has_value()) {
    return harness::DescribeFailure(*session);
  }
  return std::nullopt;
}

physics::BodyState Agent::Own() const {
  const std::scoped_lock lock(own_mutex_);
  return own_;
}

void Agent::GetReady() {
  const std::shared_ptr<const harness::ServerView> view = session_.GetServerView();
  if (view->GetPhase() != harness::Phase::kLobby || !view->lobby.has_value() ||
      ready_version_ == view->lobby->version) {
    return;
  }
  session_.ReportReady(view->lobby->version);
  ready_version_ = view->lobby->version;
}

}  // namespace augusta::agent
