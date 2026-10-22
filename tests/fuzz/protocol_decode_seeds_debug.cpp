#include <vector>

#include "augusta/math.h"
#include "augusta/protocol.h"
#include "protocol_decode_seeds.h"

// A Development build's messages' seeds (ADR-0038, ADR-0074).
namespace augusta::protocol_decode_seeds {

std::vector<Seed> DevelopmentSeeds() {
  // Two ticks of a flight, the second crossing two players, every stance in use.
  const protocol::ShotJudgementWire judgement{.shot_tick = 1200,
                                              .entries = {{.players = {{.position = math::Vec3(3.0F, 0.0F, -40.0F),
                                                                        .entity = protocol::EntityIdWire{2},
                                                                        .yaw = 1.5F,
                                                                        .stance = protocol::StanceWire::kStanding}},
                                                           .ticks_after_shot = 0},
                                                          {.players = {{.position = math::Vec3(3.5F, 0.0F, -42.0F),
                                                                        .entity = protocol::EntityIdWire{2},
                                                                        .yaw = 1.5F,
                                                                        .stance = protocol::StanceWire::kCrouching},
                                                                       {.position = math::Vec3(-1.25F, 0.0F, -43.0F),
                                                                        .entity = protocol::EntityIdWire{3},
                                                                        .yaw = -0.75F,
                                                                        .stance = protocol::StanceWire::kProne}},
                                                           .ticks_after_shot = 1}},
                                              .point = math::Vec3(3.5F, 1.625F, -42.0F),
                                              .target = protocol::EntityIdWire{2},
                                              .delay_fraction = 0.5F,
                                              .delay_ticks = 6,
                                              .struck = protocol::StruckWire::kHitbox,
                                              .part = protocol::BodyPartWire::kHead,
                                              .flags = protocol::ShotJudgementWire::kEntriesLeftOut};
  return {
      {.name = "debug_subscribe",
       .message = protocol::DebugSubscribeWire{.flags = protocol::DebugSubscribeWire::kShotJudgements}},
      {.name = "shot_judgement", .message = judgement},
  };
}

}  // namespace augusta::protocol_decode_seeds
