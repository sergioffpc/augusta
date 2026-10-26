#include "tick_messages.h"

#include <expected>
#include <functional>
#include <optional>
#include <utility>
#include <vector>

#include "augusta/failure.h"
#include "augusta/host_metrics.h"
#include "augusta/link.h"
#include "augusta/link_types.h"
#include "augusta/replication.h"
#include "augusta/simulation.h"
#include "augusta/tick.h"
#include "link_mapping.h"
#include "simulation_mapping.h"

#if AUGUSTA_DEBUG_TOOLS
#include <unordered_set>

#include "debug_subscriptions.h"
#endif

namespace augusta::host {

link::SendResult SendCounted(link::Server& network, HostMetrics& metrics, link::PeerId peer,
                             const link::Outgoing& message) {
  link::SendResult sent = network.Send(peer, message);
  if (sent == link::SendOutcome::kAccepted) {
    CountSent(metrics, message);
  }
  return sent;
}

namespace {

// One message of a tick, encoded, and who it goes to.
struct Addressed {
  std::vector<link::PeerId> peers;
  link::Outgoing message;
};

// A tick's messages, each encoded as it is added, in order; and the first the
// protocol could not carry, after which none is kept.
struct TickMessages {
  std::vector<Addressed> addressed;
  std::optional<failure::Failure> broken;

  void Add(std::expected<link::Outgoing, failure::Failure> message, std::vector<link::PeerId> peers) {
    if (broken.has_value()) {
      return;
    }
    if (!message.has_value()) {
      broken = std::move(message.error());
      return;
    }
    addressed.push_back({.peers = std::move(peers), .message = *std::move(message)});
  }
};

// One state, its bodies mapped and encoded once, readdressed to each recipient
// in turn: only the recipient's own fields are encoded for each.
void AddUpdates(TickMessages& messages, const simulation::State& state, tick::Tick tick, const TickRecipients& to) {
  const replication::Updates updates = replication::PlanUpdates(state, tick, to.recipients);
  if (updates.recipients.empty()) {
    return;
  }
  link::AuthoritativeState own = ToLink(updates);
  FillRecipientFields(own, updates.recipients.front());
  const std::expected<link::Outgoing, failure::Failure> shared = link::Encode(own);
  for (const replication::RecipientUpdate& recipient : updates.recipients) {
    FillRecipientFields(own, recipient);
    messages.Add(shared.and_then([&own](const link::Outgoing& encoded) { return link::Readdressed(encoded, own); }),
                 {to.peers.at(FromSimulation(recipient.entity))});
  }
}

#if AUGUSTA_DEBUG_TOOLS
// Each Shot judgement of result, to its shooter's peer if it asked for them: a
// shooter no longer in the match has no peer to tell.
void AddShotJudgements(TickMessages& messages, const simulation::TickResult& result, const TickRecipients& to) {
  for (const simulation::ShotJudgement& judgement : result.shot_judgements) {
    if (const auto shooter = to.peers.find(FromSimulation(judgement.shooter));
        shooter != to.peers.end() && to.shot_judgement_peers.contains(shooter->second)) {
      messages.Add(link::Encode(ToLink(judgement)), {shooter->second});
    }
  }
}
#endif

}  // namespace

#if AUGUSTA_DEBUG_TOOLS
std::unordered_set<link::PeerId> ShotJudgementPeers(const TickRecipients& to, const DebugSubscriptions& subscriptions) {
  std::unordered_set<link::PeerId> wanting;
  for (const auto& [entity, peer] : to.peers) {
    if (subscriptions.Wants(peer, link::DebugSubscribe::kShotJudgements)) {
      wanting.insert(peer);
    }
  }
  return wanting;
}
#endif

std::expected<void, failure::Failure> ForEachTickMessage(const simulation::TickResult& result, tick::Tick tick,
                                                         const TickRecipients& to, const TickMessageSink& send) {
  const simulation::State& state = result.state;
  // Every message is encoded before any is handed on, so one the protocol
  // cannot carry stops the whole tick, not the rest of it.
  TickMessages messages;
  std::vector<link::PeerId> everyone;
  everyone.reserve(to.peers.size());
  for (const auto& [entity, peer] : to.peers) {
    everyone.push_back(peer);
  }

  AddUpdates(messages, state, tick, to);
  for (const replication::Shot& shot : replication::PlanShots(state, tick)) {
    messages.Add(link::Encode(ToLink(shot)), everyone);
  }
  for (const replication::HitConfirmation& hit : replication::PlanHitConfirmations(state)) {
    if (const auto shooter = to.peers.find(FromSimulation(hit.recipient)); shooter != to.peers.end()) {
      messages.Add(link::Encode(ToLink(hit)), {shooter->second});
    }
  }
  for (const replication::Death& death : replication::PlanDeaths(state)) {
    messages.Add(link::Encode(ToLink(death)), everyone);
  }
#if AUGUSTA_DEBUG_TOOLS
  AddShotJudgements(messages, result, to);
#endif
  if (messages.broken.has_value()) {
    return std::unexpected(*std::move(messages.broken));
  }

  for (const Addressed& outgoing : messages.addressed) {
    for (const link::PeerId peer : outgoing.peers) {
      if (!send(peer, outgoing.message)) {
        return {};
      }
    }
  }
  return {};
}

std::expected<void, failure::Failure> SendTickMessages(link::Server& network, HostMetrics& metrics,
                                                       const simulation::TickResult& result, tick::Tick tick,
                                                       const TickRecipients& to, const std::function<bool()>& halted) {
  std::optional<failure::Failure> failed;
  // Nothing is handed on if the tick's messages could not all be encoded.
  if (auto handed = ForEachTickMessage(
          result, tick, to,
          [&network, &metrics, &failed, &halted](link::PeerId peer, const link::Outgoing& message) {
            // Once the transport has failed, or the runtime has elsewhere,
            // nothing more is sent.
            if (halted()) {
              return false;
            }
            link::SendResult sent = SendCounted(network, metrics, peer, message);
            if (!sent.has_value()) {
              failed = std::move(sent.error());
              return false;
            }
            if (*sent == link::SendOutcome::kAccepted && message.Type() == MessageType::kAuthoritativeState) {
              metrics.authoritative_state_update_bytes.Observe(static_cast<double>(message.Payload().size()));
            }
            return true;
          });
      !handed.has_value()) {
    return handed;
  }
  if (failed.has_value()) {
    return std::unexpected(std::move(*failed));
  }
  return {};
}

}  // namespace augusta::host
