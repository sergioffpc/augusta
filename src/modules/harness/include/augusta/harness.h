#ifndef AUGUSTA_HARNESS_H_
#define AUGUSTA_HARNESS_H_

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <boost/uuid/nil_generator.hpp>
#include <boost/uuid/uuid.hpp>

#include "augusta/assets.h"
#include "augusta/command.h"
#include "augusta/failure.h"
#include "augusta/faults.h"
#include "augusta/link.h"
#include "augusta/link_types.h"
#include "augusta/math.h"
#include "augusta/prediction.h"
#include "augusta/tick.h"
#include "augusta/version.h"

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
/// I/O thread, and Tick from the Prediction thread (the Link is safe to send
/// from both). A failure of the local transport inside any of them is kept
/// for the caller to take (Session::TakeTransportFailure) and stop on, and the
/// Session sends nothing more after it, or after a broken invariant; the
/// server ending the connection is the Session's own Failure instead.
namespace augusta::harness {

// What the server tells this client, and what it asks, are the Link's own
// messages in the engine's terms (augusta/link_types.h), named here as the
// harness's: each Session ID and Entity ID is this client's knowledge of the
// server's name for a player or a body (GLOSSARY.md).
using SessionId = link::SessionId;
using EntityId = link::EntityId;
using EntityBody = link::EntityBody;
using AuthoritativeState = link::AuthoritativeState;
using Shot = link::Shot;
using HitConfirmation = link::HitConfirmation;
using PlayerView = link::PlayerView;
using ReplayView = link::ReplayView;
using Death = link::Death;
using RosterEntry = link::RosterEntry;
using Lobby = link::Lobby;
using MatchPlayer = link::MatchPlayer;
using MatchStart = link::MatchStart;
using MatchEnd = link::MatchEnd;
using JoinRefusal = link::JoinRefusal;
using Admission = link::Admission;
using ReplayListing = link::ReplayListing;
#if AUGUSTA_DEBUG_TOOLS
using DebugSubscribe = link::DebugSubscribe;
using ShotJudgement = link::ShotJudgement;
#endif

/// The most Shots a Session keeps for TakeShots: a few seconds of a full match
/// firing, so a caller that asks every frame loses none and one that never
/// asks holds no more than this.
inline constexpr std::size_t kMaxPendingShots = 256;

/// The most Deaths a Session keeps for TakeDeaths: a match's players die once each.
inline constexpr std::size_t kMaxPendingDeaths = 16;

/// The most Hit confirmations a Session keeps for TakeHitConfirmations: more
/// than one rifle lands between two frames, by far.
inline constexpr std::size_t kMaxPendingHitConfirmations = 64;

#if AUGUSTA_DEBUG_TOOLS
/// The most Shot judgements a Session keeps for TakeShotJudgements: as many as
/// the Hit confirmations it keeps, one per round its own player fired.
inline constexpr std::size_t kMaxPendingShotJudgements = 64;
#endif

/// The most Authoritative State updates a Server view keeps
/// (ServerView::recent_authoritative): as many as the interpolation keeps per
/// body (presentation::kUpdatesKept, which ClientRuntime holds equal to this),
/// so a frame below the tick rate still finds every update since the last.
inline constexpr std::size_t kRecentStatesKept = 28;

/// Where a Session is in the lifecycle of ADR-0043.
enum class Phase : std::uint8_t {
  /// The server has not admitted this client: not yet, or it refused.
  kNotAdmitted,
  /// Admitted, waiting in the Lobby for the next match.
  kLobby,
  /// Playing in a match.
  kMatch,
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

/// What the server has told this client, as of one moment (ADR-0005): the
/// Network I/O thread publishes a new one, whole, for every message that changes
/// it, and never changes one it has published. A reader on another thread takes
/// one (Session::GetServerView) and reads everything it needs from it, so what
/// it reads is one moment, never a mix of two.
struct ServerView {
  /// The whole answer to a join request, told once; nullopt until the server
  /// admits this client.
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
  /// The newest Authoritative State of the match in progress; only while
  /// in_match, and nullopt until the match's first arrives. One that arrives
  /// outside a match (unreliable, it can overtake Match start or Match end) or
  /// that names a body not in the match is dropped.
  std::optional<AuthoritativeState> authoritative;
  /// The Authoritative State updates of the match in progress received most
  /// recently, oldest first, the newest (authoritative) last: at most
  /// kRecentStatesKept, and each newer than the one before it. A render frame
  /// takes every one newer than the last it recorded, so none that arrived
  /// between two frames is missed.
  std::vector<AuthoritativeState> recent_authoritative;
  /// The bodies of the match in progress whose Death has been told.
  std::vector<EntityId> dead;
  /// The newest Replay view of the Replay this client watches (ADR-0051), a
  /// Replay viewer's only; only while in_match.
  std::optional<ReplayView> replay_view;

