#ifndef AUGUSTA_NETWORKING_H_
#define AUGUSTA_NETWORKING_H_

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "augusta/failure.h"
#include "augusta/faults.h"

/// \file
/// augusta::networking wraps GameNetworkingSockets/UDP (ADR-0003) for
/// direct client-server connections - no matchmaking, relay, or P2P (out
/// of scope per ARCHITECTURE.md §3: players connect directly via IP:port).
///
/// Client and Server are two different classes, not one interface used
/// identically by both sides the way physics::World is: a client dials
/// out to exactly one server, while a server listens for and tracks up to
/// a handful of independent peers (US-02, 2-8 players) - genuinely
/// different shapes, not just convenience.
///
/// Unlike Renderer/Input's EventSink push (forced by GLFW/Win32 only
/// delivering messages on the thread that owns the window), every method
/// here is safe to call from any thread - GameNetworkingSockets is
/// internally thread-safe and queues both connection events and messages
/// itself. PumpEvents/ReceiveMessages are still poll calls, matching this
/// codebase's established idiom, and are expected to run on the Network
/// I/O thread (ARCHITECTURE.md §8, ADR-0005), but nothing here requires
/// that thread specifically the way window/device events required the
/// Main/Render thread.
///
/// Interface scope: raw framed payloads only. What the bytes mean -
/// message types, fields, how a Command or an Authoritative State update
/// is encoded - is the Networking Protocol's concern (augusta::protocol,
/// ADR-0007, ADR-0038). Which messages are reliable is that catalogue's
/// call too (ADR-0038's reliability split), so every send names
/// its Reliability explicitly rather than this module picking a default:
/// unreliable suits real-time state updates where a newer message
/// supersedes an older one, reliable suits a handshake that must arrive.
///
/// Every send and receive says what the local transport did with it
/// (ADR-0033): a message it accepted, one it dropped because its peer's
/// connection could not take it - a peer outcome, which never stops the
/// runtime - or a failure of the local transport itself, a failure::Failure
/// whose runtime Disposition its caller escalates. Reliable delivery is the
/// transport's: nothing here, or above it, retries a send. A Client or Server
/// given a failure::Faults asks it at each send, receive and listener setup,
/// so a test makes the transport fail there.
namespace augusta::networking {

/// One-time process-wide setup for the underlying transport library. Call
/// exactly once at process startup, before constructing any Client or
/// Server. Fails (failure::Code::kTransportInitFailed) with the library's own
/// words as detail, which its caller reports. faults, when given, is asked
/// first (failure::Site::kDependencyInit), so a test makes it fail.
[[nodiscard]] std::expected<void, failure::Failure> Init(failure::Faults* faults = nullptr);

/// Releases the transport library's process-wide state. Call at most
/// once, after every Client/Server has been destroyed. augustac/augustad
/// never call this - the OS reclaims everything at process exit either
/// way - but a process that constructs and tears down Client/Server
/// instances before exiting (e.g. a test) needs it, or GameNetworkingSockets'
/// still-referenced OpenSSL state reads as a leak under ASan.
void Shutdown();

/// How one message is delivered.
enum class Reliability {
  /// Delivered exactly once and in order among reliable messages, retransmitted as needed.
  kReliable,
  /// Sent once, best effort: may be lost, is never delivered twice, may arrive out of order.
  kUnreliable,
};

/// Network conditions to impose on the real transport, for tests (e.g. NFR-02's 100 ms).
struct SimulatedConditions {
  /// Extra delay on every packet this process sends, in milliseconds.
  int latency_ms = 0;
  /// A random delay added on top of latency_ms to every packet this process
  /// sends, drawn from an exponential distribution of this mean and capped at
  /// jitter_max_ms, in milliseconds; 0 adds none. A packet is never let overtake
  /// one sent before it, so jitter alone clumps packets but keeps their order.
  int jitter_mean_ms = 0;
  int jitter_max_ms = 0;
  /// Share of packets this process sends that are dropped, 0..100.
  float loss_percent = 0.0F;
  /// Share of packets this process sends that are held back a further
  /// reorder_delay_ms, 0..100, so that those sent meanwhile overtake them.
  float reorder_percent = 0.0F;
  int reorder_delay_ms = 0;
  /// How long a connection may hear nothing from its peer before it counts as
  /// lost (or, while connecting, as unreachable), in milliseconds; 0 keeps the
  /// transport's own default (about 10 seconds), which is too long for a test
  /// to wait. Unlike the other conditions it only reaches connections made
  /// after it is set, so set it before connecting.
  int timeout_ms = 0;
};

/// Applies conditions to every connection in the process from now on; a default-constructed one restores the real
/// network.
/// Process-wide, not per Client/Server, and applied at the packet level: the
/// latency is one-way, so two peers in the same process see a round trip of
/// about twice latency_ms, and loss drops whole packets (several small
/// messages can share one), which reliable messages survive by retransmission
/// and unreliable ones do not. Needs Init() to have run. Meant for tests: call
/// it after the connection is established (the handshake is subject to it
/// too) and reset it before the next test.
void SimulateNetworkConditions(const SimulatedConditions& conditions);

/// What the local transport did with a message it did not fail on.
enum class SendOutcome : std::uint8_t {
  /// Taken: the transport delivers it as its Reliability says. Only an
  /// accepted message counts as sent.
  kAccepted,
  /// Not taken, because its peer's connection could not: it is not connected,
  /// is ending, or has more queued than the transport holds. A peer outcome,
  /// not a local failure. A reliable message dropped for a full queue ends that
  /// connection, since it could no longer be delivered as reliable promises:
  /// a server's peer is then reported kDisconnected (kConnectionLost) by the
  /// next PumpEvents, and a client's connection reads kDisconnected.
  kDropped,
};

/// A send's outcome, or the local transport's failure
/// (failure::Code::kTransportSendFailed).
using SendResult = std::expected<SendOutcome, failure::Failure>;

/// A server address in "host:port" form (e.g. "192.168.1.10:27015"). A
/// numeric IP, not a hostname - no DNS resolution, matching the
/// direct-IP-only scope above.
struct Endpoint {
  std::string address;
};

/// One received message's raw bytes, copied out of the transport's own
/// buffer so callers don't have to reason about its lifetime.
using Payload = std::vector<std::byte>;

// ---- Client ----

/// A client's connection to the one server it dials. Mirrors the
/// underlying transport's async handshake: Connect returns immediately,
/// and GetState reports progress.
enum class ConnectionState {
  /// Connect() called; handshake not yet complete.
  kConnecting,
  /// Ready to Send/ReceiveMessages.
  kConnected,
  /// Initial state, and terminal after any of: rejected, dropped, a local
  /// Disconnect() call, or a Connect() the transport could not even start.
  kDisconnected,
};

/// A snapshot of one connection's quality/throughput, sourced directly
/// from GameNetworkingSockets' own per-connection instrumentation
/// (ADR-0003) - see Client::GetStats and Server::GetStats. None of this is
/// computed by this module itself.
struct ConnectionStats {
  /// Current round-trip time to the far end, in milliseconds.
  int ping_ms = 0;
  /// Packet delivery success rate, 0..1 (1 = no loss): measured locally,
  /// and as reported back by the far end for the reverse direction.
  /// quality_remote in particular is commonly negative right after
  /// connecting - same "not measured yet" convention as max_jitter_us
  /// below - until the far end has echoed back enough acks to compute it.
  float quality_local = 0.0F;
  float quality_remote = 0.0F;
  /// Actual throughput over the underlying transport's recent history, in
  /// bytes per second - not the same as m_nSendRateBytesPerSecond's
  /// estimated channel *capacity*, which can run well ahead of this.
  float in_bytes_per_sec = 0.0F;
  float out_bytes_per_sec = 0.0F;
  /// Worst jitter observed since the last GetStats() call, in
  /// microseconds - a high-water mark, cleared each time it's read.
  /// Negative means no data available yet (not every connection can
  /// measure jitter); kept as GameNetworkingSockets' own sentinel rather
  /// than mapped to something else.
  std::int32_t max_jitter_us = -1;
  /// Bytes queued to send (reliable + unreliable) plus reliable bytes
  /// already placed on the wire but not yet acknowledged - i.e.
  /// everything currently in flight or waiting to be.
  int pending_bytes = 0;
};

/// The client side of one connection to one dedicated server
/// (ARCHITECTURE.md §7's client-only Networking: "sends commands,
/// receives authoritative server state"). The client process constructs
/// exactly one.
class Client {
 public:
  /// faults, when given, is asked at every send and receive
  /// (failure::Site::kTransportSend, kTransportReceive); it must outlive the
  /// Client.
  explicit Client(failure::Faults* faults = nullptr);

