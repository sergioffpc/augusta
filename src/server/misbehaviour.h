#ifndef AUGUSTA_SERVER_MISBEHAVIOUR_H_
#define AUGUSTA_SERVER_MISBEHAVIOUR_H_

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <string_view>

// The server's boundary for a peer that keeps sending what no honest client
// sends: every rejection of what one peer sent is recorded here, and past a
// threshold of misbehaviour within a sliding window the peer is to be
// disconnected. Pure (no I/O, no clock of its own: the caller hands it the
// time), so it is tested without a network. Routine rejections, which an
// honest client causes under latency and loss, never count.
namespace augusta::server {

/// Why the server turned away something a peer sent.
enum class PeerRejection : std::uint8_t {
  /// Misbehaviour: no honest client sends these.
  /// The bytes did not decode as a message.
  kUndecodable,
  /// A message only the server sends.
  kNotAClientMessage,
  /// A command with a NaN or infinite number in it.
  kNonFiniteCommand,
  /// A command with a number outside what a client can produce.
  kOutOfRangeCommand,
  /// Commands from a peer that has not joined.
  kCommandsBeforeJoining,
  /// Routine: commands repeat until acknowledged, some are in flight when a
  /// Match ends, the Roster can change while a Ready is in flight, and a refused
  /// peer has done nothing wrong.
  /// A command whose sequence is not newer than the last taken in.
  kStaleCommand,
  /// Commands from a player not in a Match.
  kCommandsOutsideMatch,
  /// A Ready for a Roster version that is not the current one.
  kStaleReady,
  /// A Join the server refused.
  kJoinRefused,
};

/// Whether rejection counts toward a disconnect: false for the routine ones.
[[nodiscard]] bool IsMisbehaviour(PeerRejection rejection);

/// A short lowercase description of rejection, for logs.
[[nodiscard]] std::string_view DescribePeerRejection(PeerRejection rejection);

/// How long a misbehaviour counts toward a disconnect. A boundary constant,
/// like the command queue's cap: not a Parameter, not a setting.
inline constexpr std::chrono::seconds kMisbehaviourWindow{5};

/// How many misbehaviours within kMisbehaviourWindow disconnect a peer. An
/// honest client sends none, whatever its latency and loss; the slack lets a
/// buggy one's odd message through without leaving it a way to spend the
/// Network I/O thread's time forever.
inline constexpr std::size_t kMisbehaviourThreshold = 16;

/// What to do with a peer after a rejection.
enum class Verdict : std::uint8_t {
  kKeep,
  kDisconnect,
};

/// One peer's misbehaviours within the window.
class MisbehaviourTracker {
 public:
  /// Records rejection, at now, and decides whether the peer is to be
  /// disconnected: once kMisbehaviourThreshold misbehaviours fall within the
  /// kMisbehaviourWindow ending at now.
  [[nodiscard]] Verdict Record(PeerRejection rejection, std::chrono::steady_clock::time_point now);

 private:
  // When each misbehaviour still in the window happened, oldest first.
  std::deque<std::chrono::steady_clock::time_point> recent_;
};

}  // namespace augusta::server

#endif  // AUGUSTA_SERVER_MISBEHAVIOUR_H_