  /// Whether this client is waiting to be admitted, in the Lobby, or in a match.
  [[nodiscard]] Phase GetPhase() const;
  /// The body this client's player controls, as the last Match start named it:
  /// in the match in progress, or the last one if back in the Lobby; nullopt
  /// before the first.
  [[nodiscard]] std::optional<EntityId> OwnEntity() const;
  /// Whether this client's player is alive in the match in progress: neither
  /// told of its Death nor at zero health in the newest Authoritative State.
  /// Death is for the rest of the match.
  [[nodiscard]] bool OwnAlive() const;
};

/// What a Session needs to connect.
struct SessionConfig {
  /// The dedicated server to connect to (US-01).
  link::Endpoint server{};
  /// The engine version to present when joining; the server admits only its own.
  std::string engine_version = std::string(EngineVersion());
  /// The hash of the client pack loaded (assets::Pack::Hash); the server admits
  /// only the one cooked with its own pack.
  assets::PackHash client_pack{};
  /// The character to ask to play, by its name in the scenario's manifest (e.g.
  /// "soldier"): the server admits only one of its scenario's (ADR-0042).
  std::string character;
  /// For a test: asked at every send and receive (link.h), so the
  /// transport fails there; null otherwise. Must outlive the Session.
  failure::Faults* faults = nullptr;
  /// Set, this client is a Replay viewer (ADR-0051): instead of joining, it
  /// asks a replay server to replay the capture of this name, by its name in
  /// the server's Replay list. It is then a Spectator from the first tick: it
  /// predicts nothing and sends no Command, and character is not asked for.
  std::optional<std::string> replay = std::nullopt;
  /// Where to ask to spawn, for a Captured player (ADR-0050): it joins with a
  /// Reenact request naming it in place of a Join request. nullopt, the
  /// default, for anyone else, whom Game policy places.
  std::optional<math::Vec3> spawn = std::nullopt;
  /// The Run ID of the executable this Session is for, which its Join, Reenact
  /// or Replay request carries and the server logs at admission (ADR-0038).
  boost::uuids::uuid run = boost::uuids::nil_uuid();
  /// Told, on the Network I/O thread, of the Match this Session is in: its
  /// Match ID at each Match start the Session takes, before the start's line,
  /// and the nil ID at its Match end, after the end's line. Null, the default,
  /// tells nothing. The client holds the Match in its logging context
  /// (ADR-0029); a process that holds many Sessions, as the Agent does, has no
  /// one Match to name, and passes none.
  std::function<void(const boost::uuids::uuid& match)> on_match = nullptr;
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
  /// it, so what is read together is of the same moment. Published by
  /// ExchangeMessages; safe from any thread.
  [[nodiscard]] std::shared_ptr<const ServerView> GetServerView() const;

  /// Whether the connection is still connecting, connected or disconnected.
  [[nodiscard]] link::ConnectionState GetConnectionState() const;

  /// Why this session has ended on its own, or nullopt while it has not: before
  /// Connect, while connecting or connected, and after Disconnect (which the
  /// caller asked for, so it is not a failure), nor once a Replay viewer's
  /// Replay has ended (ReplayEnded). Safe to read from any thread.
  [[nodiscard]] std::optional<Failure> GetFailure() const;

  /// Whether this Replay viewer's Replay is over: its Match end has arrived
  /// and the replay server has closed the connection, as it does after one.
  /// Never for a client that plays. Safe to read from any thread.
  [[nodiscard]] bool ReplayEnded() const;

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
  [[nodiscard]] std::optional<link::ConnectionStats> GetConnectionStats() const;

