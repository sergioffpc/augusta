#ifndef AUGUSTA_HARNESS_H_
#define AUGUSTA_HARNESS_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "augusta/assets.h"
#include "augusta/ballistics.h"
#include "augusta/command.h"
#include "augusta/failure.h"
#include "augusta/faults.h"
#include "augusta/math.h"
#include "augusta/networking.h"
#include "augusta/parameters.h"
#include "augusta/physics.h"
#include "augusta/prediction.h"
#include "augusta/tick.h"
#include "augusta/version.h"
#include "augusta/weapon.h"

/// \file
/// augusta::harness is where anything that plays talks to the server: the
/// client's network connection and PredictionWorld (ADR-0021, ADR-0024), without
/// the parts that need a window or a GPU (Input, Renderer, Audio,
/// PresentationWorld). Whatever supplies the input for a tick plugs in here:
/// anything that plays live - ClientRuntime (src/client), a future autonomous
/// agent - runs one in real time under a Runner (runner.h), and an automated
/// test drives one by hand so client/server behavior can be checked in CI with
/// no display and no clock.
///
/// Nothing here owns a thread or reads a clock: the caller decides when the
/// network is serviced (PumpEvents, ExchangeMessages) and when a tick happens
/// (Tick), and hands in the input for that tick. The two are meant for two
/// different threads, as in Runner: Connect, Disconnect, PumpEvents,
/// ExchangeMessages, GetConnectionState and GetConnectionStats from the Network
/// I/O thread, and Tick from the Prediction thread (the transport is safe to
/// send from both). A failure of the local transport inside any of them is kept
/// for the caller to take (Session::TakeTransportFailure) and stop on; the
/// server ending the connection is the Session's own Failure instead.
namespace augusta::harness {

/// The server's name for one connected player (CONTEXT.md, "Session ID"), as
/// this client knows it: assigned when the server admits the join. Distinct
/// from the transport's handle for the connection, and not a credential.
enum class SessionId : std::uint32_t {};

/// The server's name for one dynamic body (CONTEXT.md, "Entity ID"), as this
/// client knows it: a player's is given in Match start. It names the body, not
/// who moves it, so it is never a session.
enum class EntityId : std::uint32_t {};

/// One dynamic body, under the entity the server names it by.
struct EntityBody {
  EntityId entity{};
  physics::BodyState body{};
  /// Where the body faces: the yaw of its player's view (command::Command), in radians.
  float yaw = 0.0F;
};

/// What the server said about every body as of one of its ticks: its
/// Authoritative State, as this client receives it.
struct AuthoritativeState {
  /// The server tick this state is from; a client keeps only the newest it has seen.
  tick::Tick tick = 0;
  /// The highest command sequence of this client that the server has processed, 0 if none.
  command::Sequence acknowledged_sequence = 0;
  /// Every dynamic body in the match.
  std::vector<EntityBody> bodies;
  /// This client's own player's rifle as of that tick: what its predicted rifle
  /// is reconciled against.
  weapon::State rifle{};
  /// This client's own player's health as of that tick, 0 once it has died. No
  /// other player's is told.
  float health = 0.0F;
  /// How many of this client's commands the server still held queued after
  /// that tick: what the client paces its own ticks by (tick::PacedTickDuration).
  std::uint8_t queued_commands = 0;
};

/// One round a player fired, as the server announced it (CONTEXT.md's Shot,
/// ADR-0044).
struct Shot {
  /// The body of the player who fired it.
  EntityId shooter{};
  /// The server tick it was fired on.
  tick::Tick tick = 0;
  /// Where the round left from.
  math::Vec3 origin{};
  /// Where it left for, as a view's yaw and pitch in radians (command::Command;
  /// command::ViewDirection gives the direction).
  float yaw = 0.0F;
  float pitch = 0.0F;
};

/// The most Shots a Session keeps for TakeShots: a few seconds of a full match
/// firing, so a caller that asks every frame loses none and one that never
/// asks holds no more than this.
inline constexpr std::size_t kMaxPendingShots = 256;

/// A round this client's player fired hit a player, as the server confirmed it
/// (CONTEXT.md's Hit confirmation, ADR-0044).
struct HitConfirmation {
  /// The body that was hit.
  EntityId target{};
  /// The damage the hit did.
  float damage = 0.0F;
  /// Where on the body it struck.
  ballistics::BodyPart part = ballistics::BodyPart::kTorso;
};

/// A player in the match died (US-13), as the server told every client in it:
/// for the rest of the match. Carries what a ragdoll would start from (ADR-0045).
struct Death {
  /// The body of the player who died.
  EntityId victim{};
  /// The body of the player who fired the killing round.
  EntityId killer{};
  /// Where the killing round was fired for, as a view's yaw and pitch in
  /// radians (command::ViewDirection gives the direction).
  float yaw = 0.0F;
  float pitch = 0.0F;
  /// Where on the victim it struck.
  ballistics::BodyPart part = ballistics::BodyPart::kTorso;
};

/// The most Deaths a Session keeps for TakeDeaths: a match's players die once each.
inline constexpr std::size_t kMaxPendingDeaths = 16;

/// The most Hit confirmations a Session keeps for TakeHitConfirmations: more
/// than one rifle lands between two frames, by far.
inline constexpr std::size_t kMaxPendingHitConfirmations = 64;

/// One player in the Lobby.
struct RosterEntry {
  SessionId session{};
  /// Its character, by its name in the scenario's manifest (ADR-0042).
  std::string character;
};

/// Who is in the Lobby, as the server last said (ADR-0043).
struct Lobby {
  /// Numbers this Roster; what ReportReady names.
  std::uint32_t version = 0;
  /// Every player in the Lobby, this client's own included.
  std::vector<RosterEntry> roster;
};

/// One player in a match, the body it controls, and where the server spawned it.
struct MatchPlayer {
  SessionId session{};
  /// The body this player's commands move for the whole match.
  EntityId entity{};
  /// Its character (see RosterEntry::character).
  std::string character;
  math::Vec3 spawn{};
};

/// What the server said when a match started: everyone in it, this client's own player included.
struct MatchStart {
  std::vector<MatchPlayer> players;
  /// The Match's first server tick: what a Match capture's offsets count from (ADR-0050).
  tick::Tick first_tick = 0;
};

/// What the server said when a match ended (US-14).
struct MatchEnd {
  /// The session of the player Game policy declared the winner; nullopt for a draw.
  std::optional<SessionId> winner;
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

/// Why the server refused this client's join.
enum class JoinRefusal : std::uint8_t {
  /// This client's engine version is not the server's.
  kVersionMismatch,
  /// The Lobby already holds the scenario's Player count.
  kLobbyFull,
  /// The character this client asked to play is not one of the scenario's (ADR-0042).
  kUnknownCharacter,
  /// A match is under way, and no one joins one in progress (ADR-0043).
  kMatchInProgress,
  /// This client's pack is not the one cooked with the server's.
  kPackMismatch,
  /// This client asked to join as a Captured player, and the server takes none (ADR-0050).
  kReenactmentsNotAccepted,
};

/// A short lowercase description of reason, for logs and for the player.
[[nodiscard]] std::string_view DescribeJoinRefusal(JoinRefusal reason);

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
  JoinRefusal refusal{};
};

/// A sentence for the player saying what happened and, where the client can
/// tell, what to fix; the same wording wherever it is shown.
[[nodiscard]] std::string DescribeFailure(const Failure& failure);

/// What the server said when it admitted this client, in the engine's terms.
struct Admission {
  /// The session the server assigned to this client.
  SessionId session{};
  /// The rate, in Hz, at which the server ticks and this client must.
  std::uint8_t tick_rate_hz = 0;
  /// The parameters this client must predict with.
  parameters::Parameters parameters{};
  /// This client's own character (see RosterEntry::character).
  std::string character;
};

/// What the server has told this client, as of one moment (ADR-0005): the
/// Network I/O thread publishes a new one, whole, for every message that changes
/// it, and never changes one it has published. A reader on another thread takes
/// one (Session::GetServerView) and reads everything it needs from it, so what
/// it reads is one moment, never a mix of two.
struct ServerView {
  /// The whole answer to a join request; nullopt until the server admits this client.
  std::optional<Admission> accepted;
  /// Why the server refused this client, if it did.
  std::optional<JoinRefusal> refusal;
  /// The newest Roster, kept through a match.
  std::optional<Lobby> lobby;
  /// The last match's start, and how many have started: a new count is a new
  /// match for the prediction to start over in, and the match whose Shots, Hit
  /// confirmations and Deaths a view takes (Session::TakeShots).
  std::optional<MatchStart> match_start;
  std::uint32_t matches_started = 0;
  /// Whether this client is playing in a match: from its Match start to its Match end.
  bool in_match = false;
  /// How the last match ended, until the next starts.
  std::optional<MatchEnd> match_end;
  /// The newest Authoritative State of the match in progress; only while in_match.
  std::optional<AuthoritativeState> authoritative;
  /// The bodies of the match in progress whose Death has been told.
  std::vector<EntityId> dead;

