#ifndef AUGUSTA_HARNESS_H_
#define AUGUSTA_HARNESS_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "augusta/input.h"
#include "augusta/math.h"
#include "augusta/networking.h"
#include "augusta/parameters.h"
#include "augusta/physics.h"
#include "augusta/prediction.h"
#include "augusta/protocol.h"
#include "augusta/version.h"

// augusta::harness is where anything that plays talks to the server: the
// client's network connection and PredictionWorld (ADR-0021, ADR-0024), without
// the parts that need a window or a GPU (Input, Renderer, Audio,
// PresentationWorld). Whatever supplies the input for a tick plugs in here:
// ClientRuntime (src/client) drives one from its Prediction and Network I/O
// threads (ADR-0005), an automated test drives one by hand so client/server
// behavior can be checked in CI with no display, and a future autonomous agent
// would drive one the same way.
//
// Nothing here owns a thread or reads a clock: the caller decides when the
// network is serviced (PumpEvents, ExchangeMessages) and when a tick happens
// (Tick), and hands in the input for that tick. The two are meant for two
// different threads, as in ClientRuntime: Connect, Disconnect, PumpEvents,
// ExchangeMessages, GetConnectionState and GetConnectionStats from the Network
// I/O thread, and Tick from the Prediction thread (the transport is safe to
// send from both).
namespace augusta::harness {

/// One player's body, under the session the server knows it by.
struct PlayerBody {
  protocol::SessionId session{};
  physics::BodyState body{};
};

/// What the server said about every player as of one of its ticks: its
/// Authoritative State, as this client receives it.
struct AuthoritativeState {
  /// The server tick this state is from; a client keeps only the newest it has seen.
  std::uint32_t tick = 0;
  /// The highest command sequence of this client that the server has processed, 0 if none.
  std::uint32_t acknowledged_sequence = 0;
  /// Every player in the match.
  std::vector<PlayerBody> players;
};

/// One player in the Lobby.
struct RosterEntry {
  protocol::SessionId session{};
  /// Its character index: 1-based position in the scenario's character list (ADR-0042).
  std::uint8_t character = 1;
};

/// Who is in the Lobby, as the server last said (ADR-0043).
struct Lobby {
  /// Numbers this Roster; what ReportReady names.
  std::uint32_t version = 0;
  /// Every player in the Lobby, this client's own included.
  std::vector<RosterEntry> roster;
};

/// One player in a match, and where the server spawned it.
struct MatchPlayer {
  protocol::SessionId session{};
  std::uint8_t character = 1;
  math::Vec3 spawn{};
};

/// What the server said when a match started: everyone in it, this client's own player included.
struct MatchStart {
  std::vector<MatchPlayer> players;
};

/// Where a Session is in the lifecycle of ADR-0043.
enum class Phase : std::uint8_t {
  /// The server has not admitted this client: not yet, or it refused.
  kNotAdmitted,
  /// Admitted, waiting in the Lobby for the next match.
  kLobby,
  /// Playing in a match.
  kMatch,
};

/// Why a Session ended without the player asking it to.
enum class FailureKind {
  /// The server answered the join with a refusal; see Failure::refusal.
  kRefused,
  /// The connection ended before the server admitted this client: nothing
  /// answered at that address, or what did was not a compatible server.
  kServerUnreachable,
  /// The server admitted this client, and the connection has since ended.
  kConnectionLost,
};

/// How a Session failed. There is no reconnecting: the caller reports it and exits.
struct Failure {
  /// What ended the session.
  FailureKind kind{};
  /// Why the server refused; only meaningful for kRefused.
  protocol::JoinRefusal refusal{};
};

/// A sentence for the player saying what happened and, where the client can
/// tell, what to fix; the same wording wherever it is shown.
[[nodiscard]] std::string DescribeFailure(const Failure& failure);

/// What a Session needs to connect.
struct SessionConfig {
  /// The dedicated server to connect to (US-01).
  networking::Endpoint server{};
  /// The engine version to present when joining; the server admits only its own.
  std::string engine_version = std::string(EngineVersion());
  /// The hash of the client pack loaded (assets::Pack::Hash); the server admits
  /// only the one cooked with its own pack.
  protocol::PackHash client_pack{};
  /// The character to ask to play, by its path relative to `authoring/` (e.g.
  /// "characters/player"): the server admits only one of its scenario's (ADR-0042).
  std::string character;
};

/// The client's network connection and PredictionWorld, without a window or a GPU.
class Session {
 public:
  /// Constructs the connection around prediction, which the Session takes over
  /// and Ticks; connects to nothing yet. Load the map into prediction
  /// (World::AddCollisionMesh) before handing it over: a body that has already
  /// ticked has been predicted without it, and reconciliation cannot account
  /// for that.
  Session(const SessionConfig& config, prediction::World prediction);
  ~Session();

