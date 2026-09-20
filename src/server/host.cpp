#include "host.h"

#include <cstddef>
#include <format>
#include <mutex>
#include <stdexcept>
#include <string_view>
#include <vector>

#include "augusta/input.h"
#include "augusta/logging.h"

namespace augusta::server {

namespace {

// The authoritative world with the map's collision already in it. Built
// before the socket exists, so a map that is rejected never leaves a bound
// port behind.
simulation::World BuildSimulation(const HostConfig& config) {
  simulation::World simulation(config.stamina, config.script_path);
  for (const physics::StaticMesh& mesh : config.collision) {
    if (const auto added = simulation.AddStaticMesh(mesh); !added) {
      throw std::runtime_error(
          std::format("server::Host: map collision rejected: {}", physics::DescribeStaticMeshError(added.error())));
    }
  }
  return simulation;
}

}  // namespace

struct Host::Impl {
  // Declared before the socket so it is constructed first; see BuildSimulation.
  simulation::World simulation;
  networking::Server network;

  // Guards latest_commands: written by the Network I/O thread as client
  // commands arrive, read once per Simulation tick. Always empty today - see
  // ServerRuntime's header comment on the Networking Protocol gap.
  std::mutex commands_mutex;
  std::vector<input::Command> latest_commands;

  explicit Impl(const HostConfig& config) : simulation(BuildSimulation(config)), network(config.listen) {}

  std::vector<input::Command> GetLatestCommands() {
    const std::lock_guard<std::mutex> lock(commands_mutex);
    return latest_commands;
  }
};

Host::Host(const HostConfig& config) : impl_(std::make_unique<Impl>(config)) {}

Host::~Host() = default;

void Host::PumpNetwork() {
  networking::Server& network = impl_->network;
  for (const networking::PeerEvent& event : network.PumpEvents()) {
    if (event.type == networking::PeerEventType::kConnectRequested) {
      // TODO(sergioffpc): run Input Validation/any join policy (US-15) before
      // accepting - not yet a module of its own. Accepts unconditionally for
      // now.
      network.Accept(event.peer);
    }
  }
  // TODO(sergioffpc): M1 spike only (issue #31) - decode each received
  // PeerMessage's Payload into an input::Command and store it into
  // latest_commands instead of just echoing a literal hello back, once the
  // Networking Protocol (ADR-0007) exists.
  for (const networking::PeerMessage& message : network.ReceiveMessages()) {
    LT("subsystem=serverruntime event=received bytes={}", message.payload.size());
    constexpr std::string_view kHello = "hello from augustad";
    const auto* bytes = reinterpret_cast<const std::byte*>(kHello.data());
    network.Send(message.from, networking::Payload(bytes, bytes + kHello.size()), networking::Reliability::kUnreliable);
  }
}

simulation::State Host::Tick(float delta_time) {
  const std::vector<input::Command> commands = impl_->GetLatestCommands();
  return impl_->simulation.Tick(commands, delta_time);
}

}  // namespace augusta::server
