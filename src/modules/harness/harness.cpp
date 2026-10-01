#include "augusta/harness.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <expected>
#include <format>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "augusta/ballistics.h"
#include "augusta/command.h"
#include "augusta/harness_wire.h"
#include "augusta/logging.h"
#include "augusta/math.h"
#include "augusta/networking.h"
#include "augusta/parameters.h"
#include "augusta/prediction.h"
#include "augusta/protocol.h"

namespace augusta::harness {

namespace {

// Whether session is one of start's players.
bool IsInMatch(const MatchStart& start, SessionId session) {
  return std::ranges::any_of(start.players, [&](const MatchPlayer& player) { return player.session == session; });
}

// Whether entity is the body of one of start's players.
bool IsInMatch(const MatchStart& start, EntityId entity) {
  return std::ranges::any_of(start.players, [&](const MatchPlayer& player) { return player.entity == entity; });
}

// The body session's player controls in start, if it is in it.
std::optional<EntityId> EntityOf(const MatchStart& start, SessionId session) {
  for (const MatchPlayer& player : start.players) {
    if (player.session == session) {
      return player.entity;
    }
  }
  return std::nullopt;
}

std::string_view BodyPartName(ballistics::BodyPart part) {
  switch (part) {
    case ballistics::BodyPart::kHead:
      return "head";
    case ballistics::BodyPart::kTorso:
      return "torso";
    case ballistics::BodyPart::kLimb:
      return "limb";
  }
  std::unreachable();
}

// What the server sent that is handed out once, not read: kept, oldest first
// and no more than the newest limit of it, until whoever draws it takes it.
// Safe to use from any thread, since the Network I/O thread adds and another takes.
template <typename Event>
class Pending {
 public:
  explicit Pending(std::size_t limit) : limit_(limit) {}

  void Add(const Event& event) {
    const std::lock_guard<std::mutex> lock(mutex_);
    events_.push_back(event);
    if (events_.size() > limit_) {
      events_.pop_front();
    }
  }

  std::vector<Event> Take() {
    const std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Event> taken(events_.begin(), events_.end());
    events_.clear();
    return taken;
  }

  void Clear() {
    const std::lock_guard<std::mutex> lock(mutex_);
    events_.clear();
  }

 private:
  std::size_t limit_;
  std::mutex mutex_;
  std::deque<Event> events_;
};

}  // namespace

// What the server has told this client. Immutable once published: the Network
// I/O thread makes a new one for each message that changes it, and the
// Prediction thread reads whichever is current.
struct ServerView {
  // The whole answer to a join request: the session, the tick rate and the
  // parameters, all the server's for the whole run.
  std::optional<Admission> accepted;
  std::optional<JoinRefusal> refusal;
  std::optional<Lobby> lobby;
  // The last match's start, and how many have started: a new count is a new
  // match for the prediction to start over in.
  std::optional<MatchStart> match_start;
  std::uint32_t matches_started = 0;
  bool in_match = false;
  // How the last match ended, until the next starts.
  std::optional<MatchEnd> match_end;
  // Only while in_match.
  std::optional<AuthoritativeState> authoritative;
  // The bodies of the match in progress whose Death has been told.
  std::vector<EntityId> dead;
};

struct Session::Impl {
  networking::Endpoint server;
  JoinRequest join_request;
  networking::Client network;
  prediction::World prediction;

  // Network I/O thread only.
  bool sent_join_request = false;

  // Whether the caller has asked for a connection and not since asked to end it:
  // an ended connection is only a failure while this is set.
  std::atomic<bool> wanted{false};

  // Written by the Network I/O thread alone, read from any.
  std::atomic<std::shared_ptr<const ServerView>> view{std::make_shared<const ServerView>()};

  // Prediction thread only: which match the prediction was last started over
  // in (ServerView::matches_started), 0 for none, and what it last predicted.
  std::uint32_t started_match = 0;
  prediction::State last_state{};
  // The commands still waiting to be acknowledged, and the sequence the next
  // one goes under. Sequences start at 1; 0 means none.
  std::deque<SequencedCommand> unacknowledged;
  std::uint32_t next_sequence = 1;
  // Network I/O thread only: a server can send messages that are refused as fast
  // as it likes, so their warnings are limited.
  logging::Throttle drop_warnings{std::chrono::seconds{1}};

