#include "status_handlers.h"

#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <utility>

#include <steam/isteamnetworkingsockets.h>

namespace augusta::networking {

namespace {

// Every registered handler, by the id its connections carry as user data.
class StatusHandlerRegistry {
 public:
  std::int64_t Add(StatusHandler handler) {
    const std::lock_guard<std::mutex> lock(mutex_);
    const std::int64_t handler_id = next_id_++;
    handlers_.emplace(handler_id, std::move(handler));
    return handler_id;
  }

  void Remove(std::int64_t handler_id) {
    const std::lock_guard<std::mutex> lock(mutex_);
    handlers_.erase(handler_id);
  }

  void Dispatch(std::int64_t handler_id, SteamNetConnectionStatusChangedCallback_t* info) {
    const std::lock_guard<std::mutex> lock(mutex_);
    if (const auto found = handlers_.find(handler_id); found != handlers_.end()) {
      found->second(info);
    }
  }

 private:
  std::mutex mutex_;
  // 0 is GameNetworkingSockets' "no user data".
  std::int64_t next_id_ = 1;
  std::unordered_map<std::int64_t, StatusHandler> handlers_;
};

StatusHandlerRegistry& GlobalStatusHandlers() {
  static StatusHandlerRegistry registry;
  return registry;
}

}  // namespace

StatusHandlerRegistration::StatusHandlerRegistration(StatusHandler handler)
    : id_(GlobalStatusHandlers().Add(std::move(handler))) {}

StatusHandlerRegistration::~StatusHandlerRegistration() { GlobalStatusHandlers().Remove(id_); }

void OnStatusChanged(SteamNetConnectionStatusChangedCallback_t* info) {
  GlobalStatusHandlers().Dispatch(info->m_info.m_nUserData, info);
}

}  // namespace augusta::networking
