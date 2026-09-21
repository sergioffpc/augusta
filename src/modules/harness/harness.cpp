#include "augusta/harness.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <expected>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "augusta/logging.h"

namespace augusta::harness {

// What the server has told this client. Immutable once published: the Network
// I/O thread makes a new one for each message that changes it, and the
// Prediction thread reads whichever is current.
struct ServerView {
  // The whole answer to the join request: the session, the spawn point, the
  // parameters then and who was already there.
  std::optional<protocol::JoinAccepted> accepted;
  // What the join carried at first, and what each reload since has replaced it with.
  std::optional<parameters::NumberedParameters> current_parameters;
  std::optional<protocol::JoinRefusal> refusal;
  std::optional<protocol::AuthoritativeState> authoritative;
};

struct Session::Impl {
  networking::Endpoint server;
  std::string engine_version;
  networking::Client network;
  prediction::World prediction;

  // Network I/O thread only.
  bool sent_join_request = false;

  // Whether the caller has asked for a connection and not since asked to end it:
  // an ended connection is only a failure while this is set.
  std::atomic<bool> wanted{false};

  // Written by the Network I/O thread alone, read from any.
  std::atomic<std::shared_ptr<const ServerView>> view{std::make_shared<const ServerView>()};

  // Prediction thread only: whether the prediction has been started at the
  // spawn point, under the server's stamina rules, once the server admitted this client.
  bool started = false;
  // Prediction thread only: the generation of the parameters the prediction runs on.
  std::uint32_t applied_generation = 0;
  // The commands still waiting to be acknowledged, and the sequence the next
  // one goes under. Sequences start at 1; 0 means none.
  std::deque<protocol::SequencedCommand> unacknowledged;
  std::uint32_t next_sequence = 1;
  // Network I/O thread only: a server can send messages that are refused as fast
  // as it likes, so their warnings are limited.
  logging::Throttle drop_warnings{std::chrono::seconds{1}};

  Impl(const SessionConfig& config, prediction::World world)
      : server(config.server), engine_version(config.engine_version), prediction(std::move(world)) {}

  void HandleMessage(const networking::Payload& payload) {
    const std::expected<protocol::Message, protocol::DecodeError> decoded = protocol::Decode(payload);
    if (!decoded.has_value()) {
      LW_LIMITED(drop_warnings, "subsystem=clientruntime event=dropped bytes={} reason=\"{}\"", payload.size(),
                 protocol::DescribeDecodeError(decoded.error()));
      return;
    }
    if (const auto* accepted = std::get_if<protocol::JoinAccepted>(&*decoded)) {
      OnJoinAccepted(*accepted);
    } else if (const auto* refused = std::get_if<protocol::JoinRefused>(&*decoded)) {
      OnJoinRefused(*refused);
    } else if (const auto* state = std::get_if<protocol::AuthoritativeState>(&*decoded)) {
      OnAuthoritativeState(*state);
    } else if (const auto* update = std::get_if<protocol::ParametersUpdate>(&*decoded)) {
      OnParametersUpdate(*update);
    } else {
      LW_LIMITED(drop_warnings, "subsystem=clientruntime event=dropped bytes={} reason=\"not a server message\"",
                 payload.size());
    }
  }

  // A server whose tick rate or parameters the simulation cannot run on (a rate
  // of zero would be divided by) is not a usable one: the message is dropped, as
  // a malformed one is, and the client stays unadmitted.
  void OnJoinAccepted(const protocol::JoinAccepted& accepted) {
    if (!parameters::IsValidTickRate(accepted.tick_rate_hz)) {
      LW_LIMITED(drop_warnings, "subsystem=clientruntime event=dropped reason=\"invalid tick rate\" tick_rate_hz={}",
                 accepted.tick_rate_hz);
      return;
    }
    if (const auto valid = parameters::Validate(accepted.parameters); !valid) {
      LW_LIMITED(drop_warnings, "subsystem=clientruntime event=dropped reason=\"invalid parameters\" parameter={}",
                 valid.error().path);
      return;
    }
    Publish([&](ServerView& next) {
      next.accepted = accepted;
      next.current_parameters =
          parameters::NumberedParameters{.generation = accepted.generation, .parameters = accepted.parameters};
    });
    LI("subsystem=clientruntime event=joined roster={} generation={}", accepted.roster.size(), accepted.generation);
  }

