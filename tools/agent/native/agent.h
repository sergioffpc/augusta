#ifndef AUGUSTA_AGENT_AGENT_H_
#define AUGUSTA_AGENT_AGENT_H_

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
#include "augusta/physics.h"
#include "augusta/runner.h"

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

/// One Agent, connecting from construction until destruction. It reports Ready
/// for every Roster it is sent: it has nothing to load to draw anyone. Its
/// calls are safe from any thread and never wait for a tick.
class Agent {
 public:
  /// Connects to server as character, predicting against pack's Map. Throws
  /// std::runtime_error if physics rejects a mesh of the Map.
  Agent(const std::string& server, const LoadedPack& pack, const std::string& character);

  /// Not copyable or movable: the Runner's threads hold references to it.
  Agent(const Agent&) = delete;
  Agent& operator=(const Agent&) = delete;
  Agent(Agent&&) = delete;
  Agent& operator=(Agent&&) = delete;
  ~Agent() = default;

  /// Holds command on every channel (IntentExecutor::SetRaw).
  void SetRaw(const command::Command& command);

  /// What it has been told so far, and its body as of its last Tick: a copy,
  /// which nothing changes once taken.
  [[nodiscard]] AgentView View() const;

  /// Why the Agent stopped playing, as the sentence augustac would show, or
  /// nullopt while it has not.
  [[nodiscard]] std::optional<std::string> Failure() const;

 private:
  [[nodiscard]] physics::BodyState OwnBody() const;
  // Reports Ready for a Roster it has not yet. The Runner's Network I/O
  // thread, each round.
  void ReportReadyForNewRoster();

  harness::Session session_;
  harness::IntentExecutor intents_;
  // Written on the Prediction thread each Tick, read by View on any.
  mutable std::mutex own_mutex_;
  physics::BodyState own_{};
  // The Network I/O thread's only.
  std::optional<std::uint32_t> ready_version_;
  // Last, so its threads are joined before anything they use goes.
  std::optional<harness::Runner> runner_;
};

}  // namespace augusta::agent

#endif  // AUGUSTA_AGENT_AGENT_H_
