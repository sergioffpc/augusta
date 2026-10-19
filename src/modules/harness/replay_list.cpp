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
#include "augusta/failure_latch.h"
#include "augusta/faults.h"
#include "augusta/harness.h"
#include "augusta/link.h"
#include "augusta/link_types.h"

namespace augusta::harness {

namespace {

constexpr std::uint32_t kSecondsPerMinute = 60;

}  // namespace

struct ReplayListQuery::Impl {
  link::Endpoint server;
  link::Client channel;
  bool requested = false;
  std::optional<std::vector<ReplayListing>> list;
  std::optional<JoinRefusal> refusal;
  failure::FailureLatch transport_failure;

  Impl(link::Endpoint endpoint, failure::Faults* faults) : server(std::move(endpoint)), channel(faults) {}

  // Takes in one message the server sent: its list or its refusal; anything
  // else a replay server sends no one who asked for its list.
  void Receive(link::ServerMessage& message) {
    if (auto* answered = std::get_if<std::vector<ReplayListing>>(&message)) {
      list = std::move(*answered);
    } else if (const auto* refused = std::get_if<JoinRefusal>(&message)) {
      refusal = *refused;
    }
  }
};

ReplayListQuery::ReplayListQuery(const link::Endpoint& server, failure::Faults* faults)
    : impl_(std::make_unique<Impl>(server, faults)) {
  impl_->channel.Connect(impl_->server);
}

ReplayListQuery::~ReplayListQuery() = default;

void ReplayListQuery::Pump() {
  Impl& impl = *impl_;
  impl.channel.PumpEvents();
  if (!impl.requested && impl.channel.GetState() == link::ConnectionState::kConnected) {
    // A message with no field always encodes, so only the transport can fail it.
    if (link::SendResult sent = impl.channel.Send(link::ReplayListRequest{}); !sent.has_value()) {
      impl.transport_failure.Record(std::move(sent.error()));
    }
    impl.requested = true;
  }
  auto received = impl.channel.Receive();
  if (!received.has_value()) {
    impl.transport_failure.Record(std::move(received.error()));
    return;
  }
  for (link::ServerMessage& message : *received) {
    impl.Receive(message);
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
  if (impl_->channel.GetState() != link::ConnectionState::kDisconnected) {
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
