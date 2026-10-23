#ifndef AUGUSTA_TICK_MESSAGES_H_
#define AUGUSTA_TICK_MESSAGES_H_

#include <expected>
#include <functional>
#include <unordered_map>
#include <vector>

#include "augusta/failure.h"
#include "augusta/host_metrics.h"
#include "augusta/link.h"
#include "augusta/match.h"
#include "augusta/replication.h"
#include "augusta/simulation.h"
#include "augusta/tick.h"

#if AUGUSTA_DEBUG_TOOLS
#include <unordered_set>

#include "augusta/debug_subscriptions.h"
#endif

/// \file
/// What Host sends its players after each tick (ADR-0044): each one's update,
/// unreliably, since a newer one supersedes it; then, reliably, what must
/// arrive: every Shot and every Death of the tick to all of them, and each Hit
/// confirmation to its shooter alone, if it is still in the match; in a
/// Development build, each Shot judgement after them, to its shooter alone, if
/// it is still in the match and asked for them (ADR-0038). replication
/// decides what each message holds; this has the Link encode them, in that
/// order, the Authoritative State's bodies once for every player (link::Readdressed)
/// (ForEachTickMessage, tested without a network), and sends them on the
/// Host's channels, each with the reliability the Link gives its type,
/// counting each into the Host's metrics (host_metrics.h).
namespace augusta::host {

/// Who a tick's messages go to: each player in the match.
struct TickRecipients {
  std::vector<replication::Recipient> recipients;
  /// The connection of each recipient, by the entity its player controls.
  std::unordered_map<EntityId, link::PeerId> peers;
#if AUGUSTA_DEBUG_TOOLS
  /// Of peers, those Shot judgements go to (ShotJudgementPeers).
  std::unordered_set<link::PeerId> shot_judgement_peers;
#endif
};

#if AUGUSTA_DEBUG_TOOLS
/// Of to.peers, those subscriptions sends Shot judgements to: the server
/// serves them (`debug.shot_judgements`) and the peer's last Debug subscribe
/// set their bit. What the Host fills TickRecipients::shot_judgement_peers
/// with, under the lock that guards subscriptions.
[[nodiscard]] std::unordered_set<link::PeerId> ShotJudgementPeers(const TickRecipients& to,
                                                                  const DebugSubscriptions& subscriptions);
#endif

/// Sends message to peer, counting it into metrics only if the Link accepted
/// it: how the Host sends everything it sends. Returns what the Link did with
/// it.
[[nodiscard]] link::SendResult SendCounted(link::Server& network, HostMetrics& metrics, link::PeerId peer,
                                           const link::Outgoing& message);

/// Takes one message a tick sends, encoded, to peer. Returns whether to go on:
/// false hands on nothing more.
using TickMessageSink = std::function<bool(link::PeerId, const link::Outgoing&)>;

/// Hands send every message the tick of result holds for the recipients, in
/// the order the Host sends them: each recipient's Authoritative State update,
/// in the order of to.recipients; then every Shot, Hit confirmation and Death;
/// then, in a Development build, every Shot judgement whose shooter's peer is
/// one of to.shot_judgement_peers; until send declines one.
/// The decision SendTickMessages puts on the wire, apart so it is tested
/// without a network. Every message is encoded before any is handed on, so if
/// the protocol cannot carry one, none of the tick's is: the broken invariant
/// is returned instead (link::Encode, augusta/link.h), for the runtime to stop on.
[[nodiscard]] std::expected<void, failure::Failure> ForEachTickMessage(const simulation::TickResult& result,
                                                                       tick::Tick tick, const TickRecipients& to,
                                                                       const TickMessageSink& send);

/// Sends to the recipients what the tick of result holds for them, counting into
/// metrics what the Link accepted. Stops at, and returns, the broken
/// invariant (failure::Code::kInvariantViolated) before sending anything, as
/// ForEachTickMessage does, or the local transport's first failure; a
/// recipient that drops a message is its own outcome. Asks halted before each
/// message, and sends nothing more once it says the runtime has failed.
[[nodiscard]] std::expected<void, failure::Failure> SendTickMessages(link::Server& network, HostMetrics& metrics,
                                                                     const simulation::TickResult& result,
                                                                     tick::Tick tick, const TickRecipients& to,
                                                                     const std::function<bool()>& halted);

}  // namespace augusta::host

#endif  // AUGUSTA_TICK_MESSAGES_H_
