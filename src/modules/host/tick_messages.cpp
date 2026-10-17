#include "augusta/tick_messages.h"

#include <expected>
#include <functional>
#include <optional>
#include <utility>
#include <vector>

#include "augusta/failure.h"
#include "augusta/host_metrics.h"
#include "augusta/link.h"
#include "augusta/link_mapping.h"
#include "augusta/link_types.h"
#include "augusta/replication.h"
#include "augusta/simulation.h"
#include "augusta/simulation_mapping.h"
#include "augusta/tick.h"

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

}  // namespace

std::expected<void, failure::Failure> ForEachTickMessage(const simulation::State& state, tick::Tick tick,
                                                         const TickRecipients& to, const TickMessageSink& send) {
  // Every message is encoded before any is handed on, so one the protocol
  // cannot carry stops the whole tick, not the rest of it.
  std::vector<Addressed> tick_messages;
  std::optional<failure::Failure> broken;
  const auto add = [&tick_messages, &broken](std::expected<link::Outgoing, failure::Failure> message,
                                             std::vector<link::PeerId> peers) {
    if (broken.has_value()) {
      return;
    }
    if (!message.has_value()) {
      broken = std::move(message.error());
      return;
    }
    tick_messages.push_back({.peers = std::move(peers), .message = *std::move(message)});
  };
  std::vector<link::PeerId> everyone;
  everyone.reserve(to.peers.size());
  for (const auto& [entity, peer] : to.peers) {
    everyone.push_back(peer);
  }

  const replication::Updates updates = replication::PlanUpdates(state, tick, to.recipients);
  // One state, its bodies mapped and encoded once, readdressed to each
  // recipient in turn: only the recipient's own fields are encoded for each.
  if (!updates.recipients.empty()) {
    link::AuthoritativeState own = ToLink(updates);
    Address(own, updates.recipients.front());
    const std::expected<link::Outgoing, failure::Failure> shared = link::Encode(own);
    for (const replication::RecipientUpdate& recipient : updates.recipients) {
      Address(own, recipient);
      add(shared.and_then([&own](const link::Outgoing& encoded) { return link::Readdressed(encoded, own); }),
          {to.peers.at(FromSimulation(recipient.entity))});
    }
  }
  for (const replication::Shot& shot : replication::PlanShots(state, tick)) {
    add(link::Encode(ToLink(shot)), everyone);
  }
  for (const replication::HitConfirmation& hit : replication::PlanHitConfirmations(state)) {
    if (const auto shooter = to.peers.find(FromSimulation(hit.recipient)); shooter != to.peers.end()) {
      add(link::Encode(ToLink(hit)), {shooter->second});
    }
  }
  for (const replication::Death& death : replication::PlanDeaths(state)) {
    add(link::Encode(ToLink(death)), everyone);
  }
  if (broken.has_value()) {
    return std::unexpected(*std::move(broken));
  }

  for (const Addressed& outgoing : tick_messages) {
    for (const link::PeerId peer : outgoing.peers) {
      if (!send(peer, outgoing.message)) {
        return {};
      }
    }
  }
  return {};
}

std::expected<void, failure::Failure> SendTickMessages(link::Server& network, HostMetrics& metrics,
                                                       const simulation::State& state, tick::Tick tick,
                                                       const TickRecipients& to, const std::function<bool()>& halted) {
  std::optional<failure::Failure> failed;
  // Nothing is handed on if the tick's messages could not all be encoded.
  if (auto handed = ForEachTickMessage(
          state, tick, to,
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