  // The Shots and the Hit confirmations received and not yet taken. Not part
  // of the view: they are handed out once, not read.
  Pending<Shot> shots{kMaxPendingShots};
  Pending<HitConfirmation> hit_confirmations{kMaxPendingHitConfirmations};
  Pending<Death> deaths{kMaxPendingDeaths};

  Impl(const SessionConfig& config, prediction::World world)
      : server(config.server),
        join_request{
            .engine_version = config.engine_version, .client_pack = config.client_pack, .character = config.character},
        prediction(std::move(world)) {}

  void HandleMessage(const networking::Payload& payload) {
    const std::expected<protocol::MessageWire, protocol::DecodeError> decoded = protocol::Decode(payload);
    if (!decoded.has_value()) {
      LW_LIMITED(drop_warnings, "subsystem=harness event=dropped bytes={} reason=\"{}\"", payload.size(),
                 protocol::DescribeDecodeError(decoded.error()));
      return;
    }
    if (!TakeIn(*decoded)) {
      LW_LIMITED(drop_warnings, "subsystem=harness event=dropped bytes={} reason=\"not a server message\"",
                 payload.size());
    }
  }

  // Hands message to what takes in its kind; false if it is not one a server sends.
  bool TakeIn(const protocol::MessageWire& message) {
    if (const auto* accepted = std::get_if<protocol::JoinAcceptedWire>(&message)) {
      OnJoinAccepted(FromWire(*accepted));
    } else if (const auto* refused = std::get_if<protocol::JoinRefusedWire>(&message)) {
      OnJoinRefused(FromWire(refused->reason));
    } else if (const auto* state = std::get_if<protocol::AuthoritativeStateWire>(&message)) {
      OnAuthoritativeState(FromWire(*state));
    } else if (const auto* shot = std::get_if<protocol::ShotWire>(&message)) {
      OnShot(FromWire(*shot));
    } else if (const auto* hit = std::get_if<protocol::HitConfirmationWire>(&message)) {
      OnHitConfirmation(FromWire(*hit));
    } else if (const auto* death = std::get_if<protocol::DeathWire>(&message)) {
      OnDeath(FromWire(*death));
    } else if (const auto* lobby = std::get_if<protocol::LobbyWire>(&message)) {
      OnLobby(FromWire(*lobby));
    } else if (const auto* start = std::get_if<protocol::MatchStartWire>(&message)) {
      OnMatchStart(FromWire(*start));
    } else if (const auto* end = std::get_if<protocol::MatchEndWire>(&message)) {
      OnMatchEnd(FromWire(*end));
    } else {
      return false;
    }
    return true;
  }

  // A server whose tick rate or parameters the simulation cannot run on (a rate
  // of zero would be divided by) is not a usable one: the message is dropped, as
  // a malformed one is, and the client stays unadmitted.
  void OnJoinAccepted(const Admission& accepted) {
    if (!parameters::IsValidTickRate(accepted.tick_rate_hz)) {
      LW_LIMITED(drop_warnings, "subsystem=harness event=dropped reason=\"invalid tick rate\" tick_rate_hz={}",
                 accepted.tick_rate_hz);
      return;
    }
    if (const auto valid = parameters::Validate(accepted.parameters); !valid) {
      LW_LIMITED(drop_warnings, "subsystem=harness event=dropped reason=\"invalid parameters\" parameter={}",
                 valid.error().path);
      return;
    }
    Publish([&](ServerView& next) { next.accepted = accepted; });
    LI("subsystem=harness event=joined session={} character={} tick_rate_hz={}",
       static_cast<std::uint32_t>(accepted.session), accepted.character, accepted.tick_rate_hz);
  }

  void OnLobby(Lobby lobby) {
    LI("subsystem=harness event=lobby version={} players={}", lobby.version, lobby.roster.size());
    Publish([&](ServerView& next) { next.lobby = std::move(lobby); });
  }

