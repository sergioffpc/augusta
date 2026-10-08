#ifndef AUGUSTA_SERVER_TICK_MESSAGES_H_
#define AUGUSTA_SERVER_TICK_MESSAGES_H_

#include <expected>
#include <functional>
#include <unordered_map>
#include <vector>

#include "augusta/failure.h"
#include "augusta/networking.h"
#include "augusta/replication.h"
#include "augusta/simulation.h"
#include "augusta/tick.h"
#include "host_metrics.h"
#include "match.h"

/// \file
/// What Host sends its players after each tick (ADR-0044): each one's update,
/// unreliably, since a newer one supersedes it; then, reliably, what must
/// arrive: every Shot and every Death of the tick to all of them, and each Hit
/// confirmation to its shooter alone, if it is still in the match. replication
/// decides what each message holds; this encodes them, in that order and with
/// that reliability (ForEachTickMessage, tested without a network), and sends
/// them on the Host's connections, counting each into the Host's metrics
/// (host_metrics.h).
namespace augusta::server {

/// Who a tick's messages go to: each player in the match.
struct TickRecipients {
  std::vector<replication::Recipient> recipients;
  /// The connection of each recipient, by the entity its player controls.
  std::unordered_map<EntityId, networking::PeerId> peers;
};

/// Sends payload, an encoded message, to peer as reliability says, counting it
/// into metrics only if the transport accepted it: how the Host sends
/// everything it sends. Returns what the transport did with it.
[[nodiscard]] networking::SendResult SendCounted(networking::Server& network, HostMetrics& metrics,
                                                 networking::PeerId peer, const networking::Payload& payload,
                                                 networking::Reliability reliability);

/// Takes one message a tick sends: payload, an encoded message, to peer as reliability says.
using TickMessageSink = std::function<void(networking::PeerId, const networking::Payload&, networking::Reliability)>;

/// Hands send every message tick's state holds for the recipients, in the
/// order the Host sends them: each recipient's Authoritative State update, in
/// the order of to.recipients; then every Shot, Hit confirmation and Death.
/// The decision SendTickMessages puts on the wire, apart so it is tested
/// without a network. Every message is encoded before any is handed on, so if
/// the protocol cannot carry one, none of the tick's is: the broken invariant
/// is returned instead (EncodeToSend in wire.h), for the runtime to stop on.
[[nodiscard]] std::expected<void, failure::Failure> ForEachTickMessage(const simulation::State& state, tick::Tick tick,
                                                                       const TickRecipients& to,
                                                                       const TickMessageSink& send);

/// Sends to the recipients what tick's state holds for them, counting into
/// metrics what the transport accepted. Stops at, and returns, the broken
/// invariant (failure::Code::kInvariantViolated) before sending anything, as
/// ForEachTickMessage does, or the local transport's first failure; a
/// recipient that drops a message is its own outcome.
[[nodiscard]] std::expected<void, failure::Failure> SendTickMessages(networking::Server& network, HostMetrics& metrics,
                                                                     const simulation::State& state, tick::Tick tick,
                                                                     const TickRecipients& to);

}  // namespace augusta::server

#endif  // AUGUSTA_SERVER_TICK_MESSAGES_H_
