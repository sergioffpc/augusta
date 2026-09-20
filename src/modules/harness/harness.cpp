#include "augusta/harness.h"

#include <cstddef>
#include <memory>
#include <string_view>

#include "augusta/logging.h"

namespace augusta::harness {

struct Session::Impl {
  networking::Endpoint server;
  networking::Client network;
  prediction::World prediction;
  // Network I/O thread only.
  bool sent_hello = false;

  explicit Impl(const SessionConfig& config) : server(config.server), prediction(config.stamina) {}
};

Session::Session(const SessionConfig& config) : impl_(std::make_unique<Impl>(config)) {}

Session::~Session() = default;

void Session::Connect() { impl_->network.Connect(impl_->server); }

void Session::Disconnect() { impl_->network.Disconnect(); }

void Session::PumpEvents() { impl_->network.PumpEvents(); }

void Session::ExchangeMessages() {
  // TODO(sergioffpc): M1 spike only (issue #31) - a literal hello proving the
  // transport round-trips a message at all. Replace with the real join
  // handshake once the Networking Protocol (ADR-0007) exists.
  if (!impl_->sent_hello && impl_->network.GetState() == networking::ConnectionState::kConnected) {
    constexpr std::string_view kHello = "hello from augustac";
    const auto* bytes = reinterpret_cast<const std::byte*>(kHello.data());
    impl_->network.Send(networking::Payload(bytes, bytes + kHello.size()), networking::Reliability::kUnreliable);
    impl_->sent_hello = true;
  }
  for ([[maybe_unused]] const networking::Payload& payload : impl_->network.ReceiveMessages()) {
    LT("subsystem=clientruntime event=received bytes={}", payload.size());
  }
}

networking::ConnectionState Session::GetState() const { return impl_->network.GetState(); }

std::optional<networking::ConnectionStats> Session::GetStats() const { return impl_->network.GetStats(); }

prediction::State Session::Tick(const input::Command& command, float delta_time) {
  // TODO(sergioffpc): no authoritative state to reconcile against yet -
  // deserializing one from network.ReceiveMessages() needs the Networking
  // Protocol (ADR-0007), not designed yet.
  return impl_->prediction.Tick(command, std::nullopt, delta_time);
}

}  // namespace augusta::harness
