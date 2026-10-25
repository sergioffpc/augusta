#ifndef AUGUSTA_TESTS_SESSION_TEST_H_
#define AUGUSTA_TESTS_SESSION_TEST_H_

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#include <boost/uuid/uuid_io.hpp>
#include <gtest/gtest.h>

#include "augusta/assets.h"
#include "augusta/harness.h"
#include "augusta/host.h"
#include "augusta/link.h"
#include "augusta/link_messages.h"
#include "augusta/math.h"
#include "augusta/parameters.h"
#include "augusta/physics.h"
#include "augusta/prediction.h"
#include "augusta/protocol.h"
#include "augusta/raw_link.h"
#include "augusta/version.h"

// What the Session tests' files share: a real server host and real client
// sessions over loopback, driven by hand, and a client and a server speaking
// the protocol by hand. The Development build's cases (ADR-0074) sit in files
// of their own. session_test.cpp starts and stops the Link once for the whole
// process.
namespace augusta::session_test {

using harness::Phase;
using harness::ServerView;
using harness::Session;
using harness::SessionConfig;
using harness::SessionId;
using host::Host;
using host::HostConfig;
using link::ConnectionState;
using link::Endpoint;
using math::Vec3;
using parameters::Parameters;
using physics::CollisionMesh;
using protocol::EntityIdWire;
using protocol::SessionIdWire;

// The session the server admitted view's client as, or nullopt until it has.
inline std::optional<SessionId> AdmittedSession(const ServerView& view) {
  return view.accepted.has_value() ? std::optional<SessionId>(view.accepted->session) : std::nullopt;
}

// How a test that waited for the command of client, of that index, names it
// once it never reached the server: whose, and for the tick after the newest
// client was told of.
inline std::string NeverArrived(std::size_t index, const Session& client) {
  const auto view = client.GetServerView();
  std::ostringstream named;
  named << "the command of client " << index << " (Session " << view->accepted->session
        << ") never reached the server, for the tick after "
        << (view->authoritative.has_value() ? std::to_string(view->authoritative->tick) : "none")
        << ", the newest it was told of";
  return named.str();
}

inline constexpr auto kPollInterval = std::chrono::milliseconds(10);
inline constexpr auto kPollDeadline = std::chrono::seconds(5);
inline constexpr float kFixedTick = 1.0F / 60.0F;

// What a test's server runs on: NFR-01's 60 Hz and stamina rules that never
// drain, for a match of one player.
inline constexpr std::uint8_t kTestTickRate = 60;
inline const Parameters kTestParameters{};

// The one character every test's server offers and every test's client picks,
// unless a test says otherwise.
inline constexpr const char* kCharacter = "soldier";

// A Skeleton of one Joint at the character's feet, so a test's hitboxes, on it,
// sit in the character's space as they are written.
inline assets::Skeleton RootSkeleton() { return {.joints = {{.name = "Root"}}, .rest_transforms = {math::Mat4(1.0F)}}; }

// hitbox, on RootSkeleton's Joint.
inline assets::JointHitbox OnRoot(assets::HitboxData hitbox) {
  hitbox.joint = "Root";
  return {.joint = 0, .hitbox = std::move(hitbox)};
}

// The test parameters, for a match of count players.
inline Parameters WithPlayerCount(std::uint8_t count) {
  Parameters parameters = kTestParameters;
  parameters.player_count = count;
  return parameters;
}

// A PredictionWorld with no map, and nothing decided yet: a server decides it on the client's join.
inline augusta::prediction::World EmptyWorld() { return augusta::prediction::World(); }

// ctest runs every test case in its own process, possibly in parallel, so a
// fixed port would collide: each server binds port 0, a free one of its own
// choosing, and its clients connect to the one the server reports.
inline constexpr const char* kLoopbackAnyPort = "127.0.0.1:0";

// An address nobody listens at: the one a server chose and has since closed.
// Another process's server could draw it again, but only one port in 16384 does.
inline Endpoint UnusedLoopbackEndpoint() {
  const augusta::link::Server server(Endpoint{.address = kLoopbackAnyPort});
  return Endpoint{.address = server.LocalEndpoint().address};
}

// A test server's settings: the test tick rate unless a test says otherwise.
inline HostConfig TestHostConfig(const Parameters& parameters = kTestParameters,
                                 std::uint8_t tick_rate_hz = kTestTickRate) {
  return HostConfig{
      .tick_rate_hz = tick_rate_hz,
      .parameters = parameters,
      .listen = Endpoint{.address = kLoopbackAnyPort},
      .server_pack = {},
      .capture_directory = {},
      .capture_mode = {},
      .faults = nullptr,
  };
}

// A client of the server at server playing character.
inline SessionConfig TestSessionConfig(const Endpoint& server, const std::string& character = kCharacter) {
  return SessionConfig{.server = {.address = server.address}, .character = character};
}

// The JoinRequestWire a real client of the test server sends.
inline augusta::protocol::JoinRequestWire HonestJoinRequest() {
  return augusta::protocol::JoinRequestWire{.engine_version = std::string(augusta::EngineVersion()),
                                            .character = kCharacter};
}

// A client speaking the protocol by hand, to send what a real one would not.
class RawClient {
 public:
  // A cooperative client asks to join once connected and reports Ready for each
  // Roster it is sent, as a client that has loaded everyone does; a scripted one
  // does neither, and its test sends every message itself.
  enum class Mode : std::uint8_t { kCooperative, kScripted };

