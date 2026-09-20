#include "host.h"

#include <cstddef>
#include <mutex>
#include <string_view>
#include <vector>

#include "augusta/input.h"
#include "augusta/logging.h"

namespace augusta::server {

struct Host::Impl {
  networking::Server network;
  simulation::World simulation;

  // Guards latest_commands: written by the Network I/O thread as client
  // commands arrive, read once per Simulation tick. Always empty today - see
  // ServerRuntime's header comment on the Networking Protocol gap.
  std::mutex commands_mutex;
  std::vector<input::Command> latest_commands;

  explicit Impl(const HostConfig& config) : network(config.listen), simulation(config.stamina, config.script_path) {}

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
