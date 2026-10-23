#ifndef AUGUSTA_LINK_H_
#define AUGUSTA_LINK_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "augusta/failure.h"
#include "augusta/faults.h"
#include "augusta/link_types.h"

/// \file
/// The Link (GLOSSARY.md, ADR-0038): the channel through which one Session
/// exchanges messages with the other side, owning the transport connection
/// beneath end to end. Its two ends: the client's (Client), which dials the one
/// server it plays on, and the server's (Server), which listens and keeps one
/// channel per peer. Each sends and receives the engine's types
/// (augusta/link_types.h), turned into messages and back by the Link's own
/// conversions, and sends each with the reliability ADR-0038's table gives its
/// type, so no caller names one; nothing of the protocol or the transport shows
/// through it.
///
/// Like the transport, both are safe to call from any thread, and own none: the
/// caller polls them (PumpEvents, Receive), from the Network I/O thread
/// (ARCHITECTURE.md §8), and may send from another.
namespace augusta::link {

// ---- The transport, process-wide ----

/// The one-time, process-wide setup of the transport beneath every Link (ADR-0003):
/// call it once at process startup, before any Client or Server is constructed.
/// Fails (failure::Code::kTransportInitFailed) with the transport's own words
/// as detail, which its caller reports. faults, when given, is asked first
/// (failure::Site::kDependencyInit), so a test makes it fail.
[[nodiscard]] std::expected<void, failure::Failure> Init(failure::Faults* faults = nullptr);

/// Releases what Init set up: at most once, after every Client and Server is
/// gone. augustac and augustad never call it, as the process's exit reclaims it
/// all; a process that makes and ends Links before it exits (a test) does, or
/// the transport's still-referenced state reads as a leak under ASan.
void Shutdown();

/// Which of a process's packets SimulatedConditions' latency, jitter, loss and reorder reach.
enum class SimulatedTraffic : std::uint8_t {
  /// What the process sends only.
  kSent,
  /// What it sends, and again what it receives: each condition applies once
  /// each way, so a latency of 50 ms adds about 100 ms to a round trip with a
  /// peer elsewhere.
  kSentAndReceived,
};

/// Network conditions imposed on the real transport: for tests (e.g. NFR-02's
/// 100 ms), and for a developer impairing their own client's link.
struct SimulatedConditions {
  /// Extra delay on every packet the conditions reach (applies_to), in milliseconds.
  int latency_ms = 0;
  /// A random delay added on top of latency_ms to every packet the conditions
  /// reach, drawn from an exponential distribution of this mean and capped at
  /// jitter_max_ms, in milliseconds; 0 adds none. A packet never overtakes one
  /// sent before it, so jitter alone clumps packets but keeps their order.
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
  /// transport's own default (about 10 seconds), too long for a test to wait.
  /// Unlike the others it only reaches connections made after it is set, and
  /// whatever applies_to says it is one timeout for the connection.
  int timeout_ms = 0;
  /// Which packets the conditions above reach: by default only those the process sends.
  SimulatedTraffic applies_to = SimulatedTraffic::kSent;
};

/// Imposes conditions on every connection of the process from now on; a
/// default-constructed one restores the real network. Process-wide and at the
/// packet level: the latency is one-way, so two ends in one process see a round
/// trip of about twice latency_ms (four times, with kSentAndReceived), and loss
/// drops whole packets, which reliable messages survive by retransmission and
/// unreliable ones do not. Needs Init. A test sets it once the connection is up
/// (the handshake is subject to it too) and resets it before the next; a
/// client, with its one connection, makes it that connection's.
void SimulateNetworkConditions(const SimulatedConditions& conditions);

// ---- The client's end ----

/// A server's address in "host:port" form (e.g. "192.168.1.10:27015"): a
/// numeric IP, not a hostname, since players connect directly by IP and port.
struct Endpoint {
  std::string address;
};

/// How far the connection to the server has got.
enum class ConnectionState : std::uint8_t {
  /// Connect called; the handshake has not completed.
  kConnecting,
  /// Messages travel both ways.
  kConnected,
  /// Before Connect, and for good after the connection is refused, lost or
  /// ended by Disconnect, or a Connect the transport could not even start.
  kDisconnected,
};

/// The connection's numbers, as the transport measures them (ADR-0003): its
/// round trip, its loss and its queue, and what it carries. Nothing here is
/// computed by the Link itself.
struct ConnectionStats {
  /// The round trip to the server, in milliseconds.
  int ping_ms = 0;
  /// Packet delivery success rate, 0..1 (1 = no loss), measured locally and as
  /// the server reports back for the other direction; negative while not
  /// measured yet, as quality_remote commonly is right after connecting.
  float quality_local = 0.0F;
  float quality_remote = 0.0F;
  /// What the connection has carried lately, in bytes per second.
  float in_bytes_per_sec = 0.0F;
  float out_bytes_per_sec = 0.0F;
  /// The worst jitter since the last GetStats, in microseconds: a high-water
  /// mark each read clears. Negative while there is nothing to measure.
  std::int32_t max_jitter_us = -1;
  /// The bytes queued to send, and those sent reliably and not yet
  /// acknowledged: everything in flight or waiting to be.
  int pending_bytes = 0;
};

/// How many messages, and their bytes as the transport carried them.
struct MessageCount {
  std::uint64_t messages = 0;
  std::uint64_t bytes = 0;

