#ifndef AUGUSTA_SERVER_REPLAY_H_
#define AUGUSTA_SERVER_REPLAY_H_

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "augusta/math.h"
#include "augusta/simulation.h"
#include "augusta/tick.h"
#include "capture.h"
#include "command_queue.h"
#include "match.h"

/// \file
/// A Replay (ADR-0051): a Match capture handed, tick by tick, to a fresh
/// SimulationWorld, as the live server handed its World what its players did,
/// and checked against the capture's markers. Replay re-runs one capture and
/// ReplayCheck compares each Death and the Match end the World resolves with
/// the capture's (NFR-09). Neither touches a socket, a file or a clock:
/// server::ReplayServer reads the capture, builds the World, ticks each Replay
/// on the Simulation thread and sends what it resolves to the Replay's viewer.
/// A player is named by its number in the capture (CapturedPlayer), and its
/// body in the World by the same number, so a Replay names no tick and no
/// entity of the server that made the capture.
namespace augusta::server {

/// A marker of a Replay that is not the capture's: on the build that made
/// the capture, a non-determinism to chase; on another, possibly floats
/// rounding differently (ADR-0051).
struct Divergence {
  enum class Marker : std::uint8_t { kDeath, kMatchEnd };

  Marker marker = Marker::kDeath;
  /// The tick it is on, as an offset from the Match's first.
  std::uint32_t offset = 0;
  /// kDeath: the Deaths the capture has on the tick and those the World
  /// resolved on it, each ordered by victim.
  std::vector<CapturedDeath> captured_deaths;
  std::vector<CapturedDeath> resolved_deaths;
  /// kMatchEnd: the Match end the capture has on the tick and the one the
  /// World's Game policy decided on it; nullopt for none.
  std::optional<CapturedMatchEnd> captured_end;
  std::optional<CapturedMatchEnd> resolved_end;

  bool operator==(const Divergence&) const = default;
};

/// divergence as one log line's fields: what differs, on which tick, and both sides.
[[nodiscard]] std::string DescribeDivergence(const Divergence& divergence);

/// Compares what a Replay resolves with the capture's markers, tick by tick,
/// and finds the first that differs (NFR-09): each tick's Deaths, victim and
/// killer, and the Match end, its tick and winner. A capture that ends partway
/// (Capture::torn) has no Match end to compare. Pure.
class ReplayCheck {
 public:
  explicit ReplayCheck(const Capture& capture);

  /// The tick at offset resolved deaths and, if Game policy ended the Match on
  /// it, end. Returns the first marker that differs, once: nullopt on every
  /// call after it, as on every call before one.
  [[nodiscard]] std::optional<Divergence> Check(std::uint32_t offset, std::vector<CapturedDeath> deaths,
                                                const std::optional<CapturedMatchEnd>& end);

 private:
  std::unordered_map<std::uint32_t, std::vector<CapturedDeath>> deaths_;
  std::optional<std::uint32_t> end_offset_;
  CapturedMatchEnd end_;
  bool reported_ = false;
};

/// Where one player of a Replay looked on a tick, from the Command the World
/// ran: what its Replay viewer is shown of it, which no Authoritative State
/// carries (ADR-0051).
struct PlayerView {
  EntityId entity{};
  /// In radians, as command::Command's.
  float pitch = 0.0F;
  bool ads = false;
};

/// How a Replay ended, for its viewer to be told.
struct ReplayEnd {
  /// The winner's session, as the capture names it; nullopt for a Draw.
  std::optional<SessionId> winner;
};

/// One tick of a Replay, as it ran.
struct ReplayTick {
  simulation::TickResult result;
  /// Every player handed a Command on the tick, in the capture's order.
  std::vector<PlayerView> views;
  /// Set on the Replay's last tick.
  std::optional<ReplayEnd> end;
  /// The first marker the Replay resolved otherwise than the capture, on the
  /// tick it is found on.
  std::optional<Divergence> divergence;
};

/// One capture re-run on a fresh SimulationWorld (ADR-0051), one tick a Step:
///
/// 1. Before its first tick, a Match start of the capture's players, each at
///    its captured spawn, with Game policy's `assign_spawns` not asked.
/// 2. Before each tick, the bodies of the players whose Leave is on it are
///    taken out, as the live server took them out.
/// 3. Each player's Commands go through a CommandQueue of its own, each queued
///    just before the tick it was handed to the World on, so the queue holds
///    and idles on the ticks with nothing new as the live server's did; each
///    Seen time's tick is moved onto the Replay's ticks.
///
/// It ends at the World's Match end, or at the capture's if the World has not
/// ended it by then; a capture that ends partway ends at its last record.
class Replay {
 public:
  /// A Replay of capture, which ReadCapture read, on world, a fresh
  /// SimulationWorld of the capture's server pack ticking at its tick rate,
  /// with characters, the scenario's by path, which must hold every Join's
  /// Character. Starts its Match.
  Replay(Capture capture, simulation::World world,
         const std::unordered_map<std::string, simulation::Character>& characters);

  /// Who the Match starts with, in the capture's order, each body named by its
  /// player's number, and where each spawned: what the viewer is sent as Match start.
  [[nodiscard]] const MatchStart& Start() const { return start_; }
  [[nodiscard]] const std::vector<math::Vec3>& Spawns() const { return spawns_; }

  /// The World's tick the Match starts on, its first: offset 0 of the capture.
  [[nodiscard]] tick::Tick FirstTick() const { return 1; }

  /// Runs the Replay's next tick. Must not be called once it has ended.
  [[nodiscard]] ReplayTick Step();

  [[nodiscard]] bool Ended() const { return ended_; }

 private:
  struct Player {
    CapturedPlayer number = 0;
    CommandQueue commands;
    command::Sequence sequence = 0;
    bool left = false;
  };

  // Hands the World what the tick at offset is handed before it runs: the
  // Leaves before it, and each player's Commands queued.
  void HandBefore(std::uint32_t offset);
  [[nodiscard]] std::vector<simulation::PlayerCommand> TakeCommands(std::vector<PlayerView>& views);
  [[nodiscard]] std::optional<ReplayEnd> EndOf(std::uint32_t offset, const simulation::TickResult& result) const;

  Capture capture_;
  simulation::World world_;
  ReplayCheck check_;
  MatchStart start_;
  std::vector<math::Vec3> spawns_;
  std::vector<Player> players_;
  // The next record of capture_ not yet handed to the World.
  std::size_t next_record_ = 0;
  // The offset of the tick the next Step runs.
  std::uint32_t offset_ = 0;
  // The capture's last tick: its Match end's, or its last record's for a torn one.
  std::uint32_t last_offset_ = 0;
  bool ended_ = false;
};

}  // namespace augusta::server

#endif  // AUGUSTA_SERVER_REPLAY_H_