  /// Closes the connection, if any, and releases the underlying
  /// transport connection.
  ~Client();

  /// Not copyable or movable - owns a live transport connection the same
  /// way Renderer owns a live GPU device (see renderer.h).
  Client(const Client&) = delete;
  Client& operator=(const Client&) = delete;
  Client(Client&&) = delete;
  Client& operator=(Client&&) = delete;

  /// Begins connecting to server; returns immediately. Throws
  /// failure::ClassifiedFailure (failure::Code::kInvalidConfiguration) if
  /// server.address does not parse; if the transport cannot create the
  /// connection, GetState() reports kDisconnected on return instead of
  /// kConnecting. Calling again before GetState() reports kDisconnected
  /// is undefined behavior.
  void Connect(const Endpoint& server);

  /// Ends the connection, if any. GetState() reports kDisconnected
  /// afterward. Safe to call even if never connected.
  void Disconnect();

  /// Drains connection-lifecycle events (handshake progress, rejection,
  /// drop) and updates the state GetState() returns.
  void PumpEvents();

  [[nodiscard]] ConnectionState GetState() const;

  /// Returns a snapshot of this connection's real-time quality/throughput
  /// (ConnectionStats), or std::nullopt if not currently kConnected. Safe
  /// to call every frame - the underlying transport maintains these from
  /// its own rolling window; this doesn't block or perform I/O.
  [[nodiscard]] std::optional<ConnectionStats> GetStats() const;