  /// Whether this client is waiting to be admitted, in the Lobby, or in a match.
  [[nodiscard]] Phase GetPhase() const;
  /// The body this client's player controls, as the last Match start named it.
  [[nodiscard]] std::optional<EntityId> OwnEntity() const;
  /// Whether this client's player is alive in the match in progress: neither
  /// told of its Death nor at zero health in the newest Authoritative State.
  [[nodiscard]] bool OwnAlive() const;
};

/// What a Session needs to connect.
struct SessionConfig {
  /// The dedicated server to connect to (US-01).
  networking::Endpoint server{};
  /// The engine version to present when joining; the server admits only its own.
  std::string engine_version = std::string(EngineVersion());
  /// The hash of the client pack loaded (assets::Pack::Hash); the server admits
  /// only the one cooked with its own pack.
  assets::PackHash client_pack{};
  /// The character to ask to play, by its name in the scenario's manifest (e.g.
  /// "soldier"): the server admits only one of its scenario's (ADR-0042).
  std::string character;
  /// For a test: asked at every send and receive (networking.h), so the
  /// transport fails there; null otherwise. Must outlive the Session.
  failure::Faults* faults = nullptr;
  /// Where to ask to spawn, for a Captured player (ADR-0050): it joins with a
  /// Reenact request naming it in place of a Join request. nullopt, the
  /// default, for anyone else, whom Game policy places.
  std::optional<math::Vec3> spawn = std::nullopt;
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

