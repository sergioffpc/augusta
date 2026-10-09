#include "augusta/harness.h"

#include <atomic>
#include <cstdint>
#include <expected>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "augusta/command.h"
#include "augusta/failure.h"
#include "augusta/first_failure.h"
#include "augusta/harness_wire.h"
#include "augusta/logging.h"
#include "augusta/networking.h"
#include "augusta/parameters.h"
#include "augusta/prediction.h"
#include "augusta/protocol.h"
#include "command_stream.h"
#include "inbox.h"

namespace augusta::harness {

struct Session::Impl {
  networking::Endpoint server;
  JoinRequest join_request;
  networking::Client network;

  // Network I/O thread only.
  bool sent_join_request = false;

  // Whether the caller has asked for a connection and not since asked to end it:
  // an ended connection is only a failure while this is set.
  std::atomic<bool> wanted{false};

  // What the server sent, received on the Network I/O thread and read from any.
  Inbox inbox;
  // The first local transport failure any thread's send or receive met, until taken.
  failure::FirstFailure transport_failure;
  // Prediction thread only.
  CommandStream commands;

  // The first message any thread could not encode, which was not sent, until taken.
  failure::FirstFailure invariant_failure;

  Impl(const SessionConfig& config, prediction::World world)
      : server(config.server),
        join_request{
            .engine_version = config.engine_version,
            .client_pack = config.client_pack,
            .character = config.character,
            .spawn = config.spawn,
        },
        network(config.faults),
        commands(std::move(world)) {}

