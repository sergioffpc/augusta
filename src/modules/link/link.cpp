#include "augusta/link.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <string>
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
#include "peer_message.h"
#include "reliability.h"
#include "transport_names.h"

namespace augusta::link {

std::expected<void, failure::Failure> Init(failure::Faults* faults) { return networking::Init(faults); }

void Shutdown() { networking::Shutdown(); }

void SimulateNetworkConditions(const SimulatedConditions& conditions) {
  networking::SimulateNetworkConditions(networking::SimulatedConditions{
      .latency_ms = conditions.latency_ms,
      .jitter_mean_ms = conditions.jitter_mean_ms,
      .jitter_max_ms = conditions.jitter_max_ms,
      .loss_percent = conditions.loss_percent,
      .reorder_percent = conditions.reorder_percent,
      .reorder_delay_ms = conditions.reorder_delay_ms,
      .timeout_ms = conditions.timeout_ms,
      .applies_to = conditions.applies_to == SimulatedTraffic::kSentAndReceived
                        ? networking::SimulatedTraffic::kSentAndReceived
                        : networking::SimulatedTraffic::kSent,
  });
}

namespace {

// One type's count one way, kept atomic so any thread reads it without a lock
// while the Network I/O thread and a sender add to it. Each field is read on
// its own, so a read during a count may see the message without its bytes for
// that one moment, which a reader taking rates does not notice.
class AtomicCount {
 public:
  void Add(std::size_t bytes) {
    messages_.fetch_add(1, std::memory_order_relaxed);
    bytes_.fetch_add(bytes, std::memory_order_relaxed);
  }

  void Reset() {
    messages_.store(0, std::memory_order_relaxed);
    bytes_.store(0, std::memory_order_relaxed);
  }

  [[nodiscard]] MessageCount Read() const {
    return MessageCount{.messages = messages_.load(std::memory_order_relaxed),
                        .bytes = bytes_.load(std::memory_order_relaxed)};
  }

 private:
  std::atomic<std::uint64_t> messages_{0};
  std::atomic<std::uint64_t> bytes_{0};
};

}  // namespace

struct Client::Impl {
  explicit Impl(failure::Faults* faults) : network(faults) {}

  // Encodes message and hands it to the transport with its type's reliability,
  // counting it if the transport took it.
  SendResult Send(const protocol::MessageWire& message) {
    auto payload = EncodeToSend(message);
    if (!payload.has_value()) {
      return std::unexpected(std::move(payload.error()));
    }
    const MessageType type = TypeOf(*payload);
    SendResult sent = ToLink(network.Send(*payload, ReliabilityOf(type)));
    if (sent.has_value() && *sent == SendOutcome::kAccepted) {
      CountOf(sent_counts, type).Add(payload->size());
    }
    return sent;
  }

  static AtomicCount& CountOf(std::array<AtomicCount, kMessageTypeCount>& counts, MessageType type) {
    return counts.at(static_cast<std::size_t>(type));
  }

  void ResetTraffic() {
    for (std::size_t index = 0; index < kMessageTypeCount; ++index) {
      sent_counts.at(index).Reset();
      received_counts.at(index).Reset();
    }
  }

  [[nodiscard]] Traffic ReadTraffic() const {
    Traffic traffic;
    for (std::size_t index = 0; index < kMessageTypeCount; ++index) {
      traffic.by_type.at(index) =
          TypeTraffic{.sent = sent_counts.at(index).Read(), .received = received_counts.at(index).Read()};
    }
    return traffic;
  }

  // payload as what the server said, or nullopt, with a warning, if it is no
  // message or one only a client sends.
  std::optional<ServerMessage> TakeIn(const networking::Payload& payload) {
    LOG_TRACE("subsystem=link event=received bytes={}", payload.size());
    const std::expected<protocol::MessageWire, protocol::DecodeError> decoded = protocol::Decode(payload);
    if (!decoded.has_value()) {
      LOG_WARN_LIMITED(drop_warnings, "subsystem=link event=dropped bytes={} reason=\"{}\"", payload.size(),
                       protocol::DescribeDecodeError(decoded.error()));
      return std::nullopt;
    }
    std::optional<ServerMessage> message = ServerMessageFromWire(*decoded);
    if (!message.has_value()) {
      LOG_WARN_LIMITED(drop_warnings, "subsystem=link event=dropped bytes={} reason=\"not a server message\"",
                       payload.size());
      return std::nullopt;
    }
    CountOf(received_counts, link::TypeOf(*decoded)).Add(payload.size());
    return message;
  }