  /// Sends payload to the server as reliability says: kDropped if GetState()
  /// isn't kConnected.
  [[nodiscard]] SendResult Send(const Payload& payload, Reliability reliability);

  /// Returns every message received since the last call, in arrival
  /// order: empty once drained, or while not connected. What arrived before
  /// the server closed the connection is still returned once, after
  /// PumpEvents has seen the close; Connect and Disconnect drop it. Fails
  /// (failure::Code::kTransportReceiveFailed) only when the local transport
  /// cannot receive on a connection it holds, never for an empty queue.
  [[nodiscard]] std::expected<std::vector<Payload>, failure::Failure> ReceiveMessages();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

// ---- Server ----

/// Opaque handle identifying one connected client, from the server's
/// point of view. Valid only for the Server instance that produced it,
/// from a kConnectRequested PeerEvent onward, until its matching
/// kDisconnected event.
enum class PeerId : std::uint32_t {};

/// One lifecycle transition for a peer, returned by Server::PumpEvents.
enum class PeerEventType {
  /// A client is attempting to connect and is awaiting Accept/Disconnect
  /// - until one is called, this peer can neither send nor receive.
  kConnectRequested,
  /// A previously-accepted peer completed its handshake and can now
  /// Send/ReceiveMessages.
  kConnected,
  /// A peer's connection ended, cleanly or otherwise; its PeerId is
  /// invalidated.
  kDisconnected,
};

/// Why a peer's connection ended.
enum class DisconnectReason {
  /// The peer closed the connection itself.
  kClosedByPeer,
  /// The transport gave up on the connection: nothing was heard from the peer
  /// for too long, or the network failed.
  kConnectionLost,
};

/// One of the events Server::PumpEvents returns.
struct PeerEvent {
  PeerId peer;
  PeerEventType type;
  /// Why the connection ended; only meaningful for kDisconnected.
  DisconnectReason reason = DisconnectReason::kClosedByPeer;
};

struct PeerStats {
  PeerId peer;
  ConnectionStats stats;
};

/// One received message plus which peer sent it.
struct PeerMessage {
  PeerId from;
  Payload payload;
};

/// The server side, listening for and tracking up to a handful of client
/// connections (US-02: 2-8 players) (ARCHITECTURE.md §7's server-only
/// Networking: "receives client commands, sends authoritative state").
/// The server process constructs exactly one.
class Server {
 public:
  /// Starts listening on local_endpoint - on a free port of its own choosing
  /// if that names port 0 (see LocalEndpoint). Throws
  /// failure::ClassifiedFailure, which the application boundary keeps whole:
  /// failure::Code::kInvalidConfiguration if local_endpoint does not parse,
  /// kListenerSetupFailed if the transport cannot set up its listen socket or
  /// poll group (e.g. the address can't be bound). faults, when given, is
  /// asked at listener setup and at every send and receive
  /// (failure::Site::kListenerSetup, kTransportSend, kTransportReceive); it
  /// must outlive the Server.
  explicit Server(const Endpoint& local_endpoint, failure::Faults* faults = nullptr);