  bool operator==(const MessageCount&) const = default;
};

/// One message type's traffic, each way.
struct TypeTraffic {
  MessageCount sent;
  MessageCount received;

  bool operator==(const TypeTraffic&) const = default;
};

/// How many of MessageType's values this build has.
inline constexpr std::size_t kMessageTypeCount = static_cast<std::size_t>(kLastMessageType) + 1;

/// What a Client's connection has carried since its last Connect, counted by
/// the Link itself, by message type: cumulative, so a reader takes rates by
/// difference. Sent counts only what the transport accepted; received, only
/// what Receive returns, so a payload that is no message, or one only a client
/// sends, counts nowhere.
struct Traffic {
  /// Indexed by MessageType: every type, those that never travelled at zero.
  std::array<TypeTraffic, kMessageTypeCount> by_type{};

  [[nodiscard]] const TypeTraffic& Of(MessageType type) const { return by_type.at(static_cast<std::size_t>(type)); }

  bool operator==(const Traffic&) const = default;
};

/// What the Link did with a message it did not fail on.
enum class SendOutcome : std::uint8_t {
  /// Taken: it travels as its type's reliability says. Only an accepted
  /// message counts as sent.
  kAccepted,
  /// Not taken, because the connection could not: it is not connected, is
  /// ending, or holds more than it can queue. The connection's outcome, not a
  /// failure.
  kDropped,
};

/// A send's outcome, or why nothing of it left: a message the protocol cannot
/// carry (failure::Code::kInvariantViolated, naming its type), or the local
/// transport's failure (kTransportSendFailed).
using SendResult = std::expected<SendOutcome, failure::Failure>;

/// The client's end of the Link to the one server it dials. The client process
/// holds one at a time.
class Client {
 public:
  /// faults, when given, is asked at every send and receive
  /// (failure::Site::kTransportSend, kTransportReceive); it must outlive the
  /// Client.
  explicit Client(failure::Faults* faults = nullptr);
  /// Ends the connection, if any.
  ~Client();

  /// Not copyable or movable: owns a live connection.
  Client(const Client&) = delete;
  Client& operator=(const Client&) = delete;
  Client(Client&&) = delete;
  Client& operator=(Client&&) = delete;

  /// Begins connecting to server; returns immediately. Throws
  /// failure::ClassifiedFailure (failure::Code::kInvalidConfiguration) if
  /// server.address does not parse; GetState reads kDisconnected on return if
  /// the transport could not start connecting. Call again only once GetState
  /// reads kDisconnected.
  void Connect(const Endpoint& server);

  /// Ends the connection, if any; GetState reads kDisconnected after.
  void Disconnect();

  /// Takes in the connection's progress: its handshake, refusal or loss.
  void PumpEvents();

  [[nodiscard]] ConnectionState GetState() const;

  /// The connection's numbers, or nullopt while not connected. Neither blocks
  /// nor does I/O, so it is safe to read every round.
  [[nodiscard]] std::optional<ConnectionStats> GetStats() const;

  /// What the connection has carried since the last Connect, by message type;
  /// all zero before the first. Safe from any thread, and neither blocks nor
  /// does I/O, so it is safe to read every round.
  [[nodiscard]] Traffic GetTraffic() const;

