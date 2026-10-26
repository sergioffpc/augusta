#include "augusta/networking.h"

#include <expected>
#include <optional>
#include <string>
#include <utility>

#include <steam/steamnetworkingsockets.h>
#include <steam/steamnetworkingtypes.h>

#include "augusta/failure.h"
#include "augusta/faults.h"
#include "augusta/logging.h"
#include "gns_adapter.h"

namespace augusta::networking {

std::expected<void, failure::Failure> Init(failure::Faults* faults) {
  if (faults != nullptr) {
    if (std::optional<std::string> tripped = faults->Trip(failure::Site::kDependencyInit)) {
      return std::unexpected(LocalFailure(failure::Code::kTransportInitFailed, {}, *std::move(tripped)));
    }
  }
  SteamNetworkingErrMsg err_msg;
  if (!GameNetworkingSockets_Init(nullptr, err_msg)) {
    return std::unexpected(LocalFailure(failure::Code::kTransportInitFailed, {}, err_msg));
  }
  LOG_INFO("subsystem=networking event=init");
  return {};
}

void Shutdown() {
  GameNetworkingSockets_Kill();
  LOG_INFO("subsystem=networking event=shutdown");
}

}  // namespace augusta::networking
