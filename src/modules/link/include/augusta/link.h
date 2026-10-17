#ifndef AUGUSTA_LINK_H_
#define AUGUSTA_LINK_H_

#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "augusta/failure.h"
#include "augusta/faults.h"
#include "augusta/link_types.h"

/// \file
/// The Link (GLOSSARY.md, ADR-0038): the channel through which one Session
/// exchanges messages with the other side, owning the transport connection
/// beneath end to end. This is its client end, which dials the one server it
/// plays on. It sends and receives the engine's types (augusta/link_types.h),
/// turned into messages and back by the Link's own conversions, and sends each
/// with the reliability ADR-0038's table gives its type, so no caller names
/// one; nothing of the protocol or the transport shows through it.
///
/// Like the transport, it is safe to call from any thread, and it owns none:
/// the caller polls it (PumpEvents, Receive), from the Network I/O thread
/// (ARCHITECTURE.md §8), and may send from another.
namespace augusta::link {

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

  /// Sends one message to the server: kDropped while not connected.
  [[nodiscard]] SendResult Send(const JoinRequest& request);
  [[nodiscard]] SendResult Send(const ReplayRequest& request);
  [[nodiscard]] SendResult Send(ReplayListRequest request);
  [[nodiscard]] SendResult Send(Ready ready);
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

}  // namespace augusta::link

#endif  // AUGUSTA_LINK_H_