  // Not copyable or movable: owns a live network connection.
  Session(const Session&) = delete;
  Session& operator=(const Session&) = delete;
  Session(Session&&) = delete;
  Session& operator=(Session&&) = delete;

  /// Begins connecting to the configured server; returns immediately.
  void Connect();

  /// Ends the connection, if any.
  void Disconnect();

  /// Drains connection events (handshake progress, rejection, drop).
  void PumpEvents();

  /// Sends what is due and takes in what has been received. Call after
  /// PumpEvents, in the same round of the Network I/O thread's loop.
  void ExchangeMessages();

  /// Whether the connection is still connecting, connected or disconnected.
  [[nodiscard]] networking::ConnectionState GetConnectionState() const;

  /// Why this session has ended on its own, or nullopt while it has not: before
  /// Connect, while connecting or connected, and after Disconnect (which the
  /// caller asked for, so it is not a failure). Safe to read from any thread.
  [[nodiscard]] std::optional<Failure> GetFailure() const;

  /// The connection's quality numbers, or nullopt if not connected.
  [[nodiscard]] std::optional<networking::ConnectionStats> GetConnectionStats() const;

  /// The session the server assigned once it admitted this client, or nullopt
  /// until then. Set by ExchangeMessages; safe to read from any thread.
  [[nodiscard]] std::optional<protocol::SessionId> GetSessionId() const;

  /// Whether this client is waiting to be admitted, in the Lobby, or in a
  /// match. Set by ExchangeMessages; safe to read from any thread.
  [[nodiscard]] Phase GetPhase() const;

  /// Who is in the Lobby, as of the newest Roster the server sent, or nullopt
  /// until one arrives. Kept through a match, whose players are in
  /// GetMatchStart. Set by ExchangeMessages; safe to read from any thread.
  [[nodiscard]] std::optional<Lobby> GetLobby() const;

  /// What the server said at the start of the match in progress, or of the last
  /// one if back in the Lobby; nullopt before the first. Set by
  /// ExchangeMessages; safe to read from any thread.
  [[nodiscard]] std::optional<MatchStart> GetMatchStart() const;

  /// Tells the server this client has loaded what it needs to draw everyone in
  /// the Roster of version, which makes it Ready. Sends nothing unless version
  /// is that of the newest Roster received: the server would not count an older
  /// one. The caller decides when loading is done; safe to call from any thread.
  void ReportReady(std::uint32_t version);

  /// The rate, in Hz, at which the server ticks and this client must: nullopt
  /// until the server admits this client. The server's startup setting, fixed for
  /// the life of its process, so it is told once, in Join accepted. This client
  /// has no rate of its own. Set by ExchangeMessages; safe to read from any thread.
  [[nodiscard]] std::optional<float> GetTickRate() const;

  /// The parameters the server sent when it admitted this client, or nullopt
  /// until it does. What this client predicts with, for the whole run; it has
  /// none of its own. Set by ExchangeMessages; safe to read from any thread.
  [[nodiscard]] std::optional<parameters::Parameters> GetParameters() const;

  /// Why the server refused this client, or nullopt if it has not. Set by
  /// ExchangeMessages; safe to read from any thread.
  [[nodiscard]] std::optional<protocol::JoinRefusal> GetRefusal() const;

  /// The newest Authoritative State of the match in progress, or nullopt until
  /// one arrives and again once the match ends. One that arrives outside a match
  /// (unreliable, it can overtake Match start or Match end) or that names a
  /// player not in the match is dropped. Set by ExchangeMessages; safe to read
  /// from any thread.
  [[nodiscard]] std::optional<AuthoritativeState> GetAuthoritativeState() const;

  /// Runs one fixed tick of PredictionWorld for command and returns its state.
  /// Outside a match nothing is predicted or sent, and the state is the last
  /// one predicted. The first tick of each match starts the prediction over at
  /// the spawn point Match start gave this client, under the stamina rules the
  /// server sent. From then on the command goes to the server under the next
  /// sequence, together with the recent commands the server has not yet
  /// acknowledged, and the prediction is reconciled against what the server
  /// last said about this client's player.
  prediction::State Tick(const input::Command& command, float delta_time);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace augusta::harness

#endif  // AUGUSTA_HARNESS_H_