  // Logs why update was not taken: a late or repeated one is routine, since
  // nothing is ordered across a reload, and only traced; the rest are dropped
  // as a malformed message is.
  void LogRefused(const protocol::ParametersUpdate& update, const parameters::ReplacementError& error) {
    switch (error.reason) {
      case parameters::ReplacementRefusal::kNotNewer:
        LT("subsystem=clientruntime event=dropped generation={} reason=\"not newer\"", update.generation);
        break;
      case parameters::ReplacementRefusal::kInvalid:
        LW_LIMITED(drop_warnings,
                   "subsystem=clientruntime event=dropped generation={} reason=\"invalid parameters\" parameter={}",
                   update.generation, error.parameter);
        break;
    }
  }

  // Adopts update if parameters::CheckReplacement allows it; anything else is dropped and logged.
  void OnParametersUpdate(const protocol::ParametersUpdate& update) {
    const std::optional<parameters::NumberedParameters> held = view.load()->current_parameters;
    if (!held.has_value()) {
      LW_LIMITED(drop_warnings, "subsystem=clientruntime event=dropped reason=\"parameters before joining\"");
      return;
    }
    const parameters::NumberedParameters candidate{.generation = update.generation, .parameters = update.parameters};
    if (const auto allowed = parameters::CheckReplacement(*held, candidate); !allowed) {
      LogRefused(update, allowed.error());
      return;
    }
    Publish([&](ServerView& next) { next.current_parameters = candidate; });
    LI("subsystem=clientruntime event=parameters_adopted generation={}", update.generation);
  }

  void OnJoinRefused(const protocol::JoinRefused& refused) {
    Publish([&](ServerView& next) { next.refusal = refused.reason; });
    LI("subsystem=clientruntime event=join_refused reason=\"{}\"", protocol::DescribeJoinRefusal(refused.reason));
  }

  // Keeps state if it is newer than the one held (unreliable delivery can reorder).
  void OnAuthoritativeState(const protocol::AuthoritativeState& state) {
    const std::shared_ptr<const ServerView> current = view.load();
    if (current->authoritative.has_value() && state.tick <= current->authoritative->tick) {
      return;
    }
    Publish([&](ServerView& next) { next.authoritative = state; });
  }

  // Makes the next view from the current one changed by mutate, and publishes it.
  // Only the Network I/O thread publishes, so nothing can intervene between the load and the store.
  template <typename Mutate>
  void Publish(Mutate&& mutate) {
    auto next = std::make_shared<ServerView>(*view.load());
    mutate(*next);
    view.store(std::move(next));
  }

  // What the server's state says about this client's own player.
  static std::optional<prediction::Acknowledgement> OwnAcknowledgement(const ServerView& server_view) {
    if (!server_view.accepted.has_value() || !server_view.authoritative.has_value()) {
      return std::nullopt;
    }
    for (const protocol::PlayerState& player : server_view.authoritative->players) {
      if (player.session == server_view.accepted->session) {
        return prediction::Acknowledgement{
            .sequence = server_view.authoritative->acknowledged_sequence,
            .body = player.body,
        };
      }
    }
    return std::nullopt;
  }

