#ifndef AUGUSTA_HOST_PEER_GATE_H_
#define AUGUSTA_HOST_PEER_GATE_H_

#include <chrono>
#include <functional>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "admission.h"
#include "augusta/failure_latch.h"
#include "augusta/host_log.h"
#include "augusta/host_metrics.h"
#include "augusta/link.h"
#include "augusta/misbehaviour.h"

/// \file
/// What every augustad server - a live host::Host and a replay
/// host::ReplayServer (ADR-0051) - does with a peer before and around what it
/// asks for: every connection is accepted, since a refusal needs one to travel
/// on; a peer not admitted within kAdmissionDeadline (admission.h), or that
/// keeps sending what no honest client sends (augusta/misbehaviour.h), is
/// expelled; and nothing more is taken in from an expelled peer in the round it
/// was expelled in. PeerGate keeps that state, and PumpPeers runs one round of
/// a server's Network I/O work through it, so each server only says what a
/// message, a departure and an expulsion mean to it. Both are guarded by the
/// owning server's lock.
namespace augusta::host {

/// Each connected peer's admission deadline and misbehaviour, and the peers
/// expelled in the round under way. Its owner's lock guards every call.
class PeerGate {
 public:
  /// Counts the misbehaviours it judges into metrics, which must outlive it.
  explicit PeerGate(HostMetrics& metrics) : metrics_(metrics) {}

  /// peer connected at now: its admission deadline starts.
  void Connected(link::PeerId peer, std::chrono::steady_clock::time_point now);

  /// peer was admitted: it has no deadline any more.
  void Admitted(link::PeerId peer);

  /// peer's connection is gone, however it went: everything of it is forgotten.
  void Left(link::PeerId peer);

  /// Counts rejection, at now, toward peer's misbehaviour, into the metrics
  /// too if it is one, and says whether peer is now to be expelled.
  [[nodiscard]] Verdict Judge(link::PeerId peer, PeerRejection rejection, std::chrono::steady_clock::time_point now);

  /// peer was expelled this round: what it sent later in it is ignored.
  void Expelled(link::PeerId peer);

  [[nodiscard]] bool WasExpelled(link::PeerId peer) const;

  /// The peers whose admission deadline has passed at now, each once.
  [[nodiscard]] std::vector<link::PeerId> TakeOverdue(std::chrono::steady_clock::time_point now);

  /// The round is over: a closed connection delivers nothing more.
  void EndRound();

 private:
  HostMetrics& metrics_;
  std::unordered_map<link::PeerId, MisbehaviourTracker> misbehaviour_;
  std::unordered_set<link::PeerId> expelled_;
  AdmissionDeadlines admission_deadlines_;
};

/// What a server does with what PumpPeers meets, each with its lock held.
struct PeerHandlers {
  /// peer's connection ended, as how says.
  std::function<void(link::PeerId peer, Leaving how)> disconnected;
  /// A message from a peer not expelled this round. lock holds the server's
  /// mutex: a handler may let it go for work that needs no lock, and must
  /// take it again before it returns.
  std::function<void(const link::PeerMessage& message, std::unique_lock<std::mutex>& lock)> message;
  /// peer was not admitted within kAdmissionDeadline: it is to be expelled.
  std::function<void(link::PeerId peer)> overdue;
};

/// One round of a server's Network I/O work, at now: accepts each connection
/// and starts its deadline, hands on each departure and each message
/// received, counting its bytes, then each peer past its deadline, and ends
/// the round. A local transport failure to receive is kept in
/// transport_failure, and ends the round there.
void PumpPeers(link::Server& network, HostMetrics& metrics, std::mutex& mutex, PeerGate& gate,
               std::chrono::steady_clock::time_point now, const PeerHandlers& handlers,
               failure::FailureLatch& transport_failure);

}  // namespace augusta::host

#endif  // AUGUSTA_HOST_PEER_GATE_H_
