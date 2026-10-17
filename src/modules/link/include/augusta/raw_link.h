#ifndef AUGUSTA_RAW_LINK_H_
#define AUGUSTA_RAW_LINK_H_

#include <cstddef>
#include <expected>
#include <memory>
#include <vector>

#include "augusta/failure.h"
#include "augusta/link.h"

/// \file
/// For tests only: the Link's transport carrying bytes as they are, with no
/// conversion either way, so a test speaks the protocol by hand to send what a
/// real peer would not (values that fail the checks, messages out of turn,
/// bytes that are no message) and reads every payload the other side sent.
/// Every payload travels reliably, so whatever a test sends arrives. A real
/// client or server speaks through augusta/link.h's Client and Server, which
/// never carry a payload they did not encode themselves.
namespace augusta::link {

/// One payload, as the transport carried it.
using RawPayload = std::vector<std::byte>;

/// A client's raw end: Client's connection, its payloads as they are.
class RawClient {
 public:
  RawClient();
  /// Ends the connection, if any.
  ~RawClient();

  RawClient(const RawClient&) = delete;
  RawClient& operator=(const RawClient&) = delete;
  RawClient(RawClient&&) = delete;
  RawClient& operator=(RawClient&&) = delete;

  /// As Client's.
  void Connect(const Endpoint& server);
  void Disconnect();
  void PumpEvents();
  [[nodiscard]] ConnectionState GetState() const;

  /// Sends payload as it is: kDropped while not connected.
  [[nodiscard]] SendResult Send(const RawPayload& payload);

  /// Every payload the server sent since the last call, in the order it arrived.
  [[nodiscard]] std::expected<std::vector<RawPayload>, failure::Failure> Receive();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

/// One payload a peer sent a RawServer.
struct RawPeerMessage {
  PeerId from{};
  RawPayload payload;
};

/// A server's raw end: Server's listening socket and channels, their payloads as they are.
class RawServer {
 public:
  /// As Server's.
  explicit RawServer(const Endpoint& local);
  ~RawServer();

  RawServer(const RawServer&) = delete;
  RawServer& operator=(const RawServer&) = delete;
  RawServer(RawServer&&) = delete;
  RawServer& operator=(RawServer&&) = delete;

  /// As Server's.
  [[nodiscard]] Endpoint LocalEndpoint() const;
  [[nodiscard]] std::vector<PeerEvent> PumpEvents();
  void Accept(PeerId peer);
  void Disconnect(PeerId peer);

  /// Sends payload to peer as it is: kDropped if peer is not connected.
  [[nodiscard]] SendResult Send(PeerId peer, const RawPayload& payload);

  /// Every payload any peer sent since the last call, in the order it arrived.
  [[nodiscard]] std::expected<std::vector<RawPeerMessage>, failure::Failure> Receive();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace augusta::link

#endif  // AUGUSTA_RAW_LINK_H_
