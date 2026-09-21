#ifndef AUGUSTA_HARNESS_H_
#define AUGUSTA_HARNESS_H_

#include <expected>
#include <memory>
#include <optional>
#include <string>

#include "augusta/input.h"
#include "augusta/networking.h"
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
// ExchangeMessages, GetState and GetStats from the Network I/O thread, and Tick
// from the Prediction thread (the transport is safe to send from both).
namespace augusta::harness {

/// What a Session needs to connect and predict.
struct SessionConfig {
  /// Every player body's stamina rules, shared with the server's SimulationWorld.
  physics::StaminaConfig stamina{};
  /// The dedicated server to connect to (US-01).
  networking::Endpoint server{};
  /// The engine version to present when joining; the server admits only its own.
  std::string engine_version = std::string(EngineVersion());
};

/// The client's network connection and PredictionWorld, without a window or a GPU.
class Session {
 public:
  /// Constructs the connection and an empty PredictionWorld; connects to nothing
  /// yet. Load the map with AddStaticMesh before the first Tick.
  explicit Session(const SessionConfig& config);
  ~Session();

  // Not copyable or movable: owns a live network connection.
  Session(const Session&) = delete;
  Session& operator=(const Session&) = delete;
  Session(Session&&) = delete;
  Session& operator=(Session&&) = delete;

  /// Adds a piece of the map's collision (as built by augusta::map from the
  /// client pack) to the PredictionWorld's physics. Call before the first Tick,
  /// from the thread that will Tick: a body that has already ticked has been
  /// predicted without it, and reconciliation cannot account for that.
  std::expected<void, physics::StaticMeshError> AddStaticMesh(const physics::StaticMesh& mesh);

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
  [[nodiscard]] networking::ConnectionState GetState() const;

  /// The connection's quality numbers, or nullopt if not connected.
  [[nodiscard]] std::optional<networking::ConnectionStats> GetStats() const;

  /// The session the server assigned once it admitted this client, or nullopt
  /// until then. Set by ExchangeMessages; safe to read from any thread.
  [[nodiscard]] std::optional<protocol::SessionId> GetSessionId() const;

  /// Why the server refused this client, or nullopt if it has not. Set by
  /// ExchangeMessages; safe to read from any thread.
  [[nodiscard]] std::optional<protocol::JoinRefusal> GetRefusal() const;

  /// The newest Authoritative State received from the server, or nullopt until
  /// one arrives. Set by ExchangeMessages; safe to read from any thread.
  [[nodiscard]] std::optional<protocol::AuthoritativeState> GetAuthoritativeState() const;

  /// Runs one fixed tick of PredictionWorld for command and returns its state.
  /// Once the server has admitted this client, the command goes to it under the
  /// next sequence, together with the recent commands the server has not yet
  /// acknowledged, and the prediction is reconciled against what the server
  /// last said about this client's player.
  prediction::State Tick(const input::Command& command, float delta_time);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace augusta::harness

#endif  // AUGUSTA_HARNESS_H_
