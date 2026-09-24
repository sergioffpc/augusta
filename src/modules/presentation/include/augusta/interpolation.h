#ifndef AUGUSTA_INTERPOLATION_H_
#define AUGUSTA_INTERPOLATION_H_

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "augusta/math.h"
#include "augusta/physics.h"
#include "augusta/protocol.h"

// Remote-player interpolation (ADR-0024's Interpolation phase): what
// PresentationWorld shows for every player but the local one. Neither
// PredictionWorld nor client-side reconciliation ever runs for another
// player's body - the client never has their input, only what the server's
// Authoritative State reports of them, at most once per server tick and
// possibly irregular over the network relative to the render frame rate.
// Showing the newest report directly would make a remote player jump between
// updates. Instead, this buffers each remote session's last two distinct
// updates and renders a point kInterpolationDelay behind the newest one it
// has seen, interpolated between the two updates surrounding it - smooth
// motion, independent of how often the caller happens to sample it.
//
// Pure - no clock, no ECS, no network - so it is tested on its own;
// PresentationWorld feeds it each frame with its own running clock and the
// newest Authoritative State (see presentation.cpp).
namespace augusta::presentation {

/// How far behind the newest recorded update remote players are shown, in
/// seconds - enough that the two updates surrounding the render point are
/// normally both already buffered rather than the newest one still being
/// awaited.
inline constexpr float kInterpolationDelay = 0.1F;

/// One remote player's body as shown this frame: position and velocity
/// linearly interpolated between the two surrounding updates; stance (a
/// discrete state, not a number) taken from whichever of the two is nearer
/// the render point.
struct RemoteBody {
  math::Vec3 position{};
  math::Vec3 velocity{};
  physics::Stance stance = physics::Stance::kStanding;
};

/// One session's interpolated body, as Sample returns it.
struct RemotePlayer {
  protocol::SessionIdWire session{};
  RemoteBody body{};
  /// The character index it is drawn as, from Match start; 0 if unknown.
  /// PresentationWorld fills it in: the interpolator knows only bodies.
  std::uint8_t character = 0;
};

/// Buffers the Authoritative State's per-session updates for every player
/// but the local one and interpolates between them. One instance covers the
/// whole match; sessions come and go as players join and leave.
class RemoteInterpolator {
 public:
  /// Records session's body as of timestamp, on the caller's own clock (not
  /// the server's tick number - PresentationWorld converts, see
  /// presentation.cpp). Becomes this session's newest buffered update; the
  /// previous newest becomes the one behind it. A timestamp at or before the
  /// session's current newest is ignored: out-of-order or repeated
  /// Authoritative State cannot move interpolation backward.
  void Record(protocol::SessionIdWire session, float timestamp, const physics::BodyState& body);

  /// Forgets every buffered session not present in current - the disconnect
  /// case, driven by each Authoritative State's full player list rather than
  /// a separate leave message.
  void Sync(std::span<const protocol::SessionIdWire> current);

  /// Every buffered session's body at render_time: interpolated between the
  /// two updates surrounding it if both are buffered, held at the nearer end
  /// if render_time falls outside the buffered range (before the first
  /// update, or a gap past the newest one while nothing new has arrived), or
  /// shown as recorded if only one update has ever been buffered for that
  /// session.
  [[nodiscard]] std::vector<RemotePlayer> Sample(float render_time) const;

 private:
  struct Update {
    float timestamp = 0.0F;
    physics::BodyState body{};
  };
  struct Buffered {
    protocol::SessionIdWire session{};
    std::optional<Update> previous;
    Update latest;
  };

  std::vector<Buffered> sessions_;
};

}  // namespace augusta::presentation

#endif  // AUGUSTA_INTERPOLATION_H_