  explicit RawClient(const Endpoint& server, Mode mode = Mode::kCooperative) : mode_(mode) {
    client_.Connect({.address = server.address});
  }

  ~RawClient() { client_.Disconnect(); }

  RawClient(const RawClient&) = delete;
  RawClient& operator=(const RawClient&) = delete;
  RawClient(RawClient&&) = delete;
  RawClient& operator=(RawClient&&) = delete;

  // Runs host - its clock stopped at host_time, if given - and client until
  // until() holds or the deadline passes; returns whether it held.
  template <typename Condition>
  bool ServeUntil(Host& host, Condition until,
                  std::optional<std::chrono::steady_clock::time_point> host_time = std::nullopt) {
    const auto deadline = std::chrono::steady_clock::now() + kPollDeadline;
    while (std::chrono::steady_clock::now() < deadline) {
      host.PumpNetwork(host_time.value_or(std::chrono::steady_clock::now()));
      Serve();
      if (until()) {
        return true;
      }
      std::this_thread::sleep_for(kPollInterval);
    }
    return false;
  }

  // Runs host and client until the connection is up.
  bool Connect(Host& host) {
    return ServeUntil(host, [&] { return client_.GetState() == ConnectionState::kConnected; });
  }

  // Runs host and client until the server has admitted this one to the Lobby.
  bool Join(Host& host) {
    return ServeUntil(host, [&] { return !ReceivedOf<augusta::protocol::JoinAcceptedWire>().empty(); });
  }

  // One round of this client's own network work: takes in what has arrived and,
  // if cooperative, asks to join once connected.
  void Serve() {
    client_.PumpEvents();
    if (mode_ == Mode::kCooperative && !requested_ && client_.GetState() == ConnectionState::kConnected) {
      Send(HonestJoinRequest());
      requested_ = true;
    }
    Drain();
  }

  [[nodiscard]] bool InMatch() const { return !ReceivedOf<augusta::protocol::MatchStartWire>().empty(); }

  // Its connection, as the Link names a Session's too, so the two compare.
  [[nodiscard]] ConnectionState GetConnectionState() const { return client_.GetState(); }

  // The session the server admitted this client as, if it has.
  [[nodiscard]] std::optional<SessionIdWire> GetSessionId() const {
    const auto accepted = ReceivedOf<augusta::protocol::JoinAcceptedWire>();
    return accepted.empty() ? std::nullopt : std::optional<SessionIdWire>(accepted.front().session);
  }

  void Send(const augusta::protocol::MessageWire& message) { SendPayload(augusta::protocol::Encode(message).value()); }

  // Sends bytes as they are, whether or not they are a message.
  void SendPayload(const augusta::protocol::BytesWire& payload) {
    // Dropped once the server has disconnected it, which a test may be after.
    ASSERT_TRUE(client_.Send(payload).has_value());
  }

  // The Authoritative State updates received since the last call.
  std::vector<augusta::protocol::AuthoritativeStateWire> Receive() {
    client_.PumpEvents();
    return Drain();
  }

  // Every message of type T received so far, in the order it arrived.
  template <typename T>
  [[nodiscard]] std::vector<T> ReceivedOf() const {
    std::vector<T> found;
    for (const auto& message : received_) {
      if (const auto* typed = std::get_if<T>(&message)) {
        found.push_back(*typed);
      }
    }
    return found;
  }

  // How many messages of any type have arrived so far.
  [[nodiscard]] std::size_t ReceivedCount() const { return received_.size(); }

  // The newest Authoritative State received so far.
  [[nodiscard]] std::optional<augusta::protocol::AuthoritativeStateWire> NewestState() const { return newest_; }

  // The body this client controls, as the last Match start named it.
  [[nodiscard]] std::optional<EntityIdWire> Entity() const {
    const auto accepted = ReceivedOf<augusta::protocol::JoinAcceptedWire>();
    const auto starts = ReceivedOf<augusta::protocol::MatchStartWire>();
    if (accepted.empty() || starts.empty()) {
      return std::nullopt;
    }
    for (const auto& player : starts.back().players) {
      if (player.session == accepted.front().session) {
        return player.entity;
      }
    }
    return std::nullopt;
  }