  /// Sends one message to the server: kDropped while not connected.
  [[nodiscard]] SendResult Send(const JoinRequest& request);
  [[nodiscard]] SendResult Send(const ReplayRequest& request);
  [[nodiscard]] SendResult Send(ReplayListRequest request);
  [[nodiscard]] SendResult Send(Ready ready);
#if AUGUSTA_DEBUG_TOOLS
  [[nodiscard]] SendResult Send(DebugSubscribe subscribe);
#endif
  /// A tick's commands, oldest first, as its one Commands message: the
  /// Carried ones alone.
  [[nodiscard]] SendResult Send(std::span<const SequencedCommand> commands);

  /// What the server has said since the last call, in the order it arrived:
  /// empty once drained, or while not connected. What arrived before the
  /// server closed the connection is still returned once, after PumpEvents has
  /// seen the close. A payload that is no message, or one only a client sends,
  /// is dropped here, with a warning. Fails
  /// (failure::Code::kTransportReceiveFailed) only when the local transport
  /// cannot receive on the connection it holds.
  [[nodiscard]] std::expected<std::vector<ServerMessage>, failure::Failure> Receive();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

// ---- Server ----

/// The Link's name for one peer connected to a server: what admission, the peer
/// gate and misbehaviour know it by. Valid only for the Server that produced
/// it, from its kConnectRequested event until its kDisconnected one. Distinct
/// from a Session ID, and not a credential.
enum class PeerId : std::uint32_t {};

/// One step of a peer's connection, as Server::PumpEvents reports it.
enum class PeerEventType : std::uint8_t {
  /// A client is connecting and waits for Accept or Disconnect: until one is
  /// called, nothing travels to or from it.
  kConnectRequested,
  /// An accepted peer completed its handshake: messages travel both ways.
  kConnected,
  /// The peer's connection ended, however it ended; its PeerId is no longer valid.
  kDisconnected,
};

/// Why a peer's connection ended.
enum class DisconnectReason : std::uint8_t {
  /// The peer closed it.
  kClosedByPeer,
  /// The transport gave up on it: nothing was heard from the peer for too
  /// long, or the network failed.
  kConnectionLost,
};

struct PeerEvent {
  PeerId peer{};
  PeerEventType type = PeerEventType::kConnectRequested;
  /// Only for kDisconnected.
  DisconnectReason reason = DisconnectReason::kClosedByPeer;
};

/// One peer's connection numbers.
struct PeerStats {
  PeerId peer{};
  ConnectionStats stats;
};

/// One payload a peer sent, as its server takes it in.
struct PeerMessage {
  PeerId from{};
  /// Its size, which a server counts, whatever it turns out to be.
  std::size_t bytes = 0;
  /// The type of message it is, or, if it decodes to none, the protocol's words
  /// for why.
  std::expected<MessageType, std::string> type;
  /// What it says, if it is a message a client sends; nullopt for a payload
  /// that is no message, or a message only a server sends.
  std::optional<ClientMessage> message;
};

/// A message for a client, encoded once, so a server sends the same one to as
/// many peers as need it: its type and its bytes are what a server counts.
class Outgoing {
 public:
  [[nodiscard]] MessageType Type() const { return type_; }
  [[nodiscard]] std::span<const std::byte> Payload() const { return payload_; }

 private:
  // The Link's own encoding (link.cpp) is the one way to make one, and its
  // Server sends the bytes it holds without copying them.
  friend struct OutgoingAccess;
  friend class Server;
  Outgoing(MessageType type, std::vector<std::byte> payload) : type_(type), payload_(std::move(payload)) {}

