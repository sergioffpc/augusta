#ifndef AUGUSTA_CLIENT_SESSION_H_
#define AUGUSTA_CLIENT_SESSION_H_

#include <memory>
#include <optional>
#include <vector>

#include "augusta/input.h"
#include "augusta/networking.h"
#include "augusta/physics.h"
#include "augusta/prediction.h"

// augusta::client_session is the client's connection to a match without the
// parts that need a window or a GPU: the network connection and the
// PredictionWorld (ADR-0021, ADR-0024), and nothing of Input, Renderer, Audio
// or PresentationWorld. ClientRuntime (src/client) owns one and drives it from
// its Prediction and Network I/O threads (ADR-0005); a test drives it by hand,
// so client/server behavior can be checked in CI with no display.
//
// Nothing here owns a thread or reads a clock: the caller decides when the
// network is serviced (PumpEvents, ExchangeMessages) and when a tick happens
// (Tick), and hands in the input for that tick. The two are meant for two
// different threads, as in ClientRuntime: Connect, Disconnect, PumpEvents,
// ExchangeMessages, GetState and GetStats from the Network I/O thread, and Tick
// from the Prediction thread.
namespace augusta::client_session {

/// What a Session needs to connect and predict.
struct SessionConfig {
  /// Every player body's stamina rules, shared with the server's SimulationWorld.
  physics::StaminaConfig stamina{};
  /// The dedicated server to connect to (US-01).
  networking::Endpoint server{};
  /// The level's collision, as built by augusta::level from the client pack.
  std::vector<physics::StaticMesh> level{};
};

/// The client's network connection and PredictionWorld, without a window or a GPU.
class Session {
 public:
  /// Constructs the connection and the PredictionWorld with the level's collision;
  /// connects to nothing yet. Throws std::runtime_error if a level mesh is rejected.
  explicit Session(const SessionConfig& config);
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
  [[nodiscard]] networking::ConnectionState GetState() const;

  /// The connection's quality numbers, or nullopt if not connected.
  [[nodiscard]] std::optional<networking::ConnectionStats> GetStats() const;

  /// Runs one fixed tick of PredictionWorld for command and returns its state.
  prediction::State Tick(const input::Command& command, float delta_time);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace augusta::client_session

#endif  // AUGUSTA_CLIENT_SESSION_H_
