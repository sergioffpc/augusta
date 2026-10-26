#ifndef AUGUSTA_NETWORKING_STATUS_HANDLERS_H_
#define AUGUSTA_NETWORKING_STATUS_HANDLERS_H_

#include <cstdint>
#include <functional>

#include <steam/steamnetworkingtypes.h>

/// \file
/// How GameNetworkingSockets' connection-status-changed callback reaches its
/// owner, private to the module. Both Client and Server route it back to their
/// own Impl via SteamNetConnectionInfo_t::m_nUserData, set on the connection as
/// it is created or accepted; each registers its own handler and passes it as a
/// per-connection/per-listen-socket config value (rather than one shared
/// dispatcher) so a process hosting both a Client and a Server at once - as the
/// standalone round-trip test does - never has to disambiguate whose connection
/// it is. The handler only publishes the event (transport_events.h); PumpEvents,
/// the Network I/O owner, applies it.
namespace augusta::networking {

using StatusHandler = std::function<void(SteamNetConnectionStatusChangedCallback_t*)>;

/// Registers a handler for as long as it lives. An owner declares it as its
/// last member, so it unregisters first - before the members the handler
/// reads are destroyed.
///
/// GameNetworkingSockets queues connection-status events and delivers them on
/// the next RunCallbacks, each carrying the user data it was created with - so
/// an event for a connection whose Client/Server has since been destroyed
/// still arrives. The user data is therefore an id, never a pointer: a
/// destroyed owner's id is unregistered and its events are dropped, and ids are
/// never reused, so a new owner allocated at the same address cannot receive
/// them. A handler runs under the registry's lock, so an owner cannot finish
/// unregistering (and be destroyed) mid-callback.
class StatusHandlerRegistration {
 public:
  explicit StatusHandlerRegistration(StatusHandler handler);
  ~StatusHandlerRegistration();
  StatusHandlerRegistration(const StatusHandlerRegistration&) = delete;
  StatusHandlerRegistration& operator=(const StatusHandlerRegistration&) = delete;

  /// What to set as the connection's user data so its events reach the handler.
  [[nodiscard]] std::int64_t Id() const { return id_; }

 private:
  std::int64_t id_;
};

/// The status-changed callback both roles register with GameNetworkingSockets.
void OnStatusChanged(SteamNetConnectionStatusChangedCallback_t* info);

}  // namespace augusta::networking

#endif  // AUGUSTA_NETWORKING_STATUS_HANDLERS_H_