  /// Closes the listen socket and every connected peer's connection.
  ~Server();

  /// Not copyable or movable - owns a live listen socket and every
  /// connected peer's connection the same way Renderer owns a live GPU
  /// device (see renderer.h).
  Server(const Server&) = delete;
  Server& operator=(const Server&) = delete;
  Server(Server&&) = delete;
  Server& operator=(Server&&) = delete;

  /// The address it listens on: local_endpoint's, with the port it chose if that named port 0.
  [[nodiscard]] Endpoint LocalEndpoint() const;

  /// Drains connection-lifecycle events since the last call (see
  /// PeerEventType) and returns them in arrival order. Every
  /// kConnectRequested event must be answered with Accept or Disconnect
  /// before the next PumpEvents call - left unanswered, the same peer is
  /// re-delivered as kConnectRequested again rather than silently
  /// timing out.
  [[nodiscard]] std::vector<PeerEvent> PumpEvents();

  /// Admits a pending peer (see PeerEventType::kConnectRequested),
  /// letting it proceed toward kConnected. Calling with any peer not
  /// currently pending is undefined behavior.
  void Accept(PeerId peer);

  /// Ends peer's connection: rejects it if still pending, or drops it if
  /// already connected, once what was sent to it reliably has been
  /// delivered. A no-op if peer is unknown (already disconnected).
  void Disconnect(PeerId peer);

  /// Sends payload to one connected peer as reliability says: kDropped if
  /// peer isn't connected.
  [[nodiscard]] SendResult Send(PeerId peer, const Payload& payload, Reliability reliability);

  /// Sends payload to every currently connected peer as reliability says,
  /// as Send does to each. Fails on the first local transport failure, not
  /// sending to the peers after it; a peer that drops it is that peer's outcome.
  [[nodiscard]] std::expected<void, failure::Failure> Broadcast(const Payload& payload, Reliability reliability);

  /// Returns every message received from any peer since the last call,
  /// in arrival order: empty once drained. Fails
  /// (failure::Code::kTransportReceiveFailed) only when the local transport
  /// cannot receive on its poll group, never for an empty queue.
  [[nodiscard]] std::expected<std::vector<PeerMessage>, failure::Failure> ReceiveMessages();

  /// Returns a snapshot of every connected peer's connection (ConnectionStats),
  /// as Client::GetStats does for the client's one: each read clears its
  /// max_jitter_us. Doesn't block or perform I/O.
  [[nodiscard]] std::vector<PeerStats> GetStats() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace augusta::networking

#endif  // AUGUSTA_NETWORKING_H_
