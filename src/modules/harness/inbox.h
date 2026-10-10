#ifndef AUGUSTA_HARNESS_INBOX_H_
#define AUGUSTA_HARNESS_INBOX_H_

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <vector>

#include "augusta/harness.h"
#include "augusta/logging.h"
#include "augusta/networking.h"
#include "augusta/protocol.h"
#include "pending.h"

/// \file
/// What a Session makes of the messages its server sends: each decoded,
/// checked against what the client already knows (a Match start that leaves it
/// out, or a state of a body not in the match, is dropped as a malformed message
/// is), and kept, either in the ServerView it publishes or, for what is handed
/// out once, until it is taken. No connection of its own: Session hands it each
/// payload the transport received. Private to the harness module.
namespace augusta::harness {

/// The server's messages, as a Session reads them.
class Inbox {
 public:
  /// viewer: whether the Session is a Replay viewer (ADR-0051), which is in
  /// no Match start it is sent.
  explicit Inbox(bool viewer = false) : viewer_(viewer) {}

  /// Not copyable or movable: the view and the pending events are shared with
  /// the threads that read them.
  Inbox(const Inbox&) = delete;
  Inbox& operator=(const Inbox&) = delete;
  Inbox(Inbox&&) = delete;
  Inbox& operator=(Inbox&&) = delete;
  ~Inbox() = default;

  /// Takes in one message the server sent. Network I/O thread only: it alone
  /// publishes the view.
  void Receive(const networking::Payload& payload);

  /// What the server has said so far. Safe to call from any thread.
  [[nodiscard]] std::shared_ptr<const ServerView> View() const;

  /// Session's TakeShots, TakeHitConfirmations and TakeDeaths, for the match
  /// a view with matches_started (ServerView::matches_started) is of. Safe to
  /// call from any thread.
  [[nodiscard]] std::vector<Shot> TakeShots(std::uint32_t matches_started);
  [[nodiscard]] std::vector<HitConfirmation> TakeHitConfirmations(std::uint32_t matches_started);
  [[nodiscard]] std::vector<Death> TakeDeaths(std::uint32_t matches_started);

 private:
  // Hands message to what takes in its kind; false if it is not one a server sends.
  bool TakeIn(const protocol::MessageWire& message);
  void OnJoinAccepted(const Admission& accepted);
  void OnJoinRefused(JoinRefusal reason);
  void OnLobby(Lobby lobby);
  void OnMatchStart(MatchStart start);
  void OnMatchEnd(const MatchEnd& end);
  void OnShot(const Shot& shot);
  void OnHitConfirmation(const HitConfirmation& hit);
  void OnDeath(const Death& death);
  void OnAuthoritativeState(AuthoritativeState state);
  void OnReplayView(ReplayView view);

  // Makes the next view from the current one changed by mutate, and publishes it.
  // Only the Network I/O thread publishes, so nothing can intervene between the load and the store.
  template <typename Mutate>
  void Publish(Mutate&& mutate) {
    auto next = std::make_shared<ServerView>(*view_.load());
    mutate(*next);
    view_.store(std::move(next));
  }

  const bool viewer_;
  std::atomic<std::shared_ptr<const ServerView>> view_{std::make_shared<const ServerView>()};
  // The Shots, the Hit confirmations and the Deaths received and not yet taken,
  // each under the Match it is of. Not part of the view: they are handed out
  // once, not read.
  Pending<Shot> shots_{kMaxPendingShots};
  Pending<HitConfirmation> hit_confirmations_{kMaxPendingHitConfirmations};
  Pending<Death> deaths_{kMaxPendingDeaths};
  // A server can send messages that are refused as fast as it likes, so their
  // warnings are limited.
  logging::Throttle drop_warnings_{std::chrono::seconds{1}};
};

}  // namespace augusta::harness

#endif  // AUGUSTA_HARNESS_INBOX_H_
