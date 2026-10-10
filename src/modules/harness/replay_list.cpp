#include <chrono>
#include <cstdint>
#include <expected>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "augusta/failure.h"
#include "augusta/faults.h"
#include "augusta/first_failure.h"
#include "augusta/harness.h"
#include "augusta/harness_wire.h"
#include "augusta/networking.h"
#include "augusta/protocol.h"

namespace augusta::harness {

namespace {

constexpr std::uint32_t kSecondsPerMinute = 60;

}  // namespace

struct ReplayListQuery::Impl {
  networking::Endpoint server;
  networking::Client network;
  bool requested = false;
  std::optional<std::vector<ReplayListing>> list;
  std::optional<JoinRefusal> refusal;
  failure::FirstFailure transport_failure;

  Impl(networking::Endpoint endpoint, failure::Faults* faults) : server(std::move(endpoint)), network(faults) {}

  // Takes in one message the server sent: its list or its refusal; anything
  // else a replay server sends no one who asked for its list.
  void Receive(const networking::Payload& payload) {
    const auto decoded = protocol::Decode(payload);
    if (!decoded.has_value()) {
      return;
    }
    if (const auto* answered = std::get_if<protocol::ReplayListWire>(&*decoded)) {
      std::vector<ReplayListing> listings;
      listings.reserve(answered->replays.size());
      for (const protocol::ReplayListingWire& listing : answered->replays) {
        listings.push_back(FromWire(listing));
      }
      list = std::move(listings);
    } else if (const auto* refused = std::get_if<protocol::JoinRefusedWire>(&*decoded)) {
      refusal = FromWire(refused->reason);
    }
  }
};

ReplayListQuery::ReplayListQuery(const networking::Endpoint& server, failure::Faults* faults)
    : impl_(std::make_unique<Impl>(server, faults)) {
  impl_->network.Connect(impl_->server);
}

ReplayListQuery::~ReplayListQuery() = default;

void ReplayListQuery::Pump() {
  Impl& impl = *impl_;
  impl.network.PumpEvents();
  if (!impl.requested && impl.network.GetState() == networking::ConnectionState::kConnected) {
    auto payload = protocol::Encode(protocol::ReplayListRequestWire{});
    // A message with no field always encodes.
    if (networking::SendResult sent = impl.network.Send(*payload, networking::Reliability::kReliable);
        !sent.has_value()) {
      impl.transport_failure.Record(std::move(sent.error()));
    }
    impl.requested = true;
  }
  auto received = impl.network.ReceiveMessages();
  if (!received.has_value()) {
    impl.transport_failure.Record(std::move(received.error()));
    return;
  }
  for (const networking::Payload& payload : *received) {
    impl.Receive(payload);
  }
}

const std::optional<std::vector<ReplayListing>>& ReplayListQuery::List() const { return impl_->list; }

std::optional<Failure> ReplayListQuery::GetFailure() const {
  if (impl_->list.has_value()) {
    return std::nullopt;
  }
  if (impl_->refusal.has_value()) {
    return Failure{.kind = FailureKind::kRefused, .refusal = *impl_->refusal};
  }
  if (impl_->network.GetState() != networking::ConnectionState::kDisconnected) {
    return std::nullopt;
  }
  return Failure{.kind = FailureKind::kServerUnreachable, .refusal = {}};
}

std::optional<failure::Failure> ReplayListQuery::TakeTransportFailure() { return impl_->transport_failure.Take(); }

std::string DescribeReplayList(const std::vector<ReplayListing>& listings) {
  if (listings.empty()) {
    return "the server replays no capture";
  }
  std::string lines;
  for (const ReplayListing& listing : listings) {
    const auto seconds = listing.tick_rate_hz == 0 ? 0 : listing.ticks / listing.tick_rate_hz;
    std::string characters;
    for (const std::string& character : listing.characters) {
      characters += (characters.empty() ? "" : ", ") + character;
    }
    lines += std::format("{}{}  started {:%Y-%m-%d %H:%M:%S} UTC  lasted {}:{:02}  {}", lines.empty() ? "" : "\n",
                         listing.name, std::chrono::floor<std::chrono::seconds>(listing.started),
                         seconds / kSecondsPerMinute, seconds % kSecondsPerMinute, characters);
  }
  return lines;
}

}  // namespace augusta::harness
