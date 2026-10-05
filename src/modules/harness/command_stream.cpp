#include "command_stream.h"

#include <optional>
#include <utility>
#include <vector>

#include "augusta/command.h"
#include "augusta/harness.h"
#include "augusta/harness_wire.h"
#include "augusta/math.h"
#include "augusta/prediction.h"
#include "augusta/protocol.h"

namespace augusta::harness {

namespace {

// Where Match start put this client's own player. The view is in a match,
// whose start names this client (Inbox).
math::Vec3 OwnSpawn(const ServerView& server_view) {
  for (const MatchPlayer& player : server_view.match_start->players) {
    if (player.session == server_view.accepted->session) {
      return player.spawn;
    }
  }
  return {};
}

// What the server's state says about this client's own player: its body and its rifle.
std::optional<prediction::Acknowledgement> OwnAcknowledgement(const ServerView& server_view) {
  const std::optional<EntityId> own = server_view.OwnEntity();
  if (!own.has_value() || !server_view.authoritative.has_value()) {
    return std::nullopt;
  }
  for (const EntityBody& body : server_view.authoritative->bodies) {
    if (body.entity == *own) {
      return prediction::Acknowledgement{
          .sequence = server_view.authoritative->acknowledged_sequence,
          .body = body.body,
          .rifle = server_view.authoritative->rifle,
      };
    }
  }
  return std::nullopt;
}

}  // namespace

CommandStream::CommandStream(prediction::World prediction) : prediction_(std::move(prediction)) {}

CommandTick CommandStream::Tick(const ServerView& server_view, const command::Command& command, float delta_time) {
  // Outside a match nothing the player presses affects one.
  if (!server_view.in_match) {
    return CommandTick{.state = last_state_, .send = {}};
  }
  // Each match starts its player over where Match start put it. Sequences keep
  // growing across matches, so nothing of the last one is mistaken for this one's.
  if (started_match_ != server_view.matches_started) {
    prediction_.Start(OwnSpawn(server_view), server_view.accepted->parameters);
    unacknowledged_.clear();
    started_match_ = server_view.matches_started;
  }
  const command::Sequence sequence = next_sequence_++;
  // A dead player's body and rifle are gone from the server: there is nothing
  // to predict or reconcile, and its commands keep only the stream in step.
  if (!server_view.OwnAlive()) {
    command::Command unarmed = command;
    unarmed.fire = false;
    return CommandTick{.state = last_state_, .send = Queue(server_view, sequence, unarmed)};
  }
  last_state_ = prediction_.Tick(command, sequence, OwnAcknowledgement(server_view), delta_time);
  return CommandTick{.state = last_state_, .send = Queue(server_view, sequence, command)};
}

std::vector<SequencedCommand> CommandStream::Queue(const ServerView& server_view, command::Sequence sequence,
                                                   const command::Command& command) {
  // Commands the server has already processed need not go again.
  if (server_view.authoritative.has_value()) {
    while (!unacknowledged_.empty() &&
           unacknowledged_.front().sequence <= server_view.authoritative->acknowledged_sequence) {
      unacknowledged_.pop_front();
    }
  }
  // Keeps at most the newest kMaxCommandsPerMessage, all a message can carry.
  unacknowledged_.push_back(SequencedCommand{.sequence = sequence, .command = command});
  if (unacknowledged_.size() > protocol::kMaxCommandsPerMessage) {
    unacknowledged_.pop_front();
  }
  return {unacknowledged_.begin(), unacknowledged_.end()};
}

}  // namespace augusta::harness