  MessageType type_;
  std::vector<std::byte> payload_;
};

/// A message a server sends, encoded: or, if a field of it is beyond what the
/// protocol carries, no message but failure::Code::kInvariantViolated naming
/// its type, which stops the runtime before anything of it leaves (ADR-0033).
/// A Join refused is its reason; a Replay list, the captures it names.
[[nodiscard]] std::expected<Outgoing, failure::Failure> Encode(const Admission& admission);
[[nodiscard]] std::expected<Outgoing, failure::Failure> Encode(JoinRefusal refusal);
[[nodiscard]] std::expected<Outgoing, failure::Failure> Encode(const Lobby& lobby);
[[nodiscard]] std::expected<Outgoing, failure::Failure> Encode(const MatchStart& start);
[[nodiscard]] std::expected<Outgoing, failure::Failure> Encode(const MatchEnd& end);
[[nodiscard]] std::expected<Outgoing, failure::Failure> Encode(const AuthoritativeState& state);
[[nodiscard]] std::expected<Outgoing, failure::Failure> Encode(const Shot& shot);
[[nodiscard]] std::expected<Outgoing, failure::Failure> Encode(const HitConfirmation& hit);
[[nodiscard]] std::expected<Outgoing, failure::Failure> Encode(const Death& death);
[[nodiscard]] std::expected<Outgoing, failure::Failure> Encode(const ReplayView& view);
[[nodiscard]] std::expected<Outgoing, failure::Failure> Encode(const std::vector<ReplayListing>& listings);
#if AUGUSTA_DEBUG_TOOLS
[[nodiscard]] std::expected<Outgoing, failure::Failure> Encode(const ShotJudgement& judgement);
#endif

/// state, an Authoritative State encoded for one recipient, encoded for
/// recipient instead: recipient's own fields (its acknowledged sequence, rifle,
/// health and queued commands) in place of state's, its tick and bodies state's
/// bytes, not encoded again. The bytes Encode makes of recipient with state's
/// tick and bodies, so a tick's bodies are encoded once however many players
/// are sent them. failure::Code::kInvariantViolated if state is no
/// Authoritative State, or a field of recipient's is beyond what the protocol
/// carries.
[[nodiscard]] std::expected<Outgoing, failure::Failure> Readdressed(const Outgoing& state,
                                                                    const AuthoritativeState& recipient);

/// What of listings, newest last, a Replay list carries: every one a Replay
/// request can name, the newest the list holds if there are more, in the same
/// order.
[[nodiscard]] std::vector<ReplayListing> ListedOf(const std::vector<ReplayListing>& listings);

/// A server's end of the Link: it listens, and keeps one channel per peer that
/// connects (US-02: 2-8 players).
class Server {
 public:
  /// Starts listening on local, on a free port of its own choosing if that
  /// names port 0 (LocalEndpoint). Throws failure::ClassifiedFailure:
  /// failure::Code::kInvalidConfiguration if local does not parse,
  /// kListenerSetupFailed if the transport cannot listen there. faults, when
  /// given, is asked at listener setup and at every send and receive
  /// (failure::Site::kListenerSetup, kTransportSend, kTransportReceive); it
  /// must outlive the Server.
  explicit Server(const Endpoint& local, failure::Faults* faults = nullptr);
  /// Ends every peer's connection and stops listening.
  ~Server();

  /// Not copyable or movable: owns a listening socket and live connections.
  Server(const Server&) = delete;
  Server& operator=(const Server&) = delete;
  Server(Server&&) = delete;
  Server& operator=(Server&&) = delete;

  /// The address it listens on, with the port it chose if local named port 0.
  [[nodiscard]] Endpoint LocalEndpoint() const;

  /// What happened to its peers' connections since the last call, in the
  /// order it happened. Each kConnectRequested must be answered, with Accept
  /// or Disconnect, before the next call, or the peer is reported again.
  [[nodiscard]] std::vector<PeerEvent> PumpEvents();

  /// Lets a peer that asked to connect complete its handshake.
  void Accept(PeerId peer);

  /// Ends peer's connection, refusing it if it is still asking, once what was
  /// sent to it reliably has been delivered. Nothing if peer is gone already.
  /// No kDisconnected event follows for a connection this side ends.
  void Disconnect(PeerId peer);

  /// Sends message to peer with its type's reliability: kDropped if peer is
  /// not connected, or its connection could not take it. Fails only when the
  /// local transport refuses the send (failure::Code::kTransportSendFailed).
  [[nodiscard]] SendResult Send(PeerId peer, const Outgoing& message);

  /// Every payload any peer sent since the last call, in the order it arrived,
  /// each as what it is (PeerMessage): empty once drained. Fails
  /// (failure::Code::kTransportReceiveFailed) only when the local transport
  /// cannot receive.
  [[nodiscard]] std::expected<std::vector<PeerMessage>, failure::Failure> Receive();

  /// Every connected peer's numbers. Each read clears their max_jitter_us, so
  /// one caller samples. Neither blocks nor does I/O.
  [[nodiscard]] std::vector<PeerStats> GetStats() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace augusta::link

#endif  // AUGUSTA_LINK_H_