  networking::Client network;
  // What the connection carried since Connect, indexed by MessageType.
  std::array<AtomicCount, kMessageTypeCount> sent_counts;
  std::array<AtomicCount, kMessageTypeCount> received_counts;
  // A server can send what is no message as fast as it likes, so the warnings
  // for dropping it are limited. Network I/O thread only, as Receive.
  logging::Throttle drop_warnings{std::chrono::seconds{1}};
};

Client::Client(failure::Faults* faults) : impl_(std::make_unique<Impl>(faults)) {}

Client::~Client() = default;

void Client::Connect(const Endpoint& server) {
  impl_->ResetTraffic();
  impl_->network.Connect(networking::Endpoint{.address = server.address});
}

void Client::Disconnect() { impl_->network.Disconnect(); }

void Client::PumpEvents() { impl_->network.PumpEvents(); }

ConnectionState Client::GetState() const { return ToLink(impl_->network.GetState()); }

std::optional<ConnectionStats> Client::GetStats() const {
  return impl_->network.GetStats().transform(
      [](const networking::ConnectionStats& stats) { return FromTransport(stats); });
}

Traffic Client::GetTraffic() const { return impl_->ReadTraffic(); }

SendResult Client::Send(const JoinRequest& request) { return impl_->Send(ToWire(request)); }

SendResult Client::Send(const ReplayRequest& request) { return impl_->Send(ToWire(request)); }

SendResult Client::Send(ReplayListRequest request) { return impl_->Send(ToWire(request)); }

SendResult Client::Send(Ready ready) { return impl_->Send(ToWire(ready)); }

#if AUGUSTA_DEBUG_TOOLS
SendResult Client::Send(DebugSubscribe subscribe) { return impl_->Send(ToWire(subscribe)); }
#endif

SendResult Client::Send(std::span<const SequencedCommand> commands) { return impl_->Send(ToWire(commands)); }

std::expected<std::vector<ServerMessage>, failure::Failure> Client::Receive() {
  auto received = impl_->network.ReceiveMessages();
  if (!received.has_value()) {
    return std::unexpected(std::move(received.error()));
  }
  std::vector<ServerMessage> messages;
  messages.reserve(received->size());
  for (const networking::Payload& payload : *received) {
    if (std::optional<ServerMessage> message = impl_->TakeIn(payload); message.has_value()) {
      messages.push_back(std::move(*message));
    }
  }
  return messages;
}

// The one way to make an Outgoing: the Link's own encoding.
struct OutgoingAccess {
  static std::expected<Outgoing, failure::Failure> Make(const protocol::MessageWire& message) {
    auto payload = EncodeToSend(message);
    if (!payload.has_value()) {
      return std::unexpected(std::move(payload.error()));
    }
    const MessageType type = TypeOf(*payload);
    return Outgoing(type, *std::move(payload));
  }