  // Sends message to the server as reliability says; returns whether the
  // transport accepted it. A message the protocol cannot carry is not sent:
  // it is kept in invariant_failure. A local transport failure is kept in
  // transport_failure.
  bool Send(const protocol::MessageWire& message, networking::Reliability reliability) {
    auto payload = EncodeToSend(message);
    if (!payload.has_value()) {
      invariant_failure.Record(std::move(payload.error()));
      return false;
    }
    networking::SendResult sent = network.Send(*payload, reliability);
    if (!sent.has_value()) {
      transport_failure.Record(std::move(sent.error()));
      return false;
    }
    return *sent == networking::SendOutcome::kAccepted;
  }
};

std::string_view DescribeJoinRefusal(JoinRefusal reason) {
  switch (reason) {
    case JoinRefusal::kVersionMismatch:
      return "client version does not match the server";
    case JoinRefusal::kLobbyFull:
      return "the lobby is full";
    case JoinRefusal::kUnknownCharacter:
      return "the server's scenario has no such character";
    case JoinRefusal::kMatchInProgress:
      return "a match is in progress: try again once it ends";
    case JoinRefusal::kPackMismatch:
      return "client pack does not match the server's";
    case JoinRefusal::kReenactmentsNotAccepted:
      return "the server does not take reenactments: it needs simulation.reenactments on";
  }
  return "unknown refusal";
}

std::string DescribeFailure(const Failure& failure) {
  switch (failure.kind) {
    case FailureKind::kRefused:
      return std::format("the server refused this client: {}", DescribeJoinRefusal(failure.refusal));
    case FailureKind::kServerUnreachable:
      return "could not connect to the server: check its address, and that it is running";
    case FailureKind::kConnectionLost:
      return "lost the connection to the server";
  }
  return "the session ended for an unknown reason";
}

Session::Session(const SessionConfig& config, prediction::World prediction)
    : impl_(std::make_unique<Impl>(config, std::move(prediction))) {}

Session::~Session() = default;

void Session::Connect() {
  impl_->network.Connect(impl_->server);
  // After, not before: until the transport is connecting its state is still
  // the disconnected one it starts in, which would read as a failure.
  impl_->wanted.store(true);
}

void Session::Disconnect() {
  impl_->wanted.store(false);
  impl_->network.Disconnect();
}

void Session::PumpEvents() { impl_->network.PumpEvents(); }

void Session::ExchangeMessages() {
  Impl& impl = *impl_;
  if (!impl.sent_join_request && impl.network.GetState() == networking::ConnectionState::kConnected) {
    // Once, whatever the transport did with it: reliable delivery is the
    // transport's, and a send it dropped or refused ends the connection or the runtime.
    impl.Send(ToWire(impl.join_request), networking::Reliability::kReliable);
    impl.sent_join_request = true;
  }
  auto received = impl.network.ReceiveMessages();
  if (!received.has_value()) {
    impl.transport_failure.Record(std::move(received.error()));
    return;
  }
  for (const networking::Payload& payload : *received) {
    LT("subsystem=harness event=received bytes={}", payload.size());
    impl.inbox.Receive(payload);
  }
}

std::shared_ptr<const ServerView> Session::GetServerView() const { return impl_->inbox.View(); }

networking::ConnectionState Session::GetConnectionState() const { return impl_->network.GetState(); }

std::optional<Failure> Session::GetFailure() const {
  const std::shared_ptr<const ServerView> server_view = impl_->inbox.View();
  if (server_view->refusal.has_value()) {
    return Failure{.kind = FailureKind::kRefused, .refusal = *server_view->refusal};
  }
  if (!impl_->wanted.load() || impl_->network.GetState() != networking::ConnectionState::kDisconnected) {
    return std::nullopt;
  }
  const bool was_admitted = server_view->accepted.has_value();
  return Failure{.kind = was_admitted ? FailureKind::kConnectionLost : FailureKind::kServerUnreachable};
}

std::optional<failure::Failure> Session::TakeTransportFailure() { return impl_->transport_failure.Take(); }

std::optional<failure::Failure> Session::TakeInvariantFailure() { return impl_->invariant_failure.Take(); }

std::optional<networking::ConnectionStats> Session::GetConnectionStats() const { return impl_->network.GetStats(); }

std::optional<SessionId> Session::GetSessionId() const {
  const std::shared_ptr<const ServerView> server_view = impl_->inbox.View();
  if (!server_view->accepted.has_value()) {
    return std::nullopt;
  }
  return server_view->accepted->session;
}

Phase Session::GetPhase() const { return impl_->inbox.View()->GetPhase(); }

std::optional<Lobby> Session::GetLobby() const { return impl_->inbox.View()->lobby; }

std::optional<MatchStart> Session::GetMatchStart() const { return impl_->inbox.View()->match_start; }

std::optional<MatchEnd> Session::GetMatchEnd() const { return impl_->inbox.View()->match_end; }

void Session::ReportReady(std::uint32_t version) {
  const std::shared_ptr<const ServerView> server_view = impl_->inbox.View();
  if (!server_view->lobby.has_value() || server_view->lobby->version != version) {
    return;
  }
  if (impl_->Send(protocol::ReadyWire{.version = version}, networking::Reliability::kReliable)) {
    LD("subsystem=harness event=ready version={}", version);
  }
}

std::optional<std::uint8_t> Session::GetTickRate() const {
  const std::shared_ptr<const ServerView> server_view = impl_->inbox.View();
  if (!server_view->accepted.has_value()) {
    return std::nullopt;
  }
  return server_view->accepted->tick_rate_hz;
}

std::optional<parameters::Parameters> Session::GetParameters() const {
  const std::shared_ptr<const ServerView> server_view = impl_->inbox.View();
  if (!server_view->accepted.has_value()) {
    return std::nullopt;
  }
  return server_view->accepted->parameters;
}

std::optional<JoinRefusal> Session::GetRefusal() const { return impl_->inbox.View()->refusal; }

std::optional<AuthoritativeState> Session::GetAuthoritativeState() const { return impl_->inbox.View()->authoritative; }

std::vector<Shot> Session::TakeShots() { return TakeShots(*GetServerView()); }

std::vector<Shot> Session::TakeShots(const ServerView& view) { return impl_->inbox.TakeShots(view.matches_started); }

std::vector<HitConfirmation> Session::TakeHitConfirmations() { return TakeHitConfirmations(*GetServerView()); }

std::vector<HitConfirmation> Session::TakeHitConfirmations(const ServerView& view) {
  return impl_->inbox.TakeHitConfirmations(view.matches_started);
}

std::vector<Death> Session::TakeDeaths() { return TakeDeaths(*GetServerView()); }

std::vector<Death> Session::TakeDeaths(const ServerView& view) { return impl_->inbox.TakeDeaths(view.matches_started); }

bool Session::IsAlive() const { return impl_->inbox.View()->OwnAlive(); }

std::optional<float> Session::GetHealth() const {
  const std::shared_ptr<const ServerView> server_view = impl_->inbox.View();
  if (!server_view->authoritative.has_value()) {
    return std::nullopt;
  }
  return server_view->authoritative->health;
}

std::optional<EntityId> Session::GetEntityId() const { return impl_->inbox.View()->OwnEntity(); }

command::Sequence Session::NextSequence() const { return impl_->commands.NextSequence(); }

prediction::State Session::Tick(const command::Command& command, float delta_time) {
  Impl& impl = *impl_;
  // One view for the whole tick, so the sequence, the reconciliation and the
  // commands sent all agree on what the server had said.
  const CommandTick tick = impl.commands.Tick(*impl.inbox.View(), command, delta_time);
  if (!tick.send.empty()) {
    // Not accepted, the commands are sent again next tick, as unacknowledged ones always are.
    impl.Send(ToWire(tick.send), networking::Reliability::kUnreliable);
  }
  return tick.state;
}

}  // namespace augusta::harness
