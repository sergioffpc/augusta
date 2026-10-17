#include "augusta/link.h"

#include <chrono>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include "augusta/failure.h"
#include "augusta/faults.h"
#include "augusta/link_messages.h"
#include "augusta/link_types.h"
#include "augusta/logging.h"
#include "augusta/networking.h"
#include "augusta/protocol.h"
#include "connection_stats.h"
#include "reliability.h"

namespace augusta::link {

namespace {

ConnectionState ToLink(networking::ConnectionState state) {
  switch (state) {
    case networking::ConnectionState::kConnecting:
      return ConnectionState::kConnecting;
    case networking::ConnectionState::kConnected:
      return ConnectionState::kConnected;
    case networking::ConnectionState::kDisconnected:
      return ConnectionState::kDisconnected;
  }
  // The transport has no other state.
  return ConnectionState::kDisconnected;
}

}  // namespace

struct Client::Impl {
  explicit Impl(failure::Faults* faults) : network(faults) {}

  // Encodes message and hands it to the transport with its type's reliability.
  SendResult Send(const protocol::MessageWire& message) {
    auto payload = EncodeToSend(message);
    if (!payload.has_value()) {
      return std::unexpected(std::move(payload.error()));
    }
    networking::SendResult sent = network.Send(*payload, ReliabilityOf(TypeOf(*payload)));
    if (!sent.has_value()) {
      return std::unexpected(std::move(sent.error()));
    }
    return *sent == networking::SendOutcome::kAccepted ? SendOutcome::kAccepted : SendOutcome::kDropped;
  }

  networking::Client network;
  // A server can send what is no message as fast as it likes, so the warnings
  // for dropping it are limited. Network I/O thread only, as Receive.
  logging::Throttle drop_warnings{std::chrono::seconds{1}};
};

Client::Client(failure::Faults* faults) : impl_(std::make_unique<Impl>(faults)) {}

Client::~Client() = default;

void Client::Connect(const Endpoint& server) {
  impl_->network.Connect(networking::Endpoint{.address = server.address});
}

void Client::Disconnect() { impl_->network.Disconnect(); }

void Client::PumpEvents() { impl_->network.PumpEvents(); }

ConnectionState Client::GetState() const { return ToLink(impl_->network.GetState()); }

std::optional<ConnectionStats> Client::GetStats() const {
  return impl_->network.GetStats().transform(
      [](const networking::ConnectionStats& stats) { return FromTransport(stats); });
}

SendResult Client::Send(const JoinRequest& request) { return impl_->Send(ToWire(request)); }

SendResult Client::Send(const ReplayRequest& request) { return impl_->Send(ToWire(request)); }

SendResult Client::Send(ReplayListRequest request) { return impl_->Send(ToWire(request)); }

SendResult Client::Send(Ready ready) { return impl_->Send(ToWire(ready)); }

SendResult Client::Send(std::span<const SequencedCommand> commands) { return impl_->Send(ToWire(commands)); }

std::expected<std::vector<ServerMessage>, failure::Failure> Client::Receive() {
  auto received = impl_->network.ReceiveMessages();
  if (!received.has_value()) {
    return std::unexpected(std::move(received.error()));
  }
  std::vector<ServerMessage> messages;
  messages.reserve(received->size());
  for (const networking::Payload& payload : *received) {
    LOG_TRACE("subsystem=link event=received bytes={}", payload.size());
    const std::expected<protocol::MessageWire, protocol::DecodeError> decoded = protocol::Decode(payload);
    if (!decoded.has_value()) {
      LOG_WARNING_LIMITED(impl_->drop_warnings, "subsystem=link event=dropped bytes={} reason=\"{}\"", payload.size(),
                          protocol::DescribeDecodeError(decoded.error()));
      continue;
    }
    std::optional<ServerMessage> message = ServerMessageFromWire(*decoded);
    if (!message.has_value()) {
      LOG_WARNING_LIMITED(impl_->drop_warnings, "subsystem=link event=dropped bytes={} reason=\"not a server message\"",
                          payload.size());
      continue;
    }
    messages.push_back(std::move(*message));
  }
  return messages;
}

}  // namespace augusta::link
