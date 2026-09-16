#ifndef AUGUSTA_NETWORKING_H_
#define AUGUSTA_NETWORKING_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// augusta::networking wraps GameNetworkingSockets/UDP (ADR-0003) for
// direct client-server connections - no matchmaking, relay, or P2P (out
// of scope per ARCHITECTURE.md §3: players connect directly via IP:port).
//
// Client and Server are two different classes, not one interface used
// identically by both sides the way physics::World is: a client dials
// out to exactly one server, while a server listens for and tracks up to
// a handful of independent peers (US-02, 2-8 players) - genuinely
// different shapes, not just convenience.
//
// Unlike Renderer/Input's EventSink push (forced by GLFW/Win32 only
// delivering messages on the thread that owns the window), every method
// here is safe to call from any thread - GameNetworkingSockets is
// internally thread-safe and queues both connection events and messages
// itself. PumpEvents/ReceiveMessages are still poll calls, matching this
// codebase's established idiom, and are expected to run on the Network
// I/O thread (ARCHITECTURE.md §8, ADR-0005), but nothing here requires
// that thread specifically the way window/device events required the
// Main/Render thread.
//
// Interface scope, for now: raw framed payloads only. What the bytes
// mean - message types, fields, how input::Command or a future
// authoritative-state snapshot get encoded - is Networking Protocol's
// concern (ADR-0007, custom binary format) and isn't designed yet; nor
// is per-message reliability (GameNetworkingSockets supports both
// reliable and unreliable sends, but which v1 message types need which
// isn't decided until the message catalogue exists). Send here is
// unreliable, matching real-time state updates where a newer message
// supersedes an older one; revisit once Networking Protocol exists.
namespace augusta::networking {

// One-time process-wide setup for the underlying transport library. Call
// exactly once at process startup, before constructing any Client or
// Server.
void Init();

// Releases the transport library's process-wide state. Call at most
// once, after every Client/Server has been destroyed. augustac/augustad
// never call this - the OS reclaims everything at process exit either
// way - but a process that constructs and tears down Client/Server
// instances before exiting (e.g. a test) needs it, or GameNetworkingSockets'
// still-referenced OpenSSL state reads as a leak under ASan.
void Shutdown();

// A server address in "host:port" form (e.g. "192.168.1.10:27015"). A
// numeric IP, not a hostname - no DNS resolution in v1, matching the
// direct-IP-only scope above.
struct Endpoint {
  std::string address;
};

// One received message's raw bytes, copied out of the transport's own
// buffer so callers don't have to reason about its lifetime.
using Payload = std::vector<std::byte>;

// ---- Client ----

// A client's connection to the one server it dials. Mirrors the
// underlying transport's async handshake: Connect returns immediately,
// and GetState reports progress.
enum class ConnectionState {
  kConnecting,    // Connect() called; handshake not yet complete.
  kConnected,     // Ready to Send/ReceiveMessages.
  kDisconnected,  // Initial state, and terminal after any of: rejected,
                  // dropped, or a local Disconnect() call.
};

// A snapshot of Client's connection quality/throughput, sourced directly
// from GameNetworkingSockets' own per-connection instrumentation
// (ADR-0003) - see Client::GetStats. None of this is computed by this
// module itself.
struct ConnectionStats {
  // Current round-trip time to the server, in milliseconds.
  int ping_ms = 0;
  // Packet delivery success rate, 0..1 (1 = no loss): measured locally,
  // and as reported back by the server for the reverse direction.
  // quality_remote in particular is commonly negative right after
  // connecting - same "not measured yet" convention as max_jitter_us
  // below - until the server has echoed back enough acks to compute it.
  float quality_local = 0.0F;
  float quality_remote = 0.0F;
  // Actual throughput over the underlying transport's recent history, in
  // bytes per second - not the same as m_nSendRateBytesPerSecond's
  // estimated channel *capacity*, which can run well ahead of this.
  float in_bytes_per_sec = 0.0F;
  float out_bytes_per_sec = 0.0F;
  // Worst jitter observed since the last GetStats() call, in
  // microseconds - a high-water mark, cleared each time it's read.
  // Negative means no data available yet (not every connection can
  // measure jitter); kept as GameNetworkingSockets' own sentinel rather
  // than mapped to something else.
  std::int32_t max_jitter_us = -1;
  // Bytes queued to send (reliable + unreliable) plus reliable bytes
  // already placed on the wire but not yet acknowledged - i.e.
  // everything currently in flight or waiting to be.
  int pending_bytes = 0;
};

// The client side of one connection to one dedicated server
// (ARCHITECTURE.md §7's client-only Networking: "sends commands,
// receives authoritative server state"). The client process constructs
// exactly one.
class Client {
 public:
  Client();

