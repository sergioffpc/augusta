#ifndef AUGUSTA_AGENT_AGENT_H_
#define AUGUSTA_AGENT_AGENT_H_

#include <atomic>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "augusta/assets.h"
#include "augusta/command.h"
#include "augusta/harness.h"
#include "augusta/intent.h"
#include "augusta/map.h"
#include "augusta/netcode_stats.h"
#include "augusta/networking.h"
#include "augusta/physics.h"
#include "augusta/runner.h"
#include "report.h"

/// \file
/// An Agent (CONTEXT.md, ADR-0052): a player with no one at the keyboard, on a
/// harness::Session under a harness::Runner of its own, its Commands made by an
/// IntentExecutor from the Intents its script sets. What the Python module
/// (module.cpp) wraps; nothing here knows of Python, so the Runner's threads
/// never touch the GIL.
namespace augusta::agent {

/// A client pack, verified, and its Map's collision: loaded once for every
/// Agent of a script.
struct LoadedPack {
  assets::PackHash hash{};
  std::vector<physics::CollisionMesh> map;
};

/// Why LoadPack failed: the pack or its key did not verify, or its Map did not load.
using LoadPackError = std::variant<assets::VerifiedPackError, map::MapError>;

/// Verifies the client pack at pack_path with the key at public_key_path and
/// loads its Map's collision, as augustac does before it connects.
[[nodiscard]] std::expected<LoadedPack, LoadPackError> LoadPack(const std::filesystem::path& pack_path,
                                                                const std::filesystem::path& public_key_path);

/// What to tell the script about why LoadPack failed, for the same paths.
[[nodiscard]] std::string DescribeLoadPackError(const LoadPackError& error, const std::filesystem::path& pack_path,
                                                const std::filesystem::path& public_key_path);

/// What an Agent has been told as of one moment, and its own body as its
/// prediction last left it.
struct AgentView {
  /// Never null.
  std::shared_ptr<const harness::ServerView> server;
  physics::BodyState own{};
};

/// What an Agent's prediction and fire have come to so far, and its link.
struct AgentStats {
  harness::NetcodeStats netcode;
  /// As of its last round of network work while connected; nullopt before it
  /// first connected. Its max_jitter_us is that round's.
  std::optional<networking::ConnectionStats> connection;
};

/// When an Agent reports Ready.
enum class Readiness : std::uint8_t {
  /// For every Roster it is sent: it has nothing to load to draw anyone.
  kForEveryRoster,
  /// Only when told to (Agent::Ready), as a test of the Lobby needs.
  kWhenTold,
};

/// One Agent, connecting from construction until destruction. It reports to
/// its sink, from the Runner's threads, every Shot, Hit confirmation, Death,
/// Match start and Match end it is told of, in the order they arrived, every
/// ending of an Intent it was given, and its failure, once. Its calls are safe
/// from any thread and never wait for a tick.
class Agent {
 public:
  /// Connects to server as character, predicting against pack's Map, and
  /// reports to report. Throws std::runtime_error if physics rejects a mesh
  /// of the Map.
  Agent(const std::string& server, const LoadedPack& pack, const std::string& character, Readiness readiness,
        ReportSink report);

  /// Not copyable or movable: the Runner's threads hold references to it.
  Agent(const Agent&) = delete;
  Agent& operator=(const Agent&) = delete;
  Agent(Agent&&) = delete;
  Agent& operator=(Agent&&) = delete;
  ~Agent() = default;

  /// Each sets its channel's Intent (IntentExecutor), and returns the number
  /// its ending is reported under (IntentEnded).
  std::uint64_t SetMovement(harness::MovementIntent intent);
  std::uint64_t SetAim(harness::AimIntent intent);
  std::uint64_t SetTrigger(harness::TriggerIntent intent);
  /// Holds command on every channel (IntentExecutor::SetRaw).
  std::uint64_t SetRaw(const command::Command& command);

  /// Each empties its channel, ending its Intent Replaced.
  void ClearMovement();
  void ClearAim();
  void ClearTrigger();

  /// Reports Ready for the newest Roster, as kForEveryRoster does for each;
  /// sends nothing outside the Lobby.
  void Ready();

  /// What it has been told so far, and its body as of its last Tick: a copy,
  /// which nothing changes once taken.
  [[nodiscard]] AgentView View() const;

  /// What its prediction and fire have come to so far, and its link.
  [[nodiscard]] AgentStats Stats() const;

 private:
  // An Intent's number, and what reports its ending under it.
  struct NumberedIntent {
    std::uint64_t number = 0;
    harness::OnIntentEnded on_ended;
  };
  NumberedIntent NextIntent();
  [[nodiscard]] physics::BodyState OwnBody() const;
  // What the Prediction thread does after each Tick.
  void AfterTick(const harness::PredictedTick& tick);
  // What the Network I/O thread does after each round.
  void AfterNetworkRound();
  // Reports Ready for a Roster it has not yet.
  void ReportReadyForNewRoster(const harness::ServerView& view);
  // Reports the events view tells of that it has not yet.
  void ReportEvents(const harness::ServerView& view);
  // Reports the failure described, unless one has been.
  void ReportFailure(std::string description);

  const Readiness readiness_;
  const ReportSink report_;
  harness::Session session_;
  harness::IntentExecutor intents_;
  // Written on the Prediction thread each Tick, read by View on any.
  mutable std::mutex own_mutex_;
  physics::BodyState own_{};
  // Added to on the Prediction and Network I/O threads, read by Stats on any.
  mutable std::mutex stats_mutex_;
  harness::NetcodeTally tally_;
  std::optional<networking::ConnectionStats> connection_;
  std::atomic<std::uint64_t> next_intent_{0};
  std::atomic<bool> failure_reported_{false};
  // The Network I/O thread's only.
  std::optional<std::uint32_t> ready_version_;
  // The count of Matches started as of the last Match start, and of the
  // Match whose end, it has reported.
  std::uint32_t started_reported_ = 0;
  std::uint32_t ended_reported_ = 0;
  // Last, so its threads are joined before anything they use goes.
  std::optional<harness::Runner> runner_;
};

}  // namespace augusta::agent

#endif  // AUGUSTA_AGENT_AGENT_H_
