#include "augusta/harness.h"

#include <atomic>
#include <cstdint>
#include <expected>
#include <format>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "augusta/command.h"
#include "augusta/failure.h"
#include "augusta/failure_latch.h"
#include "augusta/link.h"
#include "augusta/link_types.h"
#include "augusta/logging.h"
#include "augusta/physics.h"
#include "augusta/prediction.h"
#include "augusta/tick.h"
#include "command_stream.h"
#include "inbox.h"

namespace augusta::harness {

struct Session::Impl {
  link::Endpoint server;
  link::JoinRequest join_request;
  // Set for a Replay viewer, which asks with it instead of join_request (ADR-0051).
  std::optional<link::ReplayRequest> replay_request;
  link::Client channel;

  // Network I/O thread only.
  bool sent_join_request = false;

  // Whether the caller has asked for a connection and not since asked to end it:
  // an ended connection is only a failure while this is set.
  std::atomic<bool> wanted{false};

  // What the server sent, received on the Network I/O thread and read from any.
  Inbox inbox;
  // The first local transport failure any thread's send or receive met, until taken.
  failure::FailureLatch transport_failure;
  // Prediction thread only.
  CommandStream commands;

  // The first message any thread could not encode, which was not sent, until taken.
  failure::FailureLatch invariant_failure;

  Impl(const SessionConfig& config, prediction::World world)
      : server(config.server),
        join_request{
            .engine_version = config.engine_version,
            .client_pack = config.client_pack,
            .character = config.character,
            .spawn = config.spawn,
            .run = config.run,
        },
        replay_request(config.replay.transform([&config](const std::string& capture) {
          return link::ReplayRequest{.engine_version = config.engine_version,
                                     .client_pack = config.client_pack,
                                     .capture = capture,
                                     .run = config.run};
        })),
        channel(config.transport),
        inbox(config.replay.has_value(), config.on_match),
        commands(std::move(world)) {}

  // Whether either failure has been recorded, taken or not: the runtime has
  // failed, and the Session sends nothing more. Checked before each send, so
  // one already under way on another thread may still go out.
  [[nodiscard]] bool Failed() const { return transport_failure.Recorded() || invariant_failure.Recorded(); }