  /// Not copyable or movable: owns a live network connection.
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

  /// What the server has told this client as of now, whole: the one way to read
  /// several of the values below as of the same moment. Safe from any thread.
  [[nodiscard]] std::shared_ptr<const ServerView> GetServerView() const;

  /// Whether the connection is still connecting, connected or disconnected.
  [[nodiscard]] networking::ConnectionState GetConnectionState() const;

  /// Why this session has ended on its own, or nullopt while it has not: before
  /// Connect, while connecting or connected, and after Disconnect (which the
  /// caller asked for, so it is not a failure). Safe to read from any thread.
  [[nodiscard]] std::optional<Failure> GetFailure() const;

  /// The first failure of the local transport a send or receive met
  /// (failure::Code::kTransportSendFailed, kTransportReceiveFailed), once;
  /// nullopt before one and after it has been taken. A runtime failure, not
  /// the server's doing: Runner's thread that takes it stops on it. Safe from
  /// any thread.
  [[nodiscard]] std::optional<failure::Failure> TakeTransportFailure();

  /// The first message this client could not encode, as the broken invariant
  /// it is (failure::Code::kInvariantViolated, ADR-0033), once; nullopt before
  /// one and after it has been taken. What it was is not sent: whoever runs
  /// the Session stops on it, as on a transport failure. ExchangeMessages,
  /// ReportReady and Tick may each meet one. Safe from any thread.
  [[nodiscard]] std::optional<failure::Failure> TakeInvariantFailure();

  /// The connection's quality numbers, or nullopt if not connected.
  [[nodiscard]] std::optional<networking::ConnectionStats> GetConnectionStats() const;

  /// The session the server assigned once it admitted this client, or nullopt
  /// until then. Set by ExchangeMessages; safe to read from any thread.
  [[nodiscard]] std::optional<SessionId> GetSessionId() const;

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

  /// What the server said when the match this client was last in ended, with
  /// its winner; nullopt before the first ends and while one is in progress.
  /// Set by ExchangeMessages; safe to read from any thread.
  [[nodiscard]] std::optional<MatchEnd> GetMatchEnd() const;

  /// Tells the server this client has loaded what it needs to draw everyone in
  /// the Roster of version, which makes it Ready. Sends nothing unless version
  /// is that of the newest Roster received: the server would not count an older
  /// one. The caller decides when loading is done; safe to call from any thread.
  void ReportReady(std::uint32_t version);

  /// The rate, in Hz, at which the server ticks and this client must: nullopt
  /// until the server admits this client. The server's startup setting, fixed for
  /// the life of its process, so it is told once, in Join accepted. This client
  /// has no rate of its own. Set by ExchangeMessages; safe to read from any thread.
  [[nodiscard]] std::optional<std::uint8_t> GetTickRate() const;

