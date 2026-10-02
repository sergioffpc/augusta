#ifndef AUGUSTA_SERVER_ADMISSION_H_
#define AUGUSTA_SERVER_ADMISSION_H_

#include <chrono>
#include <unordered_map>
#include <vector>

#include "augusta/networking.h"

// The server's boundary for a peer that connects and never joins: every
// connection is accepted, since a Join refusal needs one to travel on, so a
// peer not admitted to the Lobby within a deadline is to be disconnected, or
// idle connections would pile up. Pure (no I/O, no clock of its own: the caller
// hands it the time), so it is tested without a network.
namespace augusta::server {

/// How long a connected peer has to be admitted to the Lobby. A boundary
/// constant, like the misbehaviour threshold: not a Parameter, not a setting.
/// An honest client asks to join as soon as it is connected, so it is admitted
/// or refused in a round trip; the slack is for the slowest connection, not for
/// anything a client does first.
inline constexpr std::chrono::seconds kAdmissionDeadline{30};

/// When each connected peer not yet admitted to the Lobby is due.
class AdmissionDeadlines {
 public:
  /// Starts peer's deadline, at now.
  void Connected(networking::PeerId peer, std::chrono::steady_clock::time_point now);

  /// Ends peer's deadline: it is in the Lobby. Nothing if it had none.
  void Admitted(networking::PeerId peer);

  /// Ends peer's deadline: its connection is gone. Nothing if it had none.
  void Left(networking::PeerId peer);

  /// The peers whose kAdmissionDeadline has passed at now, which are to be
  /// disconnected; their deadlines end, so each is returned once.
  [[nodiscard]] std::vector<networking::PeerId> TakeOverdue(std::chrono::steady_clock::time_point now);

 private:
  // When each peer not yet admitted connected.
  std::unordered_map<networking::PeerId, std::chrono::steady_clock::time_point> connected_at_;
};

}  // namespace augusta::server

#endif  // AUGUSTA_SERVER_ADMISSION_H_