  // Sends message to the server over the Link; returns whether the Link
  // accepted it. A message the protocol cannot carry is not sent: it is kept in
  // invariant_failure. A local transport failure is kept in transport_failure.
  // Once either is, the runtime has failed (Failed), and nothing more is sent.
  template <typename Message>
  bool Send(const Message& message) {
    if (Failed()) {
      return false;
    }
    link::SendResult sent = channel.Send(message);
    if (!sent.has_value()) {
      failure::FailureLatch& kept =
          sent.error().code == failure::Code::kInvariantViolated ? invariant_failure : transport_failure;
      kept.Record(std::move(sent.error()));
      return false;
    }
    return *sent == link::SendOutcome::kAccepted;
  }
};

std::string_view DescribeJoinRefusal(JoinRefusal reason) {
  switch (reason) {
    case JoinRefusal::kVersionMismatch:
      return "client version does not match the server";
    case JoinRefusal::kLobbyFull:
      return "the lobby is full, or the replay server runs all the Replays it may";
    case JoinRefusal::kUnknownCharacter:
      return "the server's scenario has no such character";
    case JoinRefusal::kMatchInProgress:
      return "a match is in progress: try again once it ends";
    case JoinRefusal::kPackMismatch:
      return "client pack does not match the server's";
    case JoinRefusal::kReplayServer:
      return "the server is a replay server: watch a capture with --replay, or list them with --replays";
    case JoinRefusal::kNotAReplayServer:
      return "the server is a live server, not a replay server: join it without --replays or --replay";
    case JoinRefusal::kUnknownCapture:
      return "the replay server replays no such capture: list them with --replays";
    case JoinRefusal::kReenactmentsNotAccepted:
      return "the server does not take reenactments: it needs a Development server with debug.reenactments on";
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
  impl_->channel.Connect(impl_->server);
  // After, not before: until the transport is connecting its state is still
  // the disconnected one it starts in, which would read as a failure.
  impl_->wanted.store(true);
}

void Session::Disconnect() {
  impl_->wanted.store(false);
  impl_->channel.Disconnect();
}

void Session::PumpEvents() { impl_->channel.PumpEvents(); }

void Session::ExchangeMessages() {
  Impl& impl = *impl_;
  if (!impl.sent_join_request && impl.channel.GetState() == link::ConnectionState::kConnected) {
    // Once, whatever the transport did with it: reliable delivery is the
    // transport's, and a send it dropped or refused ends the connection or the runtime.
    if (impl.replay_request.has_value()) {
      impl.Send(*impl.replay_request);
    } else {
      impl.Send(impl.join_request);
    }
    impl.sent_join_request = true;
  }
  auto received = impl.channel.Receive();
  if (!received.has_value()) {
    impl.transport_failure.Record(std::move(received.error()));
    return;
  }
  for (link::ServerMessage& message : *received) {
    impl.inbox.Receive(std::move(message));
  }
}

std::shared_ptr<const ServerView> Session::GetServerView() const { return impl_->inbox.View(); }

link::ConnectionState Session::GetConnectionState() const { return impl_->channel.GetState(); }

std::optional<Failure> Session::GetFailure() const {
  const std::shared_ptr<const ServerView> server_view = impl_->inbox.View();
  if (server_view->refusal.has_value()) {
    return Failure{.kind = FailureKind::kRefused, .refusal = *server_view->refusal};
  }
  if (!impl_->wanted.load() || impl_->channel.GetState() != link::ConnectionState::kDisconnected || ReplayEnded()) {
    return std::nullopt;
  }
  const bool was_admitted = server_view->accepted.has_value();
  return Failure{.kind = was_admitted ? FailureKind::kConnectionLost : FailureKind::kServerUnreachable};
}

bool Session::ReplayEnded() const {
  return impl_->replay_request.has_value() && impl_->inbox.View()->match_end.has_value() &&
         impl_->channel.GetState() == link::ConnectionState::kDisconnected;
}

std::optional<failure::Failure> Session::TakeTransportFailure() { return impl_->transport_failure.Take(); }

std::optional<failure::Failure> Session::TakeInvariantFailure() { return impl_->invariant_failure.Take(); }

std::optional<link::ConnectionStats> Session::GetConnectionStats() const { return impl_->channel.GetStats(); }

void Session::ReportReady(std::uint32_t version) {
  const std::shared_ptr<const ServerView> server_view = impl_->inbox.View();
  if (!server_view->lobby.has_value() || server_view->lobby->version != version) {
    return;
  }
  if (impl_->Send(link::Ready{.version = version})) {
    LOG_DEBUG("subsystem=harness event=ready version={}", version);
  }
}

#if AUGUSTA_DEBUG_TOOLS
void Session::SubscribeDebug(std::uint8_t flags) {
  if (impl_->Send(link::DebugSubscribe{.flags = flags})) {
    LOG_DEBUG("subsystem=harness event=debug_subscribed flags={:#04x}", flags);
  }
}
#endif

std::vector<Shot> Session::TakeShots(const ServerView& view) { return impl_->inbox.TakeShots(view.matches_started); }

std::vector<HitConfirmation> Session::TakeHitConfirmations(const ServerView& view) {
  return impl_->inbox.TakeHitConfirmations(view.matches_started);
}

std::vector<Death> Session::TakeDeaths(const ServerView& view) { return impl_->inbox.TakeDeaths(view.matches_started); }

#if AUGUSTA_DEBUG_TOOLS
std::vector<ShotJudgement> Session::TakeShotJudgements(const ServerView& view) {
  return impl_->inbox.TakeShotJudgements(view.matches_started);
}
#endif

tick::Sequence Session::NextSequence() const { return impl_->commands.NextSequence(); }

#if AUGUSTA_DEBUG_TOOLS
void Session::SetVisualization(const physics::Visualization& visualization) {
  impl_->commands.SetVisualization(visualization);
}
#endif

prediction::State Session::Tick(const command::Command& command, float delta_time) {
  Impl& impl = *impl_;
  // A Replay viewer has no body to predict and no Command the server would take.
  if (impl.replay_request.has_value()) {
    return prediction::State{};
  }
  // One view for the whole tick, so the sequence, the reconciliation and the
  // commands sent all agree on what the server had said.
  const CommandTick tick = impl.commands.Tick(*impl.inbox.View(), command, delta_time);
  if (!tick.send.empty()) {
    // Not accepted, the commands are sent again next tick, as unacknowledged ones always are.
    impl.Send(std::span<const link::SequencedCommand>(tick.send));
  }
  return tick.state;
}

}  // namespace augusta::harness