 private:
  // Takes in what has arrived, answering each Roster with a ReadyWire if
  // cooperative, and returns the Authoritative States among it.
  std::vector<augusta::protocol::AuthoritativeStateWire> Drain() {
    std::vector<augusta::protocol::AuthoritativeStateWire> states;
    const auto payloads = client_.Receive().value();
    for (const auto& payload : payloads) {
      const auto message = augusta::protocol::Decode(payload);
      if (!message.has_value()) {
        continue;
      }
      received_.push_back(*message);
      if (const auto* lobby = std::get_if<augusta::protocol::LobbyWire>(&*message)) {
        if (mode_ == Mode::kCooperative) {
          Send(augusta::protocol::ReadyWire{.version = lobby->version});
        }
      } else if (const auto* state = std::get_if<augusta::protocol::AuthoritativeStateWire>(&*message)) {
        states.push_back(*state);
        if (!newest_.has_value() || state->tick > newest_->tick) {
          newest_ = *state;
        }
      }
    }
    return states;
  }

  Mode mode_;
  augusta::link::RawClient client_;
  bool requested_ = false;
  std::vector<augusta::protocol::MessageWire> received_;
  std::optional<augusta::protocol::AuthoritativeStateWire> newest_;
};

// Runs host and every one of sessions (and raw, if any) - each reporting it
// has loaded every Roster it is sent, as a client does once it has - and ticks
// host, until all of them are in a match or the deadline passes. Returns
// whether they all are. Ticks as fast as the network allows, so the pause after
// a match (hundreds of ticks) passes in no more than a few seconds.
inline bool DriveIntoMatch(Host& host, const std::vector<Session*>& sessions, RawClient* raw = nullptr) {
  constexpr auto kTickInterval = std::chrono::milliseconds(1);
  constexpr auto kDeadline = std::chrono::seconds(15);
  std::map<const Session*, std::uint32_t> reported;
  const auto deadline = std::chrono::steady_clock::now() + kDeadline;
  while (std::chrono::steady_clock::now() < deadline) {
    host.PumpNetwork(std::chrono::steady_clock::now());
    bool all_in_match = raw == nullptr || raw->InMatch();
    if (raw != nullptr) {
      raw->Serve();
    }
    for (Session* session : sessions) {
      session->PumpEvents();
      session->ExchangeMessages();
      const auto lobby = session->GetServerView()->lobby;
      if (lobby.has_value() && session->GetServerView()->GetPhase() == Phase::kLobby &&
          reported[session] != lobby->version) {
        session->ReportReady(lobby->version);
        reported[session] = lobby->version;
      }
      all_in_match = all_in_match && session->GetServerView()->GetPhase() == Phase::kMatch;
    }
    if (all_in_match) {
      return true;
    }
    host.Tick(kFixedTick);
    std::this_thread::sleep_for(kTickInterval);
  }
  return false;
}

// Runs host's and sessions' network work, without ticking host, until until()
// holds or the deadline passes; returns whether it held.
template <typename Condition>
bool ExchangeUntil(Host& host, const std::vector<Session*>& sessions, Condition until) {
  const auto deadline = std::chrono::steady_clock::now() + kPollDeadline;
  while (std::chrono::steady_clock::now() < deadline) {
    host.PumpNetwork(std::chrono::steady_clock::now());
    for (Session* session : sessions) {
      session->PumpEvents();
      session->ExchangeMessages();
    }
    if (until()) {
      return true;
    }
    std::this_thread::sleep_for(kPollInterval);
  }
  return false;
}

// Runs host's and sessions' network work, without ticking host, for long
// enough that a message that was going to arrive has.
inline void Settle(Host& host, const std::vector<Session*>& sessions) {
  const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(300);
  ExchangeUntil(host, sessions, [&] { return std::chrono::steady_clock::now() >= until; });
}

inline std::vector<Session*> Pointers(const std::vector<std::unique_ptr<Session>>& sessions) {
  std::vector<Session*> pointers;
  pointers.reserve(sessions.size());
  for (const auto& session : sessions) {
    pointers.push_back(session.get());
  }
  return pointers;
}

// A large horizontal slab at height y, its triangles facing up.
inline CollisionMesh FloorAt(float y) {
  constexpr float kExtent = 100.0F;
  return CollisionMesh{.points = {Vec3(-kExtent, y, -kExtent), Vec3(-kExtent, y, kExtent), Vec3(kExtent, y, kExtent),
                                  Vec3(kExtent, y, -kExtent)},
                       .indices = {0, 1, 2, 0, 2, 3}};
}

// A PredictionWorld whose map is a floor at height y.
inline augusta::prediction::World WorldWithFloorAt(float y) {
  augusta::prediction::World world = EmptyWorld();
  EXPECT_TRUE(world.AddCollisionMesh(FloorAt(y)).has_value());
  return world;
}

// The session a ScriptedServer admits its client under.
inline constexpr SessionIdWire kScriptedSession =
    SessionIdWire({0x01, 0x90, 0xA1, 0xB2, 0xC3, 0xD4, 0x70, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01});

// Another session than kScriptedSession, told apart by its last byte.
constexpr SessionIdWire OtherScriptedSession(std::uint8_t last) {
  SessionIdWire session = kScriptedSession;
  *(session.end() - 1) = last;
  return session;
}

// The body a ScriptedServer gives its client in Match start: not its session's number.
inline constexpr EntityIdWire kScriptedEntity{101};

// A server speaking the protocol by hand to one client, to send what a real
// one would not: values that fail the checks, messages out of turn, or bytes
// that are no message.
class ScriptedServer {
 public:
  ScriptedServer() : server_(Endpoint{.address = kLoopbackAnyPort}) {}

