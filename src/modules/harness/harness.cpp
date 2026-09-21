#include "augusta/harness.h"

#include <atomic>
#include <cstdint>
#include <deque>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>

#include "augusta/logging.h"

namespace augusta::harness {

// What the server has told this client. Immutable once published: the Network
// I/O thread makes a new one for each message that changes it, and the
// Prediction thread reads whichever is current.
struct ServerView {
  std::optional<protocol::SessionId> session_id;
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

  // Written by the Network I/O thread alone, read from any.
  std::atomic<std::shared_ptr<const ServerView>> view{std::make_shared<const ServerView>()};

  // Prediction thread only: the commands still waiting to be acknowledged, and
  // the sequence the next one goes under. Sequences start at 1; 0 means none.
  std::deque<protocol::SequencedCommand> unacknowledged;
  std::uint32_t next_sequence = 1;

  Impl(const SessionConfig& config, prediction::World world)
      : server(config.server), engine_version(config.engine_version), prediction(std::move(world)) {}

  void HandleMessage(const networking::Payload& payload) {
    const std::expected<protocol::Message, protocol::DecodeError> decoded = protocol::Decode(payload);
    if (!decoded.has_value()) {
      LW("subsystem=clientruntime event=dropped bytes={} reason=\"{}\"", payload.size(),
         protocol::DescribeDecodeError(decoded.error()));
      return;
    }
    if (const auto* accepted = std::get_if<protocol::JoinAccepted>(&*decoded)) {
      OnJoinAccepted(*accepted);
    } else if (const auto* refused = std::get_if<protocol::JoinRefused>(&*decoded)) {
      OnJoinRefused(*refused);
    } else if (const auto* state = std::get_if<protocol::AuthoritativeState>(&*decoded)) {
      OnAuthoritativeState(*state);
    } else {
      LW("subsystem=clientruntime event=dropped bytes={} reason=\"not a server message\"", payload.size());
    }
  }

  // The OnXxx handlers below each take in one kind of server message, and run on
  // the Network I/O thread.

  void OnJoinAccepted(const protocol::JoinAccepted& accepted) {
    Publish([&](ServerView& next) { next.session_id = accepted.session; });
    LI("subsystem=clientruntime event=joined");
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
    if (!server_view.session_id.has_value() || !server_view.authoritative.has_value()) {
      return std::nullopt;
    }
    for (const protocol::PlayerState& player : server_view.authoritative->players) {
      if (player.session == *server_view.session_id) {
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
    if (server_view.authoritative.has_value()) {
      while (!unacknowledged.empty() &&
             unacknowledged.front().sequence <= server_view.authoritative->acknowledged_sequence) {
        unacknowledged.pop_front();
      }
    }
    unacknowledged.push_back(protocol::SequencedCommand{.sequence = sequence, .command = command});
    if (unacknowledged.size() > protocol::kMaxCommandsPerMessage) {
      unacknowledged.pop_front();
    }
    protocol::Commands message;
    message.commands.assign(unacknowledged.begin(), unacknowledged.end());
    network.Send(protocol::Encode(message), networking::Reliability::kUnreliable);
  }
};

Session::Session(const SessionConfig& config, prediction::World prediction)
    : impl_(std::make_unique<Impl>(config, std::move(prediction))) {}

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

std::optional<protocol::SessionId> Session::GetSessionId() const { return impl_->view.load()->session_id; }

std::optional<protocol::JoinRefusal> Session::GetRefusal() const { return impl_->view.load()->refusal; }

std::optional<protocol::AuthoritativeState> Session::GetAuthoritativeState() const {
  return impl_->view.load()->authoritative;
}

prediction::State Session::Tick(const input::Command& command, float delta_time) {
  Impl& impl = *impl_;
  // One view for the whole tick, so the sequence, the reconciliation and the
  // commands sent all agree on what the server had said.
  const std::shared_ptr<const ServerView> server_view = impl.view.load();
  // Nobody to send to until the server has admitted this client.
  const std::uint32_t sequence = server_view->session_id.has_value() ? impl.next_sequence++ : 0;
  const prediction::State state =
      impl.prediction.Tick(command, sequence, Impl::OwnAcknowledgement(*server_view), delta_time);
  if (sequence != 0) {
    impl.SendCommand(*server_view, sequence, command);
  }
  return state;
}

}  // namespace augusta::harness
