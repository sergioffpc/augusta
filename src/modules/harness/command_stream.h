#ifndef AUGUSTA_HARNESS_COMMAND_STREAM_H_
#define AUGUSTA_HARNESS_COMMAND_STREAM_H_

#include <cstdint>
#include <deque>
#include <vector>

#include "augusta/command.h"
#include "augusta/harness.h"
#include "augusta/harness_wire.h"
#include "augusta/prediction.h"

/// \file
/// The client's side of each tick in a Session (ADR-0021, ADR-0024): its own
/// player predicted on PredictionWorld and reconciled with what the server last
/// acknowledged, and the commands the server has not yet acknowledged, which
/// go out again with each new one. Decides what to send; Session sends it.
/// Prediction thread only. Private to the harness module.
namespace augusta::harness {

/// What one tick predicted, and the commands to send for it.
struct CommandTick {
  prediction::State state{};
  /// Every command the server has not yet acknowledged, this tick's last; empty
  /// when nothing is to be sent, outside a match.
  std::vector<SequencedCommand> send;
};

/// The predicted player and the stream of commands that moves it on the server.
class CommandStream {
 public:
  explicit CommandStream(prediction::World prediction);

  /// Runs one tick of command, delta_time seconds long, against server_view,
  /// as Session::Tick describes.
  [[nodiscard]] CommandTick Tick(const ServerView& server_view, const command::Command& command, float delta_time);

  /// The sequence the next command sent goes under.
  [[nodiscard]] command::Sequence NextSequence() const { return next_sequence_; }

 private:
  // Keeps command under sequence with the commands server_view does not yet
  // acknowledge, and returns them all, as one message carries them.
  std::vector<SequencedCommand> Queue(const ServerView& server_view, command::Sequence sequence,
                                      const command::Command& command);

  prediction::World prediction_;
  // Which match the prediction was last started over in
  // (ServerView::matches_started), 0 for none, and what it last predicted.
  std::uint32_t started_match_ = 0;
  prediction::State last_state_{};
  // The commands still waiting to be acknowledged, and the sequence the next
  // one goes under. Sequences start at 1; 0 means none. They count this
  // connection's commands alone, so 32 bits outlast any session (ADR-0038).
  std::deque<SequencedCommand> unacknowledged_;
  command::Sequence next_sequence_ = 1;
};

}  // namespace augusta::harness

#endif  // AUGUSTA_HARNESS_COMMAND_STREAM_H_