  /// Tells the server this client has loaded what it needs to draw everyone in
  /// the Roster of version, which makes it Ready. Sends nothing unless version
  /// is that of the newest Roster received: the server would not count an older
  /// one. The caller decides when loading is done; safe to call from any thread.
  void ReportReady(std::uint32_t version);

#if AUGUSTA_DEBUG_TOOLS
  /// Asks the server for the streams of its state flags names
  /// (DebugSubscribe's bits), from now on, in a Development build alone
  /// (ADR-0038, ADR-0074): a cleared bit stops its stream. Sent as it is, at
  /// any point of the connection; the Session keeps no subscription of its
  /// own, so a new connection asks again. Safe to call from any thread.
  void SubscribeDebug(std::uint8_t flags);
#endif

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

  /// The Hit confirmations of the match view is of, as TakeShots hands out
  /// Shots; the newest kMaxPendingHitConfirmations of them if more arrived.
  [[nodiscard]] std::vector<HitConfirmation> TakeHitConfirmations(const ServerView& view);

  /// The Deaths of the match view is of (those that ended it among them), as
  /// TakeShots hands out Shots; the newest kMaxPendingDeaths of them if more
  /// arrived.
  [[nodiscard]] std::vector<Death> TakeDeaths(const ServerView& view);

#if AUGUSTA_DEBUG_TOOLS
  /// The Shot judgements of the match view is of, in a Development build
  /// alone (ADR-0038, ADR-0074), as TakeShots hands out Shots; the newest
  /// kMaxPendingShotJudgements of them if more arrived. The server sends one
  /// for each round this client's player fired, once its flight ends, while
  /// this client's Debug subscribe asks for them (SubscribeDebug).
  [[nodiscard]] std::vector<ShotJudgement> TakeShotJudgements(const ServerView& view);
#endif

  /// The sequence the command of the next Tick in a match goes under: what a
  /// caller that paces its commands to the server's ticks counts from (the
  /// Authoritative State's acknowledged sequence is the last handed to the
  /// World). Prediction thread, as Tick.
  [[nodiscard]] tick::Sequence NextSequence() const;

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
  /// this client's player is dead (ServerView::OwnAlive), nothing more is
  /// predicted and the state is the last one predicted; the command still goes
  /// to the server, which acknowledges it and does nothing with it, but never
  /// with fire held.
  /// A Replay viewer predicts and sends nothing, ever: its state is the one a
  /// PredictionWorld starts with.
  prediction::State Tick(const command::Command& command, float delta_time);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

/// Asks a replay server which captures it replays (ADR-0051): connects, sends
/// a Replay list request once connected, and keeps the list it answers with,
/// after which the server closes the connection. Like a Session it owns no
/// thread and reads no clock: the caller pumps it, and decides how long to wait.
class ReplayListQuery {
 public:
  /// Begins connecting to server; returns immediately. faults, for a test, as SessionConfig::faults.
  explicit ReplayListQuery(const link::Endpoint& server, failure::Faults* faults = nullptr);
  ~ReplayListQuery();

  ReplayListQuery(const ReplayListQuery&) = delete;
  ReplayListQuery& operator=(const ReplayListQuery&) = delete;
  ReplayListQuery(ReplayListQuery&&) = delete;
  ReplayListQuery& operator=(ReplayListQuery&&) = delete;

  /// One round of network work: connection events, the request once
  /// connected, and what has arrived.
  void Pump();

  /// The server's list, oldest capture first, once it has answered; nullopt until then.
  [[nodiscard]] const std::optional<std::vector<ReplayListing>>& List() const;

  /// Why it ended without a list: the server refused this client, or the
  /// connection ended before it answered (FailureKind::kServerUnreachable);
  /// nullopt while it may still answer, and once it has.
  [[nodiscard]] std::optional<Failure> GetFailure() const;

  /// The first local transport failure a send or receive met, once, as Session::TakeTransportFailure.
  [[nodiscard]] std::optional<failure::Failure> TakeTransportFailure();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

/// listings as lines for whoever ran `augustac --replays`: one a capture,
/// its name, when it started in UTC, how long it lasted and its players'
/// Characters, or a line saying there is none.
[[nodiscard]] std::string DescribeReplayList(const std::vector<ReplayListing>& listings);

}  // namespace augusta::harness

#endif  // AUGUSTA_HARNESS_H_