  // Closes the connection, if any, and releases the underlying
  // transport connection.
  ~Client();

  // Not copyable or movable - owns a live transport connection the same
  // way Renderer owns a live GPU device (see renderer.h).
  Client(const Client&) = delete;
  Client& operator=(const Client&) = delete;
  Client(Client&&) = delete;
  Client& operator=(Client&&) = delete;

  // Begins connecting to server; returns immediately. Calling again
  // before GetState() reports kDisconnected is undefined behavior.
  void Connect(const Endpoint& server);

  // Ends the connection, if any. GetState() reports kDisconnected
  // afterward. Safe to call even if never connected.
  void Disconnect();

  // Drains connection-lifecycle events (handshake progress, rejection,
  // drop) and updates the state GetState() returns.
  void PumpEvents();

  [[nodiscard]] ConnectionState GetState() const;

  // Returns a snapshot of this connection's real-time quality/throughput
  // (ConnectionStats), or std::nullopt if not currently kConnected. Safe
  // to call every frame - the underlying transport maintains these from
  // its own rolling window; this doesn't block or perform I/O.
  [[nodiscard]] std::optional<ConnectionStats> GetStats() const;

  // Sends payload to the server. A no-op if GetState() isn't
  // kConnected - mirrors UDP's own best-effort semantics; there is no
  // synchronous failure to report.
  void Send(const Payload& payload);

  // Returns every message received since the last call, in arrival
  // order. Empty once drained.
  [[nodiscard]] std::vector<Payload> ReceiveMessages();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

// ---- Server ----

// Opaque handle identifying one connected client, from the server's
// point of view. Valid only for the Server instance that produced it,
// from a kConnectRequested PeerEvent onward, until its matching
// kDisconnected event.
enum class PeerId : std::uint32_t {};

// One lifecycle transition for a peer, returned by Server::PumpEvents.
enum class PeerEventType {
  // A client is attempting to connect and is awaiting Accept/Disconnect
  // - until one is called, this peer can neither send nor receive.
  kConnectRequested,
  // A previously-accepted peer completed its handshake and can now
  // Send/ReceiveMessages.
  kConnected,
  // A peer's connection ended, cleanly or otherwise; its PeerId is
  // invalidated.
  kDisconnected,
};

struct PeerEvent {
  PeerId peer;
  PeerEventType type;
};

// One received message plus which peer sent it.
struct PeerMessage {
  PeerId from;
  Payload payload;
};

// The server side, listening for and tracking up to a handful of client
// connections (US-02: 2-8 players) (ARCHITECTURE.md §7's server-only
// Networking: "receives client commands, sends authoritative state").
// The server process constructs exactly one.
class Server {
 public:
  // Starts listening on local_endpoint. Throws std::runtime_error if the
  // address can't be bound.
  explicit Server(const Endpoint& local_endpoint);

  // Closes the listen socket and every connected peer's connection.
  ~Server();

  // Not copyable or movable - owns a live listen socket and every
  // connected peer's connection the same way Renderer owns a live GPU
  // device (see renderer.h).
  Server(const Server&) = delete;
  Server& operator=(const Server&) = delete;
  Server(Server&&) = delete;
  Server& operator=(Server&&) = delete;

  // Drains connection-lifecycle events since the last call (see
  // PeerEventType) and returns them in arrival order. Every
  // kConnectRequested event must be answered with Accept or Disconnect
  // before the next PumpEvents call - left unanswered, the same peer is
  // re-delivered as kConnectRequested again rather than silently
  // timing out.
  [[nodiscard]] std::vector<PeerEvent> PumpEvents();

  // Admits a pending peer (see PeerEventType::kConnectRequested),
  // letting it proceed toward kConnected. Calling with any peer not
  // currently pending is undefined behavior.
  void Accept(PeerId peer);

  // Ends peer's connection: rejects it if still pending, or forcibly
  // drops it if already connected. A no-op if peer is unknown (already
  // disconnected).
  void Disconnect(PeerId peer);

  // Sends payload to one connected peer. A no-op if peer isn't
  // currently connected (see Client::Send).
  void Send(PeerId peer, const Payload& payload);

  // Sends payload to every currently connected peer.
  void Broadcast(const Payload& payload);

  // Returns every message received from any peer since the last call,
  // in arrival order. Empty once drained.
  [[nodiscard]] std::vector<PeerMessage> ReceiveMessages();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace augusta::networking

#endif  // AUGUSTA_NETWORKING_H_