  // A Match start that leaves this client out is not one it can play: dropped,
  // as a malformed message is.
  void OnMatchStart(MatchStart start) {
    const std::shared_ptr<const ServerView> current = view.load();
    if (!current->accepted.has_value() || !IsInMatch(start, current->accepted->session)) {
      LW_LIMITED(drop_warnings, "subsystem=harness event=dropped reason=\"match start without this client\"");
      return;
    }
    const std::size_t players = start.players.size();
    Publish([&](ServerView& next) {
      next.match_start = std::move(start);
      ++next.matches_started;
      next.in_match = true;
      next.match_end.reset();
      next.authoritative.reset();
      next.dead.clear();
    });
    ForgetCombat();
    LI("subsystem=harness event=match_started players={}", players);
  }

  void OnMatchEnd(const MatchEnd& end) {
    Publish([&](ServerView& next) {
      next.in_match = false;
      next.authoritative.reset();
      next.match_end = end;
    });
    // The fight's last events, the Deaths that ended the match among them, are
    // still taken; the next Match start forgets those that were not.
    if (end.winner.has_value()) {
      LI("subsystem=harness event=match_ended winner={}", std::to_underlying(*end.winner));
    } else {
      LI("subsystem=harness event=match_ended winner=draw");
    }
  }

  // Keeps shot for TakeShots if it is of the match in progress. One fired on
  // the tick a match ended arrives after Match end, which is routine; one of a
  // body not in the match is a server's mistake.
  void OnShot(const Shot& shot) {
    const std::shared_ptr<const ServerView> current = view.load();
    if (!current->in_match) {
      LT("subsystem=harness event=dropped tick={} reason=\"shot outside a match\"", shot.tick);
      return;
    }
    if (!IsInMatch(*current->match_start, shot.shooter)) {
      LW_LIMITED(drop_warnings, "subsystem=harness event=dropped tick={} reason=\"shot names a body not in the match\"",
                 shot.tick);
      return;
    }
    shots.Add(shot);
  }

  // Keeps hit for TakeHitConfirmations if it is of the match in progress, as OnShot does a Shot.
  void OnHitConfirmation(const HitConfirmation& hit) {
    const std::shared_ptr<const ServerView> current = view.load();
    if (!current->in_match) {
      LT("subsystem=harness event=dropped reason=\"hit confirmation outside a match\"");
      return;
    }
    if (!IsInMatch(*current->match_start, hit.target)) {
      LW_LIMITED(drop_warnings,
                 "subsystem=harness event=dropped reason=\"hit confirmation names a body not in the match\"");
      return;
    }
    hit_confirmations.Add(hit);
  }

  // Keeps death for TakeDeaths, and its victim as dead for the rest of the
  // match, if it is of the match in progress, as OnShot does a Shot. Both its
  // victim and its killer are players of the match: one who has left it since
  // is still in its Match start.
  void OnDeath(const Death& death) {
    const std::shared_ptr<const ServerView> current = view.load();
    if (!current->in_match) {
      LT("subsystem=harness event=dropped reason=\"death outside a match\"");
      return;
    }
    if (!IsInMatch(*current->match_start, death.victim) || !IsInMatch(*current->match_start, death.killer)) {
      LW_LIMITED(drop_warnings, "subsystem=harness event=dropped reason=\"death names a body not in the match\"");
      return;
    }
    Publish([&](ServerView& next) { next.dead.push_back(death.victim); });
    deaths.Add(death);
    LI("subsystem=harness event=death victim={} killer={} part={}", std::to_underlying(death.victim),
       std::to_underlying(death.killer), BodyPartName(death.part));
  }

  // The Shots, the Hit confirmations and the Deaths of one match are not the next one's to draw.
  void ForgetCombat() {
    shots.Clear();
    hit_confirmations.Clear();
    deaths.Clear();
  }

  void OnJoinRefused(JoinRefusal reason) {
    Publish([&](ServerView& next) { next.refusal = reason; });
    LI("subsystem=harness event=join_refused reason=\"{}\"", DescribeJoinRefusal(reason));
  }