  static std::expected<Outgoing, failure::Failure> Readdressed(const Outgoing& state,
                                                               const AuthoritativeState& recipient) {
    const auto broken = [&state](std::string detail) {
      return failure::Failure{.code = failure::Code::kInvariantViolated,
                              .context = {{.key = "message_type", .value = std::string(NameOf(state.type_))}},
                              .detail = std::move(detail)};
    };
    if (state.type_ != MessageType::kAuthoritativeState) {
      return std::unexpected(broken("only an authoritative state is readdressed"));
    }
    // The recipient's own fields alone are converted: the bodies stay the bytes they are.
    protocol::AuthoritativeStateWire own;
    FillRecipientFields(own, recipient);
    std::vector<std::byte> payload = state.payload_;
    if (const std::expected<void, protocol::EncodeError> readdressed = protocol::Readdress(payload, own);
        !readdressed.has_value()) {
      return std::unexpected(broken(std::string(protocol::DescribeEncodeError(readdressed.error()))));
    }
    return Outgoing(state.type_, std::move(payload));
  }
};

std::expected<Outgoing, failure::Failure> Encode(const Admission& admission) {
  return OutgoingAccess::Make(ToWire(admission));
}

std::expected<Outgoing, failure::Failure> Encode(JoinRefusal refusal) {
  return OutgoingAccess::Make(protocol::JoinRefusedWire{.reason = ToWire(refusal)});
}

std::expected<Outgoing, failure::Failure> Encode(const Lobby& lobby) { return OutgoingAccess::Make(ToWire(lobby)); }

std::expected<Outgoing, failure::Failure> Encode(const MatchStart& start) {
  return OutgoingAccess::Make(ToWire(start));
}

std::expected<Outgoing, failure::Failure> Encode(const MatchEnd& end) { return OutgoingAccess::Make(ToWire(end)); }

std::expected<Outgoing, failure::Failure> Encode(const AuthoritativeState& state) {
  return OutgoingAccess::Make(ToWire(state));
}

std::expected<Outgoing, failure::Failure> Encode(const Shot& shot) { return OutgoingAccess::Make(ToWire(shot)); }

std::expected<Outgoing, failure::Failure> Encode(const HitConfirmation& hit) {
  return OutgoingAccess::Make(ToWire(hit));
}

std::expected<Outgoing, failure::Failure> Encode(const Death& death) { return OutgoingAccess::Make(ToWire(death)); }

std::expected<Outgoing, failure::Failure> Encode(const ReplayView& view) { return OutgoingAccess::Make(ToWire(view)); }

std::expected<Outgoing, failure::Failure> Encode(const std::vector<ReplayListing>& listings) {
  return OutgoingAccess::Make(ToWire(listings));
}

#if AUGUSTA_DEBUG_TOOLS
std::expected<Outgoing, failure::Failure> Encode(const ShotJudgement& judgement) {
  return OutgoingAccess::Make(ToWire(judgement));
}
#endif

std::expected<Outgoing, failure::Failure> Readdressed(const Outgoing& state, const AuthoritativeState& recipient) {
  return OutgoingAccess::Readdressed(state, recipient);
}

std::vector<ReplayListing> ListedOf(const std::vector<ReplayListing>& listings) {
  std::vector<ReplayListing> listed;
  // Newest last, as listings orders them, so the oldest are the ones left out.
  for (const ReplayListing& listing : std::views::reverse(listings)) {
    if (listed.size() == protocol::kMaxReplayListings) {
      break;
    }
    // A name no Replay request could carry is no capture a viewer can watch.
    if (listing.name.size() > protocol::kMaxCaptureNameLength || !protocol::IsUtf8(listing.name)) {
      continue;
    }
    listed.push_back(listing);
  }
  std::ranges::reverse(listed);
  return listed;
}

PeerMessage ReadFrom(PeerId from, std::span<const std::byte> payload) {
  PeerMessage message{.from = from, .bytes = payload.size(), .type = {}, .message = std::nullopt};
  const std::expected<protocol::MessageWire, protocol::DecodeError> decoded = protocol::Decode(payload);
  if (!decoded.has_value()) {
    message.type = std::unexpected(std::string(protocol::DescribeDecodeError(decoded.error())));
    return message;
  }
  message.type = link::TypeOf(*decoded);
  message.message = ClientMessageFromWire(*decoded);
  return message;
}

struct Server::Impl {
  Impl(const Endpoint& local, failure::Faults* faults)
      : network(networking::Endpoint{.address = local.address}, faults) {}

  networking::Server network;
};

Server::Server(const Endpoint& local, failure::Faults* faults) : impl_(std::make_unique<Impl>(local, faults)) {}

Server::~Server() = default;

Endpoint Server::LocalEndpoint() const { return Endpoint{.address = impl_->network.LocalEndpoint().address}; }

std::vector<PeerEvent> Server::PumpEvents() {
  std::vector<PeerEvent> events;
  for (const networking::PeerEvent& event : impl_->network.PumpEvents()) {
    events.push_back(ToLink(event));
  }
  return events;
}

void Server::Accept(PeerId peer) { impl_->network.Accept(ToTransport(peer)); }

void Server::Disconnect(PeerId peer) { impl_->network.Disconnect(ToTransport(peer)); }

SendResult Server::Send(PeerId peer, const Outgoing& message) {
  return ToLink(impl_->network.Send(ToTransport(peer), message.payload_, ReliabilityOf(message.type_)));
}

std::expected<std::vector<PeerMessage>, failure::Failure> Server::Receive() {
  auto received = impl_->network.ReceiveMessages();
  if (!received.has_value()) {
    return std::unexpected(std::move(received.error()));
  }
  std::vector<PeerMessage> messages;
  messages.reserve(received->size());
  for (const networking::PeerMessage& message : *received) {
    messages.push_back(ReadFrom(ToLink(message.from), message.payload));
  }
  return messages;
}

std::vector<PeerStats> Server::GetStats() const {
  std::vector<PeerStats> stats;
  for (const networking::PeerStats& peer : impl_->network.GetStats()) {
    stats.push_back(PeerStats{.peer = ToLink(peer.peer), .stats = FromTransport(peer.stats)});
  }
  return stats;
}

}  // namespace augusta::link
