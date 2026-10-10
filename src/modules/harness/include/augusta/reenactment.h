#ifndef AUGUSTA_REENACTMENT_H_
#define AUGUSTA_REENACTMENT_H_

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <istream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "augusta/assets.h"
#include "augusta/capture_error.h"
#include "augusta/command.h"
#include "augusta/harness.h"
#include "augusta/math.h"
#include "augusta/tick.h"

/// \file
/// A Captured player (ADR-0050): one player of a Match capture played again,
/// open-loop, against a live server by a Session of its own, as `augustac
/// --reenact <capture> --player <n>` does. ReadScript takes that player's part
/// out of a capture file, with the markers a run is compared against; a
/// Reenactment then supplies the Session's Command for each Tick, paced so the
/// server's command queue hands each captured Command to the World on the
/// Match's first tick plus its offset, and says when the run leaves or ends
/// and what it saw against what the capture holds.
///
/// It decides; it does not act. Who ticks the Session, disconnects it or
/// draws what it sees is the caller's (ClientRuntime), so all of it is tested
/// without a window or a network. A capture's records are the Networking
/// Protocol's, converted in harness_wire.h. Nothing here reads a clock or owns
/// a thread: NextCommand is the Prediction thread's and touches only the
/// pacing, the rest reads only the Script and the Server view handed in, from
/// any thread.
namespace augusta::harness {

/// A player's number in a capture: from 1, in its Match start's order, as
/// `augustac --reenact --player` names it.
using CapturedPlayer = std::uint8_t;

/// A Command a Captured player sends, at the offset in ticks from the Match's
/// first tick on which the server's queue handed it to the World.
struct CapturedCommand {
  std::uint32_t offset = 0;
  /// Its Seen time's tick, as an offset from the Match's first tick: negative
  /// for a State sent before the Match started. command.seen_tick is 0.
  std::int32_t seen_offset = 0;
  command::Command command{};
};

/// A Death the capture holds, at its offset.
struct CapturedDeath {
  std::uint32_t offset = 0;
  CapturedPlayer victim = 0;
  CapturedPlayer killer = 0;
};

/// The capture's Match end, at the offset of its last tick.
struct CapturedEnd {
  std::uint32_t offset = 0;
  /// nullopt for a Draw.
  std::optional<CapturedPlayer> winner;
};

/// One player's part of a capture, and the markers its run is compared with.
struct Script {
  /// The client pack the capture's players joined with: the only one its
  /// Commands are valid on.
  assets::PackHash client_pack{};
  std::uint8_t tick_rate_hz = 0;
  /// The player this Script plays.
  CapturedPlayer player = 0;
  /// Its Character, by its name in the scenario's manifest, and where it spawned.
  std::string character;
  math::Vec3 spawn{};
  /// Where every player of the capture spawned, by number: player n's at n - 1.
  /// A run tells its own Match's players apart by them.
  std::vector<math::Vec3> spawns;
  /// The player's Commands, in offset order, at most one an offset.
  std::vector<CapturedCommand> commands;
  /// The offset its connection ended at, if it left mid-Match.
  std::optional<std::uint32_t> leave;
  /// Every Death of the capture, anyone's, in offset order.
  std::vector<CapturedDeath> deaths;
  /// nullopt when the capture stopped before its Match end.
  std::optional<CapturedEnd> end;
  /// Whether the file ended partway through a record, which was dropped.
  bool torn = false;
};

/// Why a capture gives no Script.
struct ScriptError {
  /// Why the capture does not read (augusta/capture_file.h), or nullopt when it
  /// reads but has no player of the number asked for.
  std::optional<capture_file::ReadError> capture;

  bool operator==(const ScriptError&) const = default;
};

/// What to tell whoever runs the process about error.
[[nodiscard]] std::string DescribeScriptError(const ScriptError& error);

/// Player player's part of the capture in, as a Capturer writes one and
/// augusta/capture_file.h reads it (ADR-0050). A last record cut short is
/// dropped and reported in Script::torn.
[[nodiscard]] std::expected<Script, ScriptError> ReadScript(std::istream& in, CapturedPlayer player);

/// The server tick a Command sent now is handed to the World on, as best the
/// client can tell: the newest Authoritative State's tick, from which the
/// server hands one Command a tick, ahead of it every Command sent and not yet
/// handed (ahead, this one included), but never sooner than a Command can
/// reach it, round_trip_ticks plus one after that State.
[[nodiscard]] tick::Tick DueTick(tick::Tick newest_tick, std::uint64_t ahead, tick::Tick round_trip_ticks);

/// A round trip of ping_ms (networking::ConnectionStats) in whole ticks at
/// tick_rate_hz, rounded up; 0 for none measured yet.
[[nodiscard]] tick::Tick RoundTripTicks(int ping_ms, std::uint8_t tick_rate_hz);

/// How many Commands a Captured player keeps waiting in the server's queue at
/// most, beyond those a round trip holds in flight: about what the client's
/// pacing aims for (tick::kTargetQueuedCommands), far below the server's own
/// bound (server::kMaxQueuedCommands), so a client that ticks faster than the
/// server never overruns the queue and has a Command dropped.
inline constexpr std::uint64_t kMaxCommandsQueued = 2;

/// Which of a Script's Commands goes out on each Tick (ADR-0050), one for
/// each server tick at most, never two for the same one. Each captured Command
/// is sent once, in order, for the tick it is due to be handed to the World on,
/// its offset, or as soon after as it can: a late one is never dropped, and
/// the gaps after it close up. For a tick with none due, a filler keeps the
/// server's queue in step as its own hold would have: the last Command without
/// its fire or reload, its movement for up to command::kMaxHeldTime, then none.
/// Nothing is sent for the tick of the captured Leave or after it.
class Pacer {
 public:
  /// Paces commands, in offset order, for a server ticking at tick_rate_hz,
  /// up to leave, the offset of the player's Leave, if it has one.
  Pacer(std::vector<CapturedCommand> commands, std::uint8_t tick_rate_hz, std::optional<std::uint32_t> leave);

