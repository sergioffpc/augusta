#include "augusta/harness.h"

#include <cstdint>
#include <deque>
#include <expected>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <variant>

#include "augusta/logging.h"

namespace augusta::harness {

struct Session::Impl {
  networking::Endpoint server;
  std::string engine_version;
  networking::Client network;
  prediction::World prediction;
  // Network I/O thread only.
  bool sent_join_request = false;

  // What the server has told this client, written by the Network I/O thread as
  // it answers and read from any; also the commands the Prediction thread
  // still waits to see acknowledged.
  mutable std::mutex mutex;
  std::optional<protocol::SessionId> session_id;
  std::optional<protocol::JoinRefusal> refusal;
  std::optional<protocol::AuthoritativeState> authoritative;
  std::deque<protocol::SequencedCommand> unacknowledged;
  // Sequences start at 1; 0 means none.
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
    const std::lock_guard<std::mutex> lock(mutex);
    if (const auto* accepted = std::get_if<protocol::JoinAccepted>(&*decoded)) {
      session_id = accepted->session;
      LI("subsystem=clientruntime event=joined");
    } else if (const auto* refused = std::get_if<protocol::JoinRefused>(&*decoded)) {
      refusal = refused->reason;
      LI("subsystem=clientruntime event=join_refused reason=\"{}\"", protocol::DescribeJoinRefusal(refused->reason));
    } else if (const auto* state = std::get_if<protocol::AuthoritativeState>(&*decoded)) {
      TakeInState(*state);
    } else {
      LW("subsystem=clientruntime event=dropped bytes={} reason=\"not a server message\"", payload.size());
    }
  }

  // Keeps state if it is newer than the one held (unreliable delivery can
  // reorder), and forgets the commands it acknowledges. Holds mutex.
  void TakeInState(const protocol::AuthoritativeState& state) {
    if (authoritative.has_value() && state.tick <= authoritative->tick) {
      return;
    }
    authoritative = state;
    while (!unacknowledged.empty() && unacknowledged.front().sequence <= state.acknowledged_sequence) {
      unacknowledged.pop_front();
    }
  }

  // The number the next command goes to the server under, or 0 while the
  // server has not admitted this client and there is nobody to send to.
  std::uint32_t TakeSequence() {
    const std::lock_guard<std::mutex> lock(mutex);
    return session_id.has_value() ? next_sequence++ : 0;
  }

  // What the newest state from the server says about this client's own player.
  std::optional<prediction::Acknowledgement> OwnAcknowledgement() {
    const std::lock_guard<std::mutex> lock(mutex);
    if (!session_id.has_value() || !authoritative.has_value()) {
      return std::nullopt;
    }
    for (const protocol::PlayerState& player : authoritative->players) {
      if (player.session == *session_id) {
        return prediction::Acknowledgement{.sequence = authoritative->acknowledged_sequence, .body = player.body};
      }
    }
    return std::nullopt;
  }

  // Sends command under sequence with the commands still unacknowledged.
  void SendCommand(std::uint32_t sequence, const input::Command& command) {
    protocol::Commands message;
    {
      const std::lock_guard<std::mutex> lock(mutex);
      unacknowledged.push_back(protocol::SequencedCommand{.sequence = sequence, .command = command});
      if (unacknowledged.size() > protocol::kMaxCommandsPerMessage) {
        unacknowledged.pop_front();
      }
      message.commands.assign(unacknowledged.begin(), unacknowledged.end());
    }
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

std::optional<protocol::SessionId> Session::GetSessionId() const {
  const std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->session_id;
}

std::optional<protocol::JoinRefusal> Session::GetRefusal() const {
  const std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->refusal;
}

std::optional<protocol::AuthoritativeState> Session::GetAuthoritativeState() const {
  const std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->authoritative;
}

prediction::State Session::Tick(const input::Command& command, float delta_time) {
  Impl& impl = *impl_;
  const std::uint32_t sequence = impl.TakeSequence();
  const prediction::State state = impl.prediction.Tick(command, sequence, impl.OwnAcknowledgement(), delta_time);
  if (sequence != 0) {
    impl.SendCommand(sequence, command);
  }
  return state;
}

}  // namespace augusta::harness