  // Keeps state if it is newer than the one held (unreliable delivery can
  // reorder) and belongs to the match in progress: unreliable, it can arrive
  // before Match start or after Match end.
  void OnAuthoritativeState(AuthoritativeState state) {
    const std::shared_ptr<const ServerView> current = view.load();
    if (!current->in_match) {
      LT("subsystem=harness event=dropped tick={} reason=\"state outside a match\"", state.tick);
      return;
    }
    const auto in_match = [&](const EntityBody& body) { return IsInMatch(*current->match_start, body.entity); };
    if (!std::ranges::all_of(state.bodies, in_match)) {
      LW_LIMITED(drop_warnings,
                 "subsystem=harness event=dropped tick={} reason=\"state names a body not in the match\"", state.tick);
      return;
    }
    if (current->authoritative.has_value() && state.tick <= current->authoritative->tick) {
      return;
    }
    Publish([&](ServerView& next) { next.authoritative = std::move(state); });
  }

  // Makes the next view from the current one changed by mutate, and publishes it.
  // Only the Network I/O thread publishes, so nothing can intervene between the load and the store.
  template <typename Mutate>
  void Publish(Mutate&& mutate) {
    auto next = std::make_shared<ServerView>(*view.load());
    mutate(*next);
    view.store(std::move(next));
  }

  // Where Match start put this client's own player. The view is in a match,
  // whose start names this client (OnMatchStart).
  static math::Vec3 OwnSpawn(const ServerView& server_view) {
    for (const MatchPlayer& player : server_view.match_start->players) {
      if (player.session == server_view.accepted->session) {
        return player.spawn;
      }
    }
    return {};
  }

  // The body this client's own player controls, as the last Match start named it.
  static std::optional<EntityId> OwnEntity(const ServerView& server_view) {
    if (!server_view.accepted.has_value() || !server_view.match_start.has_value()) {
      return std::nullopt;
    }
    return EntityOf(*server_view.match_start, server_view.accepted->session);
  }

  // Whether this client's own player is alive in the match in progress. A
  // Death and an update at zero health each say it is not, and either can
  // arrive first: the one is reliable, the other can overtake it.
  static bool OwnAlive(const ServerView& server_view) {
    const std::optional<EntityId> own = OwnEntity(server_view);
    if (!server_view.in_match || !own.has_value()) {
      return false;
    }
    if (server_view.authoritative.has_value() && server_view.authoritative->health <= 0.0F) {
      return false;
    }
    return !std::ranges::contains(server_view.dead, *own);
  }

  // What the server's state says about this client's own player: its body and its rifle.
  static std::optional<prediction::Acknowledgement> OwnAcknowledgement(const ServerView& server_view) {
    const std::optional<EntityId> own = OwnEntity(server_view);
    if (!own.has_value() || !server_view.authoritative.has_value()) {
      return std::nullopt;
    }
    for (const EntityBody& body : server_view.authoritative->bodies) {
      if (body.entity == *own) {
        return prediction::Acknowledgement{
            .sequence = server_view.authoritative->acknowledged_sequence,
            .body = body.body,
            .rifle = server_view.authoritative->rifle,
        };
      }
    }
    return std::nullopt;
  }

