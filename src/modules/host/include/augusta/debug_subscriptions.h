#ifndef AUGUSTA_DEBUG_SUBSCRIPTIONS_H_
#define AUGUSTA_DEBUG_SUBSCRIPTIONS_H_

#if AUGUSTA_DEBUG_TOOLS

#include <cstdint>
#include <string>
#include <unordered_map>

#include "augusta/link.h"

/// \file
/// Which streams of the server's state each peer asked for with its last Debug
/// subscribe, in a Development build alone (ADR-0038, ADR-0074): from the
/// peer's connection to its disconnect, whether or not it has joined or is in
/// a Match, and which of the bits it set the server does not serve, so each is
/// logged once per peer. A Debug subscribe is never misbehaviour, whatever it
/// sets. The Host keeps one, under its lock, and asks it whom a stream goes to;
/// the streams themselves, and what each holds, are the Host's.
namespace augusta::host {

/// Each connected peer's Debug subscribe flags, against the streams the server serves.
class DebugSubscriptions {
 public:
  /// served holds the bit of each stream this server sends, as its `debug`
  /// keys say (bit 0: link::DebugSubscribe::kShotJudgements).
  explicit DebugSubscriptions(std::uint8_t served) : served_(served) {}

  /// peer's latest Debug subscribe set flags, which replace its last. Returns
  /// the bits of flags the server does not serve that peer had never set
  /// before: each to be logged, once.
  [[nodiscard]] std::uint8_t Subscribe(link::PeerId peer, std::uint8_t flags);

  /// Whether stream, one bit, goes to peer: the server serves it, and peer's
  /// last Debug subscribe set it.
  [[nodiscard]] bool Wants(link::PeerId peer, std::uint8_t stream) const;

  /// peer's connection is gone: what it asked for is forgotten.
  void Forget(link::PeerId peer);

 private:
  struct Peer {
    // Its last Debug subscribe's.
    std::uint8_t flags = 0;
    // Every bit it has set that the server does not serve.
    std::uint8_t unserved_seen = 0;
  };

  std::uint8_t served_;
  std::unordered_map<link::PeerId, Peer> peers_;
};

/// The name of the stream bit stands for, a single bit, as a log line names
/// it: "shot_judgements", or "bit<n>" for one no stream uses.
[[nodiscard]] std::string DebugStreamName(std::uint8_t bit);

}  // namespace augusta::host

#endif  // AUGUSTA_DEBUG_TOOLS

#endif  // AUGUSTA_DEBUG_SUBSCRIPTIONS_H_
