#ifndef AUGUSTA_NETWORKING_H_
#define AUGUSTA_NETWORKING_H_

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#if AUGUSTA_DEBUG_TOOLS
#include <array>
#include <chrono>
#include <span>
#include <variant>
#endif

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
/// Unlike the window's events (GLFW/Win32 only deliver them on the thread
/// that owns the window), every method
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

/// Which of a process's packets SimulatedConditions' latency, jitter, loss and reorder reach.
enum class SimulatedTraffic : std::uint8_t {
  /// What the process sends only.
  kSent,
  /// What it sends, and again what it receives: each condition applies once
  /// each way, so a latency of 50 ms adds about 100 ms to a round trip with a
  /// peer elsewhere.
  kSentAndReceived,
};

/// Network conditions to impose on the real transport: for tests (e.g. NFR-02's
/// 100 ms), and for a developer impairing their own client's link.
struct SimulatedConditions {
  /// Extra delay on every packet the conditions reach (applies_to), in milliseconds.
  int latency_ms = 0;
  /// A random delay added on top of latency_ms to every packet the conditions
  /// reach, drawn from an exponential distribution of this mean and capped at
  /// jitter_max_ms, in milliseconds; 0 adds none. A packet is never let overtake
  /// one sent before it, so jitter alone clumps packets but keeps their order.
  int jitter_mean_ms = 0;
  int jitter_max_ms = 0;
  /// Share of packets the conditions reach that are dropped, 0..100.
  float loss_percent = 0.0F;
  /// Share of packets the conditions reach that are held back a further
  /// reorder_delay_ms, 0..100, so that those behind them overtake them. The
  /// transport has one reorder delay for both ways.
  float reorder_percent = 0.0F;
  int reorder_delay_ms = 0;
  /// How long a connection may hear nothing from its peer before it counts as
  /// lost (or, while connecting, as unreachable), in milliseconds; 0 keeps the
  /// transport's own default (about 10 seconds), which is too long for a test
  /// to wait. Unlike the other conditions it only reaches connections made
  /// after it is set, so set it before connecting. Whatever applies_to says, it
  /// is one timeout for the connection.
  int timeout_ms = 0;
  /// Which packets the conditions above reach: by default only those the process sends.
  SimulatedTraffic applies_to = SimulatedTraffic::kSent;
};

/// Applies conditions to every connection in the process from now on; a default-constructed one restores the real
/// network.
/// Process-wide, not per Client/Server, and applied at the packet level: the
/// latency is one-way, so two peers in the same process see a round trip of
/// about twice latency_ms (four times, with kSentAndReceived), and loss drops
/// whole packets (several small messages can share one), which reliable
/// messages survive by retransmission and unreliable ones do not. Needs Init()
/// to have run. A test calls it after the connection is established (the
/// handshake is subject to it too) and resets it before the next test; a
/// client with its one connection makes it that connection's.
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

#if AUGUSTA_DEBUG_TOOLS
inline constexpr std::size_t kIpv4AddressBytes = 4;
inline constexpr std::size_t kIpv6AddressBytes = 16;
/// An IPv4 address's bytes, in network order (192.168.1.10 is {192, 168, 1, 10}).
using Ipv4Address = std::array<std::uint8_t, kIpv4AddressBytes>;
/// An IPv6 address's bytes, in network order.
using Ipv6Address = std::array<std::uint8_t, kIpv6AddressBytes>;

/// One end of a tapped message: an address and a port, as values rather than
/// Endpoint's "host:port" text. A default one is unspecified: IPv4 0.0.0.0, port 0.
struct TapEndpoint {
  std::variant<Ipv4Address, Ipv6Address> address;
  /// In host order.
  std::uint16_t port = 0;

  bool operator==(const TapEndpoint&) const = default;
};

/// The unspecified address in endpoint's family, port 0: 0.0.0.0 for an IPv4
/// endpoint, :: for an IPv6 one. A Client's local end, which the transport
/// does not give, so a pcapng never pairs an IPv4 end with an IPv6 one.
TapEndpoint UnspecifiedEndpointLike(const TapEndpoint& endpoint);

/// Which way a tapped message went, from this process's side.
enum class TapDirection : std::uint8_t {
  /// Received from the remote end.
  kInbound,
  /// Sent to the remote end.
  kOutbound,
};

/// One message a Client or Server reports to its Tap.
struct TappedMessage {
  TapDirection direction;
  /// A Server's listen address and port; for a Client, since the transport
  /// does not give a connection's local address, UnspecifiedEndpointLike(remote).
  TapEndpoint local;
  TapEndpoint remote;
  /// The message's bytes, valid only during the Tap::Report call.
  std::span<const std::byte> payload;
  /// When the transport took the message to send, or handed it over received.
  std::chrono::system_clock::time_point time;
};

/// What a Client or Server reports every message it moves to, in a Development
/// build only (ADR-0074): each send the transport accepted (kAccepted, never a
/// dropped or failed one), and each message received, as it is taken off the
/// transport and before anyone decodes it. Report is called on whichever
/// thread sends or receives, the Network I/O thread or a tick's, often with
/// the transport's own lock held, so it must not block: an implementation
/// that writes somewhere slow only enqueues. What is done with the messages (such
/// as a pcapng file) is the tap's, not this module's.
class Tap {
 public:
  virtual ~Tap() = default;
  virtual void Report(const TappedMessage& message) = 0;

 protected:
  Tap() = default;
  Tap(const Tap&) = default;
  Tap& operator=(const Tap&) = default;
  Tap(Tap&&) = default;
  Tap& operator=(Tap&&) = default;
};
#endif

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
#if AUGUSTA_DEBUG_TOOLS
  /// As above, reporting every message to tap when it is not null; tap must
  /// outlive the Client.
  Client(failure::Faults* faults, Tap* tap);
#endif

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

/// The most bytes one message a Server receives may hold unless it is told
/// fewer: the transport's own default (GameNetworkingSockets'
/// k_ESteamNetworkingConfig_RecvMaxMessageSize), the largest message it sends.
inline constexpr std::size_t kDefaultMaxReceiveSize = std::size_t{512} * 1024;

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
  /// must outlive the Server. A peer that sends a message of more than
  /// max_receive_size bytes (held to kDefaultMaxReceiveSize at most) has its
  /// connection closed by the transport, reported as its kDisconnected, and
  /// the message never arrives.
  explicit Server(const Endpoint& local_endpoint, failure::Faults* faults = nullptr,
                  std::size_t max_receive_size = kDefaultMaxReceiveSize);
#if AUGUSTA_DEBUG_TOOLS
  /// As above, reporting every message to tap when it is not null; tap must
  /// outlive the Server.
  Server(const Endpoint& local_endpoint, failure::Faults* faults, Tap* tap,
         std::size_t max_receive_size = kDefaultMaxReceiveSize);
#endif

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
