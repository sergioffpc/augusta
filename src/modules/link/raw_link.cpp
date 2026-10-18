#include "augusta/raw_link.h"

#include <expected>
#include <memory>
#include <utility>
#include <vector>

#include "augusta/failure.h"
#include "augusta/link.h"
#include "augusta/networking.h"
#include "transport_names.h"

namespace augusta::link {

struct RawClient::Impl {
  networking::Client network;
};

RawClient::RawClient() : impl_(std::make_unique<Impl>()) {}

RawClient::~RawClient() = default;

void RawClient::Connect(const Endpoint& server) {
  impl_->network.Connect(networking::Endpoint{.address = server.address});
}

void RawClient::Disconnect() { impl_->network.Disconnect(); }

void RawClient::PumpEvents() { impl_->network.PumpEvents(); }

ConnectionState RawClient::GetState() const { return ToLink(impl_->network.GetState()); }

SendResult RawClient::Send(const RawPayload& payload) {
  return ToLink(impl_->network.Send(payload, networking::Reliability::kReliable));
}

std::expected<std::vector<RawPayload>, failure::Failure> RawClient::Receive() {
  return impl_->network.ReceiveMessages();
}

struct RawServer::Impl {
  explicit Impl(const Endpoint& local) : network(networking::Endpoint{.address = local.address}) {}

  networking::Server network;
};

RawServer::RawServer(const Endpoint& local) : impl_(std::make_unique<Impl>(local)) {}

RawServer::~RawServer() = default;

Endpoint RawServer::LocalEndpoint() const { return Endpoint{.address = impl_->network.LocalEndpoint().address}; }

std::vector<PeerEvent> RawServer::PumpEvents() {
  std::vector<PeerEvent> events;
  for (const networking::PeerEvent& event : impl_->network.PumpEvents()) {
    events.push_back(ToLink(event));
  }
  return events;
}

void RawServer::Accept(PeerId peer) { impl_->network.Accept(ToTransport(peer)); }

void RawServer::Disconnect(PeerId peer) { impl_->network.Disconnect(ToTransport(peer)); }

SendResult RawServer::Send(PeerId peer, const RawPayload& payload) {
  return ToLink(impl_->network.Send(ToTransport(peer), payload, networking::Reliability::kReliable));
}

std::expected<std::vector<RawPeerMessage>, failure::Failure> RawServer::Receive() {
  auto received = impl_->network.ReceiveMessages();
  if (!received.has_value()) {
    return std::unexpected(std::move(received.error()));
  }
  std::vector<RawPeerMessage> messages;
  messages.reserve(received->size());
  for (networking::PeerMessage& message : *received) {
    messages.push_back(RawPeerMessage{.from = ToLink(message.from), .payload = std::move(message.payload)});
  }
  return messages;
}

}  // namespace augusta::link
