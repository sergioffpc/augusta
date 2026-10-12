#ifndef AUGUSTA_NETWORKING_TRANSPORT_EVENTS_H_
#define AUGUSTA_NETWORKING_TRANSPORT_EVENTS_H_

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>

#include "augusta/networking.h"

/// \file
/// The transport's connection events as data (ADR-0005): GameNetworkingSockets'
/// status-changed callback publishes one minimal TransportEvent and does
/// nothing else - no state change, no transport call, no log line. The Network
/// I/O owner (Server::PumpEvents, Client::PumpEvents) drains them after
/// RunCallbacks and applies each: the decision of what it does (PeerTable,
/// ApplyToClient) under the owner's lock, and the transport call it asks for
/// after the lock is released. Private to the module, and free of
/// GameNetworkingSockets types, so the decision is tested apart from a live
/// connection.
namespace augusta::networking {

/// A connection's new state as a status-changed callback reports it: only the
/// states this module acts on, every other one as kOther.
enum class TransportState : std::uint8_t {
  kConnecting,
  kConnected,
  /// The peer closed the connection.
  kClosedByPeer,
  /// The transport gave up on it: nothing heard for too long, or the network failed.
  kProblemDetectedLocally,
  kOther,
};

/// One status change, as the callback publishes it.
struct TransportEvent {
  /// The transport's handle for the connection.
  std::uint32_t connection = 0;
  TransportState state = TransportState::kOther;
  /// Where the connection's other end is, as "ip:port", for the owner's log line.
  std::string remote_address;
};

/// The transport call the owner makes for one event, outside every lock.
enum class TransportCall : std::uint8_t {
  kNone,
  /// Add the connection to the server's poll group, so its messages are received.
  kJoinPollGroup,
  /// Release the connection: it has ended.
  kClose,
};

/// Events the callback has published and the owner has not yet drained. Safe
/// from any thread: the callback runs inside RunCallbacks, under the module's
/// handler registry lock, and takes only this queue's own.
class TransportEventQueue {
 public:
  void Publish(TransportEvent event);

  /// Every event published since the last call, oldest first.
  [[nodiscard]] std::vector<TransportEvent> Drain();

 private:
  std::mutex mutex_;
  std::vector<TransportEvent> events_;
};

/// The server's peers as its transport events leave them. Not thread-safe: the
/// server guards it with its own lock.
class PeerTable {
 public:
  /// Updates the peers for event, queues the PeerEvent it means, and returns
  /// the transport call it needs.
  [[nodiscard]] TransportCall Apply(const TransportEvent& event);

  /// The one-shot events queued since the last call, oldest first, then a
  /// kConnectRequested for every peer still waiting for Answer.
  [[nodiscard]] std::vector<PeerEvent> TakeEvents();

  /// connection has been accepted or refused: it is no longer requested.
  void Answer(std::uint32_t connection);

  /// connection was closed by this side: it is neither pending nor connected.
  void Forget(std::uint32_t connection);

  /// connection is ended by this side because the transport could not keep it
  /// (a reliable message its full queue could not take): forgotten, as Forget
  /// does, and reported as a kDisconnected (kConnectionLost) event, since its
  /// owner hears of no departure otherwise. Returns the transport call it
  /// needs: kClose, or kNone if it was not open.
  [[nodiscard]] TransportCall Lose(std::uint32_t connection);

  [[nodiscard]] bool IsConnected(std::uint32_t connection) const;

  /// Every connected peer's connection.
  [[nodiscard]] std::vector<std::uint32_t> Connected() const;

  /// Every connection still open, pending or connected: what closing the server closes.
  [[nodiscard]] std::vector<std::uint32_t> Connections() const;

 private:
  std::unordered_set<std::uint32_t> pending_;
  std::unordered_set<std::uint32_t> connected_;
  std::vector<PeerEvent> queued_;
};

/// What one event does to the client's connection.
struct ClientTransition {
  /// The connection's state after it.
  ConnectionState state = ConnectionState::kDisconnected;
  /// The transport call it needs.
  TransportCall call = TransportCall::kNone;
  /// Whether it ended the current connection, which the client then forgets.
  bool ended = false;
};

/// What event does to a client whose current connection is current and in
/// state. An event of an earlier connection - one this client has since
/// replaced - leaves the state alone; if it ended, that connection is only closed.
[[nodiscard]] ClientTransition ApplyToClient(std::uint32_t current, ConnectionState state, const TransportEvent& event);

}  // namespace augusta::networking

#endif  // AUGUSTA_NETWORKING_TRANSPORT_EVENTS_H_
