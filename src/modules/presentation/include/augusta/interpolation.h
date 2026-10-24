#ifndef AUGUSTA_INTERPOLATION_H_
#define AUGUSTA_INTERPOLATION_H_

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include "augusta/math.h"
#include "augusta/physics.h"
#include "augusta/tick.h"

/// \file
/// Remote-player interpolation (ADR-0024's Interpolation phase): what
/// PresentationWorld shows for every player but the local one. Neither
/// PredictionWorld nor client-side reconciliation ever runs for another
/// player's body - the client never has their input, only what the server's
/// Authoritative State reports of them, at most once per server tick and
/// arriving irregularly over the network relative to the render frame rate.
/// Showing the newest report directly would make a remote player step at
/// network arrival times, jitter included. Instead, each update is placed on
/// the server's own timeline (its tick × the tick duration), a short ring of
/// them is kept per remote entity, and each frame shows the point
/// kInterpolationDelay behind a render-side clock aligned to that timeline
/// (ServerClock), interpolated between the two updates surrounding it - smooth
/// motion, independent of when updates arrive or frames run.
///
/// Pure - no clock, no ECS, no network - so it is tested on its own;
/// PresentationWorld feeds it each frame with its render frame's elapsed time
/// and every Authoritative State update since the last frame (see
/// presentation.cpp).
namespace augusta::presentation {

/// The server's name for one dynamic body (CONTEXT.md, "Entity ID"), as
/// presentation knows it: ClientRuntime converts the harness's own at its edge
/// (see World::RunFrame in presentation.h), so this module does not depend on
/// the network session.
enum class EntityId : std::uint32_t {};

/// How far behind the server time ServerClock estimates remote players are
/// shown, in seconds - enough that the two updates surrounding the sample
/// point have normally both arrived rather than the newer one still being
/// awaited.
inline constexpr double kInterpolationDelay = 0.1;

/// The fastest tick rate a server can run at, in Hz: it is one byte on the
/// wire (ADR-0038).
inline constexpr double kFastestTickRateHz = std::numeric_limits<std::uint8_t>::max();

/// How many updates RemoteInterpolator keeps per remote entity: enough to
/// cover kInterpolationDelay, rounded up, plus two, at kFastestTickRateHz - so
/// at any tick rate.
inline constexpr std::size_t kUpdatesKept = static_cast<std::size_t>(kInterpolationDelay * kFastestTickRateHz) + 3;

/// The render side's estimate of the server's current time, in seconds on the
/// server's timeline (tick × tick duration). Aligned to the first update's
/// server time, then advanced by each render frame's elapsed time and pulled
/// back into step with each newer update slowly - never backwards, never more
/// than kSlew faster or slower than real time while within kFastSlewBeyond of
/// it, kFastSlew beyond - so neither network jitter nor frame timing shows as
/// uneven motion, yet a latency spike is caught up with within seconds. Pure:
/// the caller supplies both the elapsed time and the updates' server times.
class ServerClock {
 public:
  /// The most the clock runs faster or slower than real time to get back into
  /// step while within kFastSlewBeyond of the newest update's server time, as
  /// a fraction of each frame's elapsed time: ordinary jitter never moves it
  /// faster, so remote players never visibly speed up or slow down.
  static constexpr double kSlew = 0.02;
  /// The same, further off than kFastSlewBeyond: after a latency spike, remote
  /// players are held at the newest update for under a second rather than
  /// several (prototypes/replication-interpolation.html).
  static constexpr double kFastSlew = 0.10;
  /// How far off the newest update's server time the clock is, in seconds,
  /// beyond which it slews at kFastSlew rather than kSlew, whether ahead or
  /// behind.
  static constexpr double kFastSlewBeyond = 0.05;
  /// An update's server time further than this from the clock, in seconds,
  /// realigns it at once instead: the server's timeline itself jumped.
  static constexpr double kResyncThreshold = 0.25;

  /// Advances the clock by one render frame's elapsed seconds, adjusted toward
  /// the newest observed server time by at most kSlew of them, or kFastSlew
  /// while further off than kFastSlewBeyond. Does nothing before the first
  /// Observe.
  void Advance(double elapsed);

  /// Takes server_time, an update's time on the server's timeline, as it
  /// arrives: aligns the clock to it the first time (or off by more than
  /// kResyncThreshold), otherwise leaves Advance to catch up with it.
  void Observe(double server_time);

  /// Forgets the alignment - outside a match there is no timeline to follow.
  void Reset();

  /// The estimated current server time, or nullopt before the first Observe
  /// (or since Reset).
  [[nodiscard]] std::optional<double> Now() const;

  /// The interpolation buffer's slack, in seconds: how far the newest server
  /// time observed since the clock last aligned is ahead of the point remote
  /// players are sampled at, kInterpolationDelay behind Now(). Positive while
  /// updates wait to be shown; zero or below once that point has reached the
  /// newest, which RemoteInterpolator::Sample then holds. Nullopt while Now()
  /// is.
  [[nodiscard]] std::optional<double> Slack() const;