  // Sends command under sequence with the commands server_view does not yet acknowledge.
  void SendCommand(const ServerView& server_view, std::uint32_t sequence, const input::Command& command) {
    // Commands the server has already processed need not go again.
    if (server_view.authoritative.has_value()) {
      while (!unacknowledged.empty() &&
             unacknowledged.front().sequence <= server_view.authoritative->acknowledged_sequence) {
        unacknowledged.pop_front();
      }
    }
    // Keeps at most the newest kMaxCommandsPerMessage, all a message can carry.
    unacknowledged.push_back(protocol::SequencedCommand{.sequence = sequence, .command = command});
    if (unacknowledged.size() > protocol::kMaxCommandsPerMessage) {
      unacknowledged.pop_front();
    }
    protocol::Commands message;
    message.commands.assign(unacknowledged.begin(), unacknowledged.end());
    network.Send(protocol::Encode(message), networking::Reliability::kUnreliable);
  }
};

std::string DescribeFailure(const Failure& failure) {
  switch (failure.kind) {
    case FailureKind::kRefused:
      return std::format("the server refused this client: {}", protocol::DescribeJoinRefusal(failure.refusal));
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

std::optional<Failure> Session::GetFailure() const {
  const std::shared_ptr<const ServerView> server_view = impl_->view.load();
  if (server_view->refusal.has_value()) {
    return Failure{.kind = FailureKind::kRefused, .refusal = *server_view->refusal};
  }
  if (!impl_->wanted.load() || impl_->network.GetState() != networking::ConnectionState::kDisconnected) {
    return std::nullopt;
  }
  const bool was_admitted = server_view->accepted.has_value();
  return Failure{.kind = was_admitted ? FailureKind::kConnectionLost : FailureKind::kServerUnreachable};
}

std::optional<networking::ConnectionStats> Session::GetStats() const { return impl_->network.GetStats(); }

std::optional<protocol::SessionId> Session::GetSessionId() const {
  const std::shared_ptr<const ServerView> server_view = impl_->view.load();
  if (!server_view->accepted.has_value()) {
    return std::nullopt;
  }
  return server_view->accepted->session;
}

std::vector<protocol::PlayerState> Session::GetRoster() const {
  const std::shared_ptr<const ServerView> server_view = impl_->view.load();
  return server_view->accepted.has_value() ? server_view->accepted->roster : std::vector<protocol::PlayerState>{};
}

std::optional<float> Session::GetTickRate() const {
  const std::shared_ptr<const ServerView> server_view = impl_->view.load();
  if (!server_view->accepted.has_value()) {
    return std::nullopt;
  }
  return server_view->accepted->tick_rate_hz;
}

std::optional<parameters::NumberedParameters> Session::GetParameters() const {
  return impl_->view.load()->current_parameters;
}

std::optional<protocol::JoinRefusal> Session::GetRefusal() const { return impl_->view.load()->refusal; }

std::optional<protocol::AuthoritativeState> Session::GetAuthoritativeState() const {
  return impl_->view.load()->authoritative;
}

prediction::State Session::Tick(const input::Command& command, float delta_time) {
  Impl& impl = *impl_;
  // One view for the whole tick, so the sequence, the reconciliation and the
  // commands sent all agree on what the server had said.
  const std::shared_ptr<const ServerView> server_view = impl.view.load();
  // Nobody to send to until the server has admitted this client, and until
  // then the prediction has neither its spawn point nor the server's rules.
  if (server_view->accepted.has_value() && !impl.started) {
    impl.prediction.Start(server_view->accepted->spawn, server_view->current_parameters->parameters);
    impl.started = true;
    impl.applied_generation = server_view->current_parameters->generation;
  } else if (impl.started && server_view->current_parameters->generation > impl.applied_generation) {
    // The server reloaded: the prediction goes on where it is, under the new rules.
    impl.prediction.SetParameters(server_view->current_parameters->parameters);
    impl.applied_generation = server_view->current_parameters->generation;
  }
  const std::uint32_t sequence = impl.started ? impl.next_sequence++ : 0;
  const prediction::State state =
      impl.prediction.Tick(command, sequence, Impl::OwnAcknowledgement(*server_view), delta_time);
  if (sequence != 0) {
    impl.SendCommand(*server_view, sequence, command);
  }
  return state;
}

}  // namespace augusta::harness