  // Sends command under sequence with the commands server_view does not yet acknowledge.
  void SendCommand(const ServerView& server_view, std::uint32_t sequence, const command::Command& command) {
    // Commands the server has already processed need not go again.
    if (server_view.authoritative.has_value()) {
      while (!unacknowledged.empty() &&
             unacknowledged.front().sequence <= server_view.authoritative->acknowledged_sequence) {
        unacknowledged.pop_front();
      }
    }
    // Keeps at most the newest kMaxCommandsPerMessage, all a message can carry.
    unacknowledged.push_back(SequencedCommand{.sequence = sequence, .command = command});
    if (unacknowledged.size() > protocol::kMaxCommandsPerMessage) {
      unacknowledged.pop_front();
    }
    const std::vector<SequencedCommand> pending(unacknowledged.begin(), unacknowledged.end());
    network.Send(protocol::Encode(ToWire(pending)), networking::Reliability::kUnreliable);
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
    impl.network.Send(protocol::Encode(ToWire(impl.join_request)), networking::Reliability::kReliable);
    impl.sent_join_request = true;
  }
  for (const networking::Payload& payload : impl.network.ReceiveMessages()) {
    LT("subsystem=harness event=received bytes={}", payload.size());
    impl.HandleMessage(payload);
  }
}

networking::ConnectionState Session::GetConnectionState() const { return impl_->network.GetState(); }

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

std::optional<networking::ConnectionStats> Session::GetConnectionStats() const { return impl_->network.GetStats(); }

std::optional<SessionId> Session::GetSessionId() const {
  const std::shared_ptr<const ServerView> server_view = impl_->view.load();
  if (!server_view->accepted.has_value()) {
    return std::nullopt;
  }
  return server_view->accepted->session;
}

Phase Session::GetPhase() const {
  const std::shared_ptr<const ServerView> server_view = impl_->view.load();
  if (server_view->in_match) {
    return Phase::kMatch;
  }
  return server_view->accepted.has_value() ? Phase::kLobby : Phase::kNotAdmitted;
}

std::optional<Lobby> Session::GetLobby() const { return impl_->view.load()->lobby; }

std::optional<MatchStart> Session::GetMatchStart() const { return impl_->view.load()->match_start; }

std::optional<MatchEnd> Session::GetMatchEnd() const { return impl_->view.load()->match_end; }

void Session::ReportReady(std::uint32_t version) {
  const std::shared_ptr<const ServerView> server_view = impl_->view.load();
  if (!server_view->lobby.has_value() || server_view->lobby->version != version) {
    return;
  }
  impl_->network.Send(protocol::Encode(protocol::ReadyWire{.version = version}), networking::Reliability::kReliable);
  LD("subsystem=harness event=ready version={}", version);
}

std::optional<std::uint8_t> Session::GetTickRate() const {
  const std::shared_ptr<const ServerView> server_view = impl_->view.load();
  if (!server_view->accepted.has_value()) {
    return std::nullopt;
  }
  return server_view->accepted->tick_rate_hz;
}

std::optional<parameters::Parameters> Session::GetParameters() const {
  const std::shared_ptr<const ServerView> server_view = impl_->view.load();
  if (!server_view->accepted.has_value()) {
    return std::nullopt;
  }
  return server_view->accepted->parameters;
}

std::optional<JoinRefusal> Session::GetRefusal() const { return impl_->view.load()->refusal; }

std::optional<AuthoritativeState> Session::GetAuthoritativeState() const { return impl_->view.load()->authoritative; }

std::vector<Shot> Session::TakeShots() { return impl_->shots.Take(); }

std::vector<HitConfirmation> Session::TakeHitConfirmations() { return impl_->hit_confirmations.Take(); }

std::vector<Death> Session::TakeDeaths() { return impl_->deaths.Take(); }

bool Session::IsAlive() const { return Impl::OwnAlive(*impl_->view.load()); }

std::optional<float> Session::GetHealth() const {
  const std::shared_ptr<const ServerView> server_view = impl_->view.load();
  if (!server_view->authoritative.has_value()) {
    return std::nullopt;
  }
  return server_view->authoritative->health;
}

std::optional<EntityId> Session::GetEntityId() const { return Impl::OwnEntity(*impl_->view.load()); }

prediction::State Session::Tick(const command::Command& command, float delta_time) {
  Impl& impl = *impl_;
  // One view for the whole tick, so the sequence, the reconciliation and the
  // commands sent all agree on what the server had said.
  const std::shared_ptr<const ServerView> server_view = impl.view.load();
  // Outside a match nothing the player presses affects one.
  if (!server_view->in_match) {
    return impl.last_state;
  }
  // Each match starts its player over where Match start put it. Sequences keep
  // growing across matches, so nothing of the last one is mistaken for this one's.
  if (impl.started_match != server_view->matches_started) {
    impl.prediction.Start(Impl::OwnSpawn(*server_view), server_view->accepted->parameters);
    impl.unacknowledged.clear();
    impl.started_match = server_view->matches_started;
  }
  const std::uint32_t sequence = impl.next_sequence++;
  // A dead player's body and rifle are gone from the server: there is nothing
  // to predict or reconcile, and its commands keep only the stream in step.
  if (!Impl::OwnAlive(*server_view)) {
    command::Command unarmed = command;
    unarmed.fire = false;
    impl.SendCommand(*server_view, sequence, unarmed);
    return impl.last_state;
  }
  impl.last_state = impl.prediction.Tick(command, sequence, Impl::OwnAcknowledgement(*server_view), delta_time);
  impl.SendCommand(*server_view, sequence, command);
  return impl.last_state;
}

}  // namespace augusta::harness