  // The address it listens on, for its client to connect to.
  [[nodiscard]] Endpoint LocalEndpoint() const { return Endpoint{.address = server_.LocalEndpoint().address}; }

  // Connects session and answers its join with parameters, then with a Match
  // start of it alone if start_match, and reports whether the session was
  // admitted within wait.
  bool Admit(Session& session, const Parameters& parameters, std::chrono::milliseconds wait = kPollDeadline,
             bool start_match = true) {
    parameters_ = parameters;
    start_match_ = start_match;
    session.Connect();
    const auto deadline = std::chrono::steady_clock::now() + wait;
    while (!session.GetServerView()->accepted.has_value() && std::chrono::steady_clock::now() < deadline) {
      Pump();
      session.PumpEvents();
      session.ExchangeMessages();
      std::this_thread::sleep_for(kPollInterval);
    }
    return session.GetServerView()->accepted.has_value();
  }

  // Serves the connection and the join, and takes in whatever the client sent.
  void Pump() {
    for (const auto& event : server_.PumpEvents()) {
      if (event.type == augusta::link::PeerEventType::kConnectRequested) {
        server_.Accept(event.peer);
      }
    }
    const auto messages = server_.Receive().value();
    for (const auto& message : messages) {
      peer_ = message.from;
      const auto decoded = augusta::protocol::Decode(message.payload);
      if (!decoded.has_value()) {
        continue;
      }
      if (std::holds_alternative<augusta::protocol::JoinRequestWire>(*decoded)) {
        Send(augusta::protocol::JoinAcceptedWire{.session = kScriptedSession,
                                                 .tick_rate_hz = kTestTickRate,
                                                 .parameters = augusta::link::ToWire(parameters_),
                                                 .character = kCharacter});
        if (start_match_) {
          Send(StartOfAlone());
        }
      } else if (std::holds_alternative<augusta::protocol::CommandsWire>(*decoded)) {
        ++commands_received_;
      } else if (const auto* ready = std::get_if<augusta::protocol::ReadyWire>(&*decoded)) {
        readies_.push_back(ready->version);
      }
    }
  }

  // A Match start of the admitted client alone, at the origin.
  static augusta::protocol::MatchStartWire StartOfAlone() {
    return augusta::protocol::MatchStartWire{
        .players = {{.spawn = {}, .session = kScriptedSession, .entity = kScriptedEntity, .character = kCharacter}}};
  }

  // An Authoritative State of tick listing entities, each at the origin, with
  // the client's player unhurt.
  static augusta::protocol::AuthoritativeStateWire StateOf(augusta::tick::Tick tick,
                                                           const std::vector<EntityIdWire>& entities) {
    augusta::protocol::AuthoritativeStateWire state{
        .tick = tick, .bodies = {}, .rifle = {}, .health = 100.0F, .acknowledged_sequence = 0};
    for (const EntityIdWire entity : entities) {
      state.bodies.push_back({.entity = entity, .body = {}});
    }
    return state;
  }

  void Send(const augusta::protocol::MessageWire& message) { SendPayload(augusta::protocol::Encode(message).value()); }

  // Sends bytes as they are, whether or not they are a message.
  void SendPayload(const augusta::protocol::BytesWire& payload) {
    ASSERT_TRUE(server_.Send(*peer_, payload).has_value());
  }

  // How many CommandsWire messages the client has sent.
  [[nodiscard]] int CommandsReceived() const { return commands_received_; }

  // The Roster versions the client has reported ReadyWire for, in order.
  [[nodiscard]] const std::vector<std::uint32_t>& Readies() const { return readies_; }

 private:
  augusta::link::RawServer server_;
  std::optional<augusta::link::PeerId> peer_;
  Parameters parameters_;
  bool start_match_ = true;
  int commands_received_ = 0;
  std::vector<std::uint32_t> readies_;
};

}  // namespace augusta::session_test

#endif  // AUGUSTA_TESTS_SESSION_TEST_H_
