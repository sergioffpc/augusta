#include <steam/isteamnetworkingutils.h>
#include <steam/steamnetworkingtypes.h>

#include "augusta/logging.h"
#include "augusta/networking.h"

namespace augusta::networking {

namespace {

// The transport's fake-packet settings of one side: what a process sends, or what it receives.
struct FakePacketSide {
  ESteamNetworkingConfigValue lag;
  ESteamNetworkingConfigValue jitter_avg;
  ESteamNetworkingConfigValue jitter_max;
  ESteamNetworkingConfigValue jitter_pct;
  ESteamNetworkingConfigValue loss;
  ESteamNetworkingConfigValue reorder;
};

constexpr FakePacketSide kSendSide{
    .lag = k_ESteamNetworkingConfig_FakePacketLag_Send,
    .jitter_avg = k_ESteamNetworkingConfig_FakePacketJitter_Send_Avg,
    .jitter_max = k_ESteamNetworkingConfig_FakePacketJitter_Send_Max,
    .jitter_pct = k_ESteamNetworkingConfig_FakePacketJitter_Send_Pct,
    .loss = k_ESteamNetworkingConfig_FakePacketLoss_Send,
    .reorder = k_ESteamNetworkingConfig_FakePacketReorder_Send,
};

constexpr FakePacketSide kReceiveSide{
    .lag = k_ESteamNetworkingConfig_FakePacketLag_Recv,
    .jitter_avg = k_ESteamNetworkingConfig_FakePacketJitter_Recv_Avg,
    .jitter_max = k_ESteamNetworkingConfig_FakePacketJitter_Recv_Max,
    .jitter_pct = k_ESteamNetworkingConfig_FakePacketJitter_Recv_Pct,
    .loss = k_ESteamNetworkingConfig_FakePacketLoss_Recv,
    .reorder = k_ESteamNetworkingConfig_FakePacketReorder_Recv,
};

// Sets one side's latency, jitter, loss and reorder share to conditions'.
void SetFakePackets(ISteamNetworkingUtils& utils, const FakePacketSide& side, const SimulatedConditions& conditions) {
  utils.SetGlobalConfigValueInt32(side.lag, conditions.latency_ms);
  // Every packet draws a jitter; a mean of 0 draws none.
  constexpr float kEveryPacket = 100.0F;
  utils.SetGlobalConfigValueFloat(side.jitter_avg, static_cast<float>(conditions.jitter_mean_ms));
  utils.SetGlobalConfigValueFloat(side.jitter_max, static_cast<float>(conditions.jitter_max_ms));
  utils.SetGlobalConfigValueFloat(side.jitter_pct, kEveryPacket);
  utils.SetGlobalConfigValueFloat(side.loss, conditions.loss_percent);
  utils.SetGlobalConfigValueFloat(side.reorder, conditions.reorder_percent);
}

}  // namespace

void SimulateNetworkConditions(const SimulatedConditions& conditions) {
  ISteamNetworkingUtils* utils = SteamNetworkingUtils();
  SetFakePackets(*utils, kSendSide, conditions);
  // The receive side is set either way, so conditions on it from an earlier
  // call never outlive a call that leaves it out.
  SetFakePackets(*utils, kReceiveSide,
                 conditions.applies_to == SimulatedTraffic::kSentAndReceived ? conditions : SimulatedConditions{});
  // One delay for both sides in the transport.
  utils->SetGlobalConfigValueInt32(k_ESteamNetworkingConfig_FakePacketReorder_Time, conditions.reorder_delay_ms);
  for (const ESteamNetworkingConfigValue timeout :
       {k_ESteamNetworkingConfig_TimeoutInitial, k_ESteamNetworkingConfig_TimeoutConnected}) {
    if (conditions.timeout_ms > 0) {
      utils->SetGlobalConfigValueInt32(timeout, conditions.timeout_ms);
    } else {
      // A null value clears the override, back to the library's default.
      utils->SetConfigValue(timeout, k_ESteamNetworkingConfig_Global, 0, k_ESteamNetworkingConfig_Int32, nullptr);
    }
  }
  LOG_INFO(
      "subsystem=networking event=simulated_conditions latency_ms={} jitter_mean_ms={} jitter_max_ms={} "
      "loss_percent={} reorder_percent={} reorder_delay_ms={} applies_to={}",
      conditions.latency_ms, conditions.jitter_mean_ms, conditions.jitter_max_ms, conditions.loss_percent,
      conditions.reorder_percent, conditions.reorder_delay_ms,
      conditions.applies_to == SimulatedTraffic::kSentAndReceived ? "sent_and_received" : "sent");
}

}  // namespace augusta::networking
