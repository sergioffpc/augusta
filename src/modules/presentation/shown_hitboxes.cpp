#include "augusta/shown_hitboxes.h"

#include <algorithm>
#include <iterator>
#include <utility>
#include <vector>

#include "augusta/ballistics.h"
#include "augusta/debug_shapes.h"
#include "augusta/hitbox.h"
#include "augusta/interpolation.h"
#include "augusta/math.h"
#include "augusta/presentation.h"

namespace augusta::presentation {

std::vector<hitbox::PlacedHitbox> PlaceShownPlayers(const CharacterHitboxes& hitboxes, const State& state) {
  std::vector<hitbox::PlacedHitbox> placed;
  for (const RemotePlayer& player : state.remote_players) {
    if (const auto character = hitboxes.find(player.character); character != hitboxes.end()) {
      const hitbox::Pose pose{.position = player.body.position, .yaw = player.body.yaw, .stance = player.body.stance};
      std::ranges::move(hitbox::Place(character->second, pose), std::back_inserter(placed));
    }
  }
  return placed;
}

void AddOutline(const hitbox::PlacedHitbox& hitbox, const math::Vec3& color, bool through_walls,
                debug::Shapes& shapes) {
  for (const ballistics::Triangle& triangle : hitbox.triangles) {
    for (const auto& [from, to] :
         {std::pair{triangle.a, triangle.b}, std::pair{triangle.b, triangle.c}, std::pair{triangle.c, triangle.a}}) {
      shapes.segments.push_back(
          debug::Segment{.from = from, .to = to, .color = color, .alpha = 1.0F, .through_walls = through_walls});
    }
  }
}

}  // namespace augusta::presentation
