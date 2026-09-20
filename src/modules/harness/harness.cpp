#include "augusta/harness.h"

#include <expected>
#include <format>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <variant>

#include "augusta/logging.h"

namespace augusta::harness {

struct Session::Impl {
  networking::Endpoint server;
  std::string engine_version;
  networking::Client network;
  prediction::World prediction;
  // Network I/O thread only.
  bool sent_join_request = false;

  // Written by the Network I/O thread as the server answers, read from any.
  mutable std::mutex join_mutex;
  std::optional<protocol::SessionId> session_id;
  std::optional<protocol::JoinRefusal> refusal;

  explicit Impl(const SessionConfig& config)
      : server(config.server), engine_version(config.engine_version), prediction(config.stamina) {
    for (const physics::StaticMesh& mesh : config.collision) {
      if (const auto added = prediction.AddStaticMesh(mesh); !added) {
        throw std::runtime_error(std::format("harness::Session: map collision rejected: {}",
                                             physics::DescribeStaticMeshError(added.error())));
      }
    }
  }

  void HandleMessage(const networking::Payload& payload) {
    const std::expected<protocol::Message, protocol::DecodeError> decoded = protocol::Decode(payload);
    if (!decoded.has_value()) {
      LW("subsystem=clientruntime event=dropped bytes={} reason=\"{}\"", payload.size(),
         protocol::DescribeDecodeError(decoded.error()));
      return;
    }
    const std::lock_guard<std::mutex> lock(join_mutex);
    if (const auto* accepted = std::get_if<protocol::JoinAccepted>(&*decoded)) {
      session_id = accepted->session;
      LI("subsystem=clientruntime event=joined");
    } else if (const auto* refused = std::get_if<protocol::JoinRefused>(&*decoded)) {
      refusal = refused->reason;
      LI("subsystem=clientruntime event=join_refused reason=\"{}\"", protocol::DescribeJoinRefusal(refused->reason));
    } else {
      LW("subsystem=clientruntime event=dropped bytes={} reason=\"not a server message\"", payload.size());
    }
  }
};

Session::Session(const SessionConfig& config) : impl_(std::make_unique<Impl>(config)) {}

Session::~Session() = default;

void Session::Connect() { impl_->network.Connect(impl_->server); }

void Session::Disconnect() { impl_->network.Disconnect(); }

void Session::PumpEvents() { impl_->network.PumpEvents(); }

void Session::ExchangeMessages() {
  Impl& impl = *impl_;
  if (!impl.sent_join_request && impl.network.GetState() == networking::ConnectionState::kConnected) {
    impl.network.Send(protocol::Encode(protocol::JoinRequest{.engine_version = impl.engine_version}),
                      networking::Reliability::kReliable);
    impl.sent_join_request = true;
  }
  for (const networking::Payload& payload : impl.network.ReceiveMessages()) {
    LT("subsystem=clientruntime event=received bytes={}", payload.size());
    impl.HandleMessage(payload);
  }
}

networking::ConnectionState Session::GetState() const { return impl_->network.GetState(); }

std::optional<networking::ConnectionStats> Session::GetStats() const { return impl_->network.GetStats(); }

std::optional<protocol::SessionId> Session::GetSessionId() const {
  const std::lock_guard<std::mutex> lock(impl_->join_mutex);
  return impl_->session_id;
}

std::optional<protocol::JoinRefusal> Session::GetRefusal() const {
  const std::lock_guard<std::mutex> lock(impl_->join_mutex);
  return impl_->refusal;
}

prediction::State Session::Tick(const input::Command& command, float delta_time) {
  // TODO(sergioffpc): no authoritative state to reconcile against yet -
  // deserializing one from network.ReceiveMessages() needs the Networking
  // Protocol (ADR-0007), not designed yet.
  return impl_->prediction.Tick(command, std::nullopt, delta_time);
}

}  // namespace augusta::harness