  /// The parameters the server sent when it admitted this client, or nullopt
  /// until it does. What this client predicts with, for the whole run; it has
  /// none of its own. Set by ExchangeMessages; safe to read from any thread.
  [[nodiscard]] std::optional<parameters::Parameters> GetParameters() const;

  /// Why the server refused this client, or nullopt if it has not. Set by
  /// ExchangeMessages; safe to read from any thread.
  [[nodiscard]] std::optional<JoinRefusal> GetRefusal() const;

  /// The newest Authoritative State of the match in progress, or nullopt until
  /// one arrives and again once the match ends. One that arrives outside a match
  /// (unreliable, it can overtake Match start or Match end) or that names a
  /// body not in the match is dropped. Set by ExchangeMessages; safe to read
  /// from any thread.
  [[nodiscard]] std::optional<AuthoritativeState> GetAuthoritativeState() const;

  /// The Shots of the match view is of (ServerView::matches_started): the
  /// match in progress, or the last one if back in the Lobby. Those received
  /// since the last call, in the order they arrived; the newest
  /// kMaxPendingShots of them if more did. One of an earlier match is never
  /// taken: once a view of a match is held, none of the one before is left to
  /// take. One of a later match is kept for a view of it, so what a reader
  /// draws is always of the match its view is of. One that arrives outside a
  /// match or names a body not in it is dropped. Received by ExchangeMessages;
  /// safe to call from any thread.
  [[nodiscard]] std::vector<Shot> TakeShots(const ServerView& view);
  /// TakeShots for the newest Server view (GetServerView).
  [[nodiscard]] std::vector<Shot> TakeShots();

  /// The Hit confirmations of the match view is of, as TakeShots hands out
  /// Shots; the newest kMaxPendingHitConfirmations of them if more arrived.
  [[nodiscard]] std::vector<HitConfirmation> TakeHitConfirmations(const ServerView& view);
  /// TakeHitConfirmations for the newest Server view (GetServerView).
  [[nodiscard]] std::vector<HitConfirmation> TakeHitConfirmations();

  /// The Deaths of the match view is of (those that ended it among them), as
  /// TakeShots hands out Shots; the newest kMaxPendingDeaths of them if more
  /// arrived.
  [[nodiscard]] std::vector<Death> TakeDeaths(const ServerView& view);
  /// TakeDeaths for the newest Server view (GetServerView).
  [[nodiscard]] std::vector<Death> TakeDeaths();

  /// Whether this client's own player is alive: in a match, and neither told
  /// of its Death nor at zero health in the newest Authoritative State. Death
  /// is for the rest of the match. Set by ExchangeMessages; safe to read from
  /// any thread.
  [[nodiscard]] bool IsAlive() const;

  /// This client's own player's health, as the newest Authoritative State of
  /// the match in progress told it; nullopt outside a match and until the
  /// match's first state arrives. Set by ExchangeMessages; safe to read from
  /// any thread.
  [[nodiscard]] std::optional<float> GetHealth() const;

  /// The body this client's player controls, as Match start named it: in the
  /// match in progress, or the last one if back in the Lobby; nullopt before
  /// the first. Set by ExchangeMessages; safe to read from any thread.
  [[nodiscard]] std::optional<EntityId> GetEntityId() const;

  /// The sequence the command of the next Tick in a match goes under: what a
  /// caller that paces its commands to the server's ticks counts from (the
  /// Authoritative State's acknowledged sequence is the last handed to the
  /// World). Prediction thread, as Tick.
  [[nodiscard]] command::Sequence NextSequence() const;

  /// Runs one fixed tick of PredictionWorld for command and returns its state.
  /// The Seen time command reports (command::Command) is the caller's to fill, from
  /// whatever it shows the other players with: a Session shows nothing.
  /// Outside a match nothing is predicted or sent, and the state is the last
  /// one predicted. The first tick of each match starts the prediction over at
  /// the spawn point Match start gave this client, with a rifle ready to fire,
  /// under the stamina rules and the rifle the server sent. From then on the
  /// command goes to the server under the next sequence, together with the
  /// recent commands the server has not yet acknowledged, and the prediction is
  /// reconciled against what the server last said about this client's player:
  /// its body and its rifle, when the server's state still has its body. Once
  /// this client's player is dead (IsAlive), nothing more is predicted and the
  /// state is the last one predicted; the command still goes to the server,
  /// which acknowledges it and does nothing with it, but never with fire held.
  prediction::State Tick(const command::Command& command, float delta_time);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace augusta::harness

#endif  // AUGUSTA_HARNESS_H_