 private:
  std::optional<double> now_;
  // The newest server time observed since the clock last aligned.
  double newest_ = 0.0;
  // How far the newest observed server time was ahead of the clock (negative
  // if behind), less what Advance has since caught up.
  double behind_ = 0.0;
};

/// The Seen time of a frame: what of the server's timeline it shows the other
/// players at, as the tick of an Authoritative State update and how far from it
/// to the next one, 0 to 1. It is what a Command sampled on that frame reports
/// to the server, which judges the Command's shots against the players as they
/// were then (ADR-0044).
struct SeenTime {
  tick::Tick tick = 0;
  float fraction = 0.0F;
};

/// The Seen time of a frame that samples at sample_time: sample_time, in seconds
/// on the server's timeline, as the tick it falls on or after, of ticks
/// tick_duration seconds long, and how far past it it is. Held within
/// oldest_tick and newest_tick, the first and the last update there is to show:
/// before or past them a frame shows that update itself (RemoteInterpolator::Sample).
[[nodiscard]] SeenTime SeenTimeAt(double sample_time, double tick_duration, tick::Tick oldest_tick,
                                  tick::Tick newest_tick);

/// Where a player aims: its view's yaw and pitch, in radians
/// (command::Command's), and whether it holds ADS. The local player's as of a
/// render frame, newer than its latest Command's, which is only as new as the
/// last tick; a remote player's as an Authoritative State tells it.
struct Aim {
  float yaw = 0.0F;
  float pitch = 0.0F;
  bool ads = false;
};

/// One remote player's body as shown this frame: position, velocity and the
/// view's pitch linearly interpolated between the two surrounding updates, and
/// facing along the shorter arc between them (math::LerpAngle, as the server
/// poses its hitboxes, ADR-0044); stance and ADS (discrete states, not
/// numbers) taken from whichever of the two is nearer the sample point.
struct RemoteBody {
  math::Vec3 position{};
  math::Vec3 velocity{};
  /// Where the body faces: the yaw of its player's view, in radians (command::Command).
  float yaw = 0.0F;
  /// Where its player's view looks up or down, in radians (command::Command).
  float pitch = 0.0F;
  physics::Stance stance = physics::Stance::kStanding;
  /// Whether its player aims down sights.
  bool ads = false;
};

/// One entity's interpolated body, as Sample returns it.
struct RemotePlayer {
  EntityId entity{};
  RemoteBody body{};
  /// The character it is drawn as, by its path, from Match start; empty if
  /// unknown. PresentationWorld fills it in: the interpolator knows only bodies.
  std::string character;
};

/// Buffers the Authoritative State's per-entity updates for every player
/// but the local one and interpolates between them. One instance covers the
/// whole match; bodies come and go as players leave.
class RemoteInterpolator {
 public:
  /// Records entity's body, and where its player aims, as of server_time, the
  /// update's time on the server's timeline (its tick × the tick duration) -
  /// not when it arrived.
  /// Becomes this entity's newest update; beyond kUpdatesKept, its oldest is
  /// dropped. A server_time at or before the entity's current newest is
  /// ignored: out-of-order or repeated Authoritative State cannot move
  /// interpolation backward.
  void Record(EntityId entity, double server_time, const physics::BodyState& body, const Aim& aim);

  /// Forgets every buffered entity not present in current - the disconnect
  /// case, driven by each Authoritative State's full player list rather than
  /// a separate leave message.
  void Sync(std::span<const EntityId> current);

  /// Every buffered entity's body at sample_time, on the server's timeline:
  /// interpolated between the two kept updates surrounding it, or held at the
  /// nearer end if sample_time falls outside them (before the oldest kept, or
  /// a gap past the newest while nothing new has arrived - no extrapolation).
  [[nodiscard]] std::vector<RemotePlayer> Sample(double sample_time) const;

 private:
  struct Update {
    double server_time = 0.0;
    physics::BodyState body{};
    Aim aim{};
  };
  // One entity's newest kUpdatesKept updates, oldest first, overwriting the
  // oldest in place once full rather than shifting the rest down per update.
  class History {
   public:
    explicit History(const Update& first);
    // Appends update as the newest, dropping the oldest beyond kUpdatesKept.
    void Push(const Update& update);
    [[nodiscard]] std::size_t Size() const { return slots_.size(); }
    // The index-th oldest kept update, index < Size().
    [[nodiscard]] const Update& operator[](std::size_t index) const;
    [[nodiscard]] const Update& Newest() const { return (*this)[Size() - 1]; }

   private:
    // Never empty; grows to kUpdatesKept, then wraps.
    std::vector<Update> slots_;
    // Where the oldest kept update is in slots_: 0 until it first wraps.
    std::size_t oldest_ = 0;
  };
  struct Buffered {
    EntityId entity{};
    History updates;
    // Whether the list Sync is applying names this entity; false outside Sync.
    bool listed = false;
  };

  // Every buffered entity, in the order each was first recorded (since it was
  // last forgotten): the order Sample returns them in.
  std::vector<Buffered> bodies_;
  // Each buffered entity's position in bodies_, so Record and Sync find it
  // without searching every other entity's buffer for each. Always names
  // exactly bodies_'s entities at their current positions.
  std::unordered_map<EntityId, std::size_t> index_;
};

}  // namespace augusta::presentation

#endif  // AUGUSTA_INTERPOLATION_H_