  /// The Command to send now, if the server is due to hand it to the World on
  /// due_tick, of a Match whose first tick is first_tick: its Seen time's tick
  /// is due_tick less the delay the capture had, so Lag compensation judges it
  /// as far back as in the playtest. nullopt to send none: a Command already
  /// went for due_tick or one after it, or the Leave has come.
  [[nodiscard]] std::optional<command::Command> Next(tick::Tick first_tick, tick::Tick due_tick);

  /// How many of the Script's Commands have gone out.
  [[nodiscard]] std::size_t Sent() const { return next_; }

  /// Whether every tick before the Leave has had its Command sent, so nothing
  /// more is: false without a Leave.
  [[nodiscard]] bool DoneBeforeLeave() const { return leave_.has_value() && slot_ >= *leave_; }

 private:
  // The filler for a tick with nothing due.
  [[nodiscard]] command::Command Filler(tick::Tick due_tick);

  std::vector<CapturedCommand> commands_;
  int max_held_ticks_;
  std::optional<std::uint32_t> leave_;
  std::size_t next_ = 0;
  // The offset of the first tick no Command has been sent for yet.
  std::uint64_t slot_ = 0;
  // The last Command sent, the delay of its Seen time, and how many fillers
  // have held its movement since.
  std::optional<command::Command> last_;
  tick::Tick last_delay_ = 0;
  int held_ = 0;
};

/// What a Captured player's run does next, as Reenactment::Check decides.
enum class Progress : std::uint8_t {
  /// Not yet in its Match, or playing it.
  kPlaying,
  /// Its captured Leave has come, and every Command it sent before it has been
  /// handed to the World: it disconnects, and the run ends.
  kLeave,
  /// The capture's Match end or the server's has come: the run ends.
  kEnded,
};

/// A Death the server told, at the tick of the newest State when it was taken.
struct ObservedDeath {
  tick::Tick tick = 0;
  EntityId victim{};
  EntityId killer{};
};

/// A captured Death beside the one the server told of the same victim, the
/// latter in the capture's terms: its players by their numbers in the capture
/// (0 for one the capture lacks), its tick as an offset from the Match's
/// first. Either may be missing, not both.
struct PairedDeath {
  std::optional<CapturedDeath> captured;
  std::optional<CapturedDeath> observed;
};

/// Each of the script's Deaths, in its order, beside the first of observed of
/// the same victim not taken by an earlier one, then each of observed none
/// took. view is the run's, with the Match start that names its players:
/// each is told apart by where it spawned (Script::spawns).
[[nodiscard]] std::vector<PairedDeath> PairDeaths(const Script& script, const ServerView& view,
                                                  const std::vector<ObservedDeath>& observed);

/// One Captured player's run: its Script and its pacing.
class Reenactment {
 public:
  explicit Reenactment(Script script);

  [[nodiscard]] const Script& GetScript() const { return script_; }

  /// The Command for the Session's next Tick (Prediction thread), from view
  /// and the sequence that Tick sends under (Session::NextSequence), with
  /// round_trip_ticks the connection's round trip in ticks: in the first Match
  /// the client is in, the pacing's, or nullopt to skip the Tick, when
  /// kMaxCommandsQueued are already waiting beyond a round trip's or the
  /// pacing has none to send. Outside it, an idle Command, which a Session
  /// outside a match does not send. Only the first Match is reenacted: the
  /// run ends with it.
  [[nodiscard]] std::optional<command::Command> NextCommand(const ServerView& view, command::Sequence next_sequence,
                                                            tick::Tick round_trip_ticks);

  /// Whether, as of view, the run plays on, leaves at its captured Leave, or
  /// has reached the capture's Match end or the server's. Any thread.
  [[nodiscard]] Progress Check(const ServerView& view, tick::Tick round_trip_ticks) const;

  /// The lines a run logs when it ends as how says (ADR-0050), as logfmt
  /// fields to follow `subsystem=reenactment`: each of PairDeaths' pairs,
  /// then the capture's Match end beside the server's. What came after a
  /// Leave, which the client no longer saw, is said to be so (observed=left).
  /// last_tick is the newest server tick the run saw.
  [[nodiscard]] std::vector<std::string> Outcome(const ServerView& view, const std::vector<ObservedDeath>& observed,
                                                 tick::Tick last_tick, Progress how) const;

  /// The line a run logs at admission when the server's Player count is not
  /// the capture's number of players, which it plays on regardless; nullopt
  /// when they agree.
  [[nodiscard]] std::optional<std::string> PlayerCountMismatch(const Admission& admission) const;

 private:
  const Script script_;
  // Prediction thread only: the pacing, and how many Commands it has sent in its Match.
  Pacer pacer_;
  std::uint64_t sent_in_match_ = 0;
  // Written on the Prediction thread, read by Check: the sequence of the last
  // Command sent, 0 for none, and whether the pacing is done before the Leave.
  std::atomic<command::Sequence> last_sent_{0};
  std::atomic<bool> done_before_leave_{false};
};

}  // namespace augusta::harness

#endif  // AUGUSTA_REENACTMENT_H_
