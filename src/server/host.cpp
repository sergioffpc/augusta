#include "host.h"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <mutex>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

#include "augusta/input.h"
#include "augusta/logging.h"
#include "augusta/protocol.h"
#include "augusta/version.h"
#include "match.h"

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

// The transport's handle as a number, for log lines.
std::uint32_t PeerNumber(networking::PeerId peer) { return static_cast<std::uint32_t>(peer); }

}  // namespace

struct Host::Impl {
  // Declared before the socket so it is constructed first; see BuildSimulation.
  simulation::World simulation;
  networking::Server network;
  // Network I/O thread only.
  Match match{std::string(EngineVersion())};

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

  void Reply(networking::PeerId peer, const protocol::Message& message) {
    network.Send(peer, protocol::Encode(message), networking::Reliability::kReliable);
  }

  void HandleJoinRequest(networking::PeerId peer, const protocol::JoinRequest& request) {
    const auto session = match.Join(peer, request.engine_version);
    if (!session.has_value()) {
      LI("subsystem=serverruntime event=join_refused peer={} reason=\"{}\"", PeerNumber(peer),
         protocol::DescribeJoinRefusal(session.error()));
      Reply(peer, protocol::JoinRefused{.reason = session.error()});
      return;
    }
    LI("subsystem=serverruntime event=joined peer={} players={}", PeerNumber(peer), match.PlayerCount());
    Reply(peer, protocol::JoinAccepted{.session = *session});
  }

  void HandleMessage(const networking::PeerMessage& message) {
    const std::expected<protocol::Message, protocol::DecodeError> decoded = protocol::Decode(message.payload);
    if (!decoded.has_value()) {
      LW("subsystem=serverruntime event=dropped peer={} bytes={} reason=\"{}\"", PeerNumber(message.from),
         message.payload.size(), protocol::DescribeDecodeError(decoded.error()));
      return;
    }
    if (const auto* request = std::get_if<protocol::JoinRequest>(&*decoded)) {
      HandleJoinRequest(message.from, *request);
      return;
    }
    LW("subsystem=serverruntime event=dropped peer={} bytes={} reason=\"not a client message\"",
       PeerNumber(message.from), message.payload.size());
  }
};

Host::Host(const HostConfig& config) : impl_(std::make_unique<Impl>(config)) {}

Host::~Host() = default;

void Host::PumpNetwork() {
  Impl& impl = *impl_;
  for (const networking::PeerEvent& event : impl.network.PumpEvents()) {
    switch (event.type) {
      case networking::PeerEventType::kConnectRequested:
        // Every connection is accepted, since a refusal is a message and needs
        // the connection to travel on; whether the peer joins the match is
        // decided by its JoinRequest. TODO(sergioffpc): input validation and
        // any join policy (US-15) is not yet a module of its own.
        impl.network.Accept(event.peer);
        break;
      case networking::PeerEventType::kConnected:
        break;
      case networking::PeerEventType::kDisconnected:
        impl.match.Leave(event.peer);
        break;
    }
  }
  for (const networking::PeerMessage& message : impl.network.ReceiveMessages()) {
    LT("subsystem=serverruntime event=received peer={} bytes={}", PeerNumber(message.from), message.payload.size());
    impl.HandleMessage(message);
  }
}

simulation::State Host::Tick(float delta_time) {
  const std::vector<input::Command> commands = impl_->GetLatestCommands();
  return impl_->simulation.Tick(commands, delta_time);
}

}  // namespace augusta::server
