#include <cstddef>
#include <cstdint>
#include <functional>
#include <numbers>
#include <span>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "augusta/animation.h"
#include "augusta/assets.h"
#include "augusta/command.h"
#include "augusta/hitbox.h"
#include "augusta/interpolation.h"
#include "augusta/link_messages.h"
#include "augusta/link_types.h"
#include "augusta/math.h"
#include "augusta/parameters.h"
#include "augusta/physics.h"
#include "augusta/pose.h"
#include "augusta/protocol.h"
#include "augusta/simulation.h"
#include "augusta/tick.h"
#include "augusta/weapon.h"
#include "clips_fixture.h"

// Client and server agree on the pose (ADR-0061): a SimulationWorld poses two
// players from their Clips, its Authoritative State goes over the wire to a
// client's RemoteInterpolator, and the Animation module poses each player again
// from what the interpolator shows, at each tick and between two. The server's
// pose between two ticks is the one its Hitbox history judges a Shot against
// (ADR-0044): each Joint relative to its parent interpolated between the two
// ticks' poses, composed onto the root interpolated apart.
namespace {

namespace fixture = augusta::clips_fixture;

using augusta::animation::JointPose;
using augusta::animation::Poser;
using augusta::animation::SkeletonPose;
using augusta::command::Command;
using augusta::math::Mat4;
using augusta::math::Vec3;
using augusta::physics::Stance;
using augusta::presentation::RemoteInterpolator;
using augusta::presentation::RemotePlayer;
using augusta::simulation::Character;
using augusta::simulation::EntityId;
using augusta::simulation::EntityState;

constexpr std::uint8_t kTickRate = 60;
constexpr float kTick = 1.0F / kTickRate;

// How far a client's pose may be from the server's, in each entry of each
// Joint's world transform: a Joint's position in meters on each axis, and its
// axes turned by about that many radians. On a tick, the client poses the state
// the server posed, rounded to the wire's grids. Between two ticks, it poses
// the state interpolated where the server interpolates the two ticks' Joints:
// where a Clip turns a Joint back, as the Walker's legs turn back at each Foot
// contact, the server's Joints cut the corner the client's pose turns, by up to
// 2.6 cm at a sprint.
constexpr float kOnATick = 0.001F;
constexpr float kBetweenTicks = 0.03F;

// The Walker, whose legs swing by the Stride and who has an air Clip, and the
// Holder, whose Aim offset and ADS grip bend and raise it and whose left hand
// reaches the rifle by IK (clips_fixture.h).
constexpr EntityId kWalker{1};
constexpr EntityId kHolder{2};

Character Walker() {
  return Character{.eye = Vec3(0.0F, 1.7F, 0.0F),
                   .hitboxes = {},
                   .skeleton = fixture::WalkerSkeleton(),
                   .clips = fixture::WalkerClips()};
}

Character Holder() {
  return Character{.eye = Vec3(0.0F, 1.7F, 0.0F),
                   .hitboxes = {},
                   .skeleton = fixture::HolderSkeleton(),
                   .clips = fixture::HolderClips()};
}

augusta::physics::CollisionMesh Floor() {
  constexpr float kExtent = 100.0F;
  return {.points = {Vec3(-kExtent, 0.0F, -kExtent), Vec3(-kExtent, 0.0F, kExtent), Vec3(kExtent, 0.0F, kExtent),
                     Vec3(kExtent, 0.0F, -kExtent)},
          .indices = {0, 1, 2, 0, 2, 3}};
}

// What the two players do on the tick at index.
using Script = std::function<std::pair<Command, Command>(int index)>;

// A body's pose state, interpolated, against the server's: how often each
// input was seen to vary while compared.
struct Covered {
  int ticks = 0;
  int between = 0;
  int airborne = 0;
  int changing_stance = 0;
  int ads = 0;
  int pitched = 0;
  int strides = 0;
};

class PoseAgreementTest : public ::testing::Test {
 protected:
  PoseAgreementTest() : world_(augusta::parameters::Parameters{}, kTickRate, {}, fixture::HolderRifle()) {
    EXPECT_TRUE(world_.AddCollisionMesh(Floor()).has_value());
  }

  // Runs script for ticks ticks, the Walker dropped from 1.5 m and the Holder
  // on the floor, and compares every pose: on each tick and at fractions
  // between it and the one before.
  Covered Run(int ticks, const Script& script) {
    world_.AddPlayer(kWalker, Vec3(0.0F, 1.5F, 0.0F), walker_);
    world_.AddPlayer(kHolder, Vec3(5.0F, 0.5F, 0.0F), holder_);
    Covered covered;
    // Each body of the tick before, with the server's pose of it.
    std::vector<std::pair<EntityState, SkeletonPose>> last;
    for (int index = 0; index < ticks; ++index) {
      const auto [walker, holder] = script(index);
      const augusta::simulation::State state =
          world_.Tick({{.entity = kWalker, .command = walker}, {.entity = kHolder, .command = holder}}, kTick).state;
      Receive(state);
      std::vector<std::pair<EntityState, SkeletonPose>> posed;
      for (const EntityState& body : state.bodies) {
        const SkeletonPose server = ServerPose(body);
        Compare(state.tick, body.entity, 0.0F, server, kOnATick);
        covered.ticks++;
        covered.airborne += body.body.grounded ? 0 : 1;
        covered.changing_stance += body.body.stance_progress < augusta::physics::kStanceProgressSteps ? 1 : 0;
        covered.ads += body.ads ? 1 : 0;
        covered.pitched += body.pitch != 0.0F ? 1 : 0;
        for (const auto& [before, earlier] : last) {
          if (before.entity != body.entity) {
            continue;
          }
          covered.strides += before.body.stride != body.body.stride ? 1 : 0;
          if (FlipsADiscreteState(before, body)) {
            continue;
          }
          for (const float fraction : {0.25F, 0.5F, 0.75F}) {
            Compare(state.tick - 1, body.entity, fraction,
                    Between(CharacterOf(body.entity).skeleton, earlier, server, fraction), kBetweenTicks);
            covered.between++;
          }
        }
        posed.emplace_back(body, server);
      }
      last = std::move(posed);
    }
    return covered;
  }

 private:
  const Character& CharacterOf(EntityId entity) const { return entity == kWalker ? walker_ : holder_; }

  // Poses character from input as Posing does: its Clips, then its rifle held.
  SkeletonPose Pose(EntityId entity, const augusta::animation::PoseInput& input) {
    const Character& character = CharacterOf(entity);
    SkeletonPose posed;
    poser_.Pose(*character.clips, character.skeleton, input, posed);
    if (character.clips->weapon_socket) {
      augusta::animation::HoldRifle(character.skeleton, *character.clips->weapon_socket, fixture::HolderRifle(), posed);
    }
    return posed;
  }

  // The server's pose of body, of the World's last tick, which its Posing
  // phase posed: the Holder's rifle where the World says Posing put it shows
  // it is that one.
  SkeletonPose ServerPose(const EntityState& body) {
    SkeletonPose posed = Pose(body.entity, {.body = body.body, .yaw = body.yaw, .pitch = body.pitch, .ads = body.ads});
    if (body.entity == kHolder) {
      const auto rifle = world_.HeldRifleOf(kHolder);
      EXPECT_TRUE(rifle.has_value());
      if (rifle) {
        const Mat4 hand = posed.world[fixture::kHolderRightHand] * holder_.clips->weapon_socket->frame;
        ExpectNear(augusta::weapon::PlaceMuzzle(hand, fixture::HolderRifle()), rifle->muzzle, 1e-5F, "muzzle");
      }
    }
    return posed;
  }

  // The server's pose between earlier and later, fraction of the way: as its
  // Hitbox history interpolates them (ADR-0044).
  static SkeletonPose Between(const augusta::assets::Skeleton& skeleton, const SkeletonPose& earlier,
                              const SkeletonPose& later, float fraction) {
    SkeletonPose between;
    between.root = {.position = augusta::math::Lerp(earlier.root.position, later.root.position, fraction),
                    .yaw = augusta::math::LerpAngle(earlier.root.yaw, later.root.yaw, fraction),
                    .stance = later.root.stance};
    const Mat4 root = augusta::hitbox::RootOf(between.root);
    for (std::size_t joint = 0; joint < skeleton.joints.size(); ++joint) {
      const JointPose local{
          .translation =
              augusta::math::Lerp(earlier.local[joint].translation, later.local[joint].translation, fraction),
          .rotation = augusta::math::Slerp(earlier.local[joint].rotation, later.local[joint].rotation, fraction)};
      const std::uint32_t parent = skeleton.joints[joint].parent;
      between.local.push_back(local);
      between.world.push_back(
          augusta::animation::Compose(parent == augusta::assets::kJointNoParent ? root : between.world[parent], local,
                                      skeleton.joints[joint].scale));
    }
    return between;
  }

  // Whether a body takes the nearer of two ticks between them, not something
  // in between: its ADS, whether it is on the ground or its change of stance
  // changed.
  static bool FlipsADiscreteState(const EntityState& earlier, const EntityState& later) {
    return earlier.ads != later.ads || earlier.body.grounded != later.body.grounded ||
           earlier.body.stance != later.body.stance || earlier.body.previous_stance != later.body.previous_stance;
  }

  // state, as a client receives it: encoded, decoded, and recorded by its
  // interpolator at its time on the server's timeline.
  void Receive(const augusta::simulation::State& state) {
    augusta::link::AuthoritativeState sent;
    sent.tick = state.tick;
    for (const EntityState& body : state.bodies) {
      sent.bodies.push_back(augusta::link::EntityBody{.entity = static_cast<augusta::link::EntityId>(body.entity),
                                                      .body = body.body,
                                                      .yaw = body.yaw,
                                                      .pitch = body.pitch,
                                                      .ads = body.ads});
    }
    const auto bytes = augusta::protocol::Encode(augusta::link::ToWire(sent));
    ASSERT_TRUE(bytes.has_value());
    const auto decoded = augusta::protocol::Decode(*bytes);
    ASSERT_TRUE(decoded.has_value());
    const augusta::link::AuthoritativeState received =
        augusta::link::FromWire(std::get<augusta::protocol::AuthoritativeStateWire>(*decoded));
    for (const augusta::link::EntityBody& body : received.bodies) {
      interpolator_.Record(static_cast<augusta::presentation::EntityId>(body.entity),
                           static_cast<double>(received.tick) * kTick, body.body,
                           {.yaw = body.yaw, .pitch = body.pitch, .ads = body.ads});
    }
  }

  // Expects the client's pose of entity fraction of the way past tick to agree
  // with server, the server's, within tolerance.
  void Compare(augusta::tick::Tick tick, EntityId entity, float fraction, const SkeletonPose& server, float tolerance) {
    const double time = (static_cast<double>(tick) + fraction) * kTick;
    for (const RemotePlayer& player : interpolator_.Sample(time)) {
      if (static_cast<EntityId>(player.entity) != entity) {
        continue;
      }
      const SkeletonPose client = Pose(entity, augusta::presentation::PoseInputOf(player.body));
      ASSERT_EQ(client.world.size(), server.world.size());
      for (std::size_t joint = 0; joint < client.world.size(); ++joint) {
        ::testing::ScopedTrace trace(__FILE__, __LINE__,
                                     ::testing::Message() << "entity " << static_cast<int>(entity) << ", tick " << tick
                                                          << " + " << fraction << ", joint " << joint);
        ExpectNear(client.world[joint], server.world[joint], tolerance, "joint");
      }
    }
  }

  static void ExpectNear(const Mat4& actual, const Mat4& expected, float tolerance, const char* what) {
    for (int column = 0; column < 4; ++column) {
      for (int row = 0; row < 4; ++row) {
        EXPECT_NEAR(actual[column][row], expected[column][row], tolerance)
            << what << " [" << column << "][" << row << "]";
      }
    }
  }

  Character walker_ = Walker();
  Character holder_ = Holder();
  augusta::simulation::World world_;
  RemoteInterpolator interpolator_;
  Poser poser_;
};

Command Moving(const Vec3& direction, float yaw, Stance stance = Stance::kStanding, bool sprint = false) {
  Command command;
  command.movement.direction = direction;
  command.movement.desired_stance = stance;
  command.movement.sprint = sprint;
  command.yaw = yaw;
  return command;
}

// The Walker lands, walks ahead and aslant while it turns, sprints, crouches
// and goes prone on the move; the Holder sweeps its view from low to high,
// raises its rifle to ADS, crouches while aiming and walks. Every input the
// pose uses varies, and on every tick and between every two, the client's
// pose of each is the server's within kOnATick and kBetweenTicks.
// Requirements: US-11, NFR-02
TEST_F(PoseAgreementTest, AClientPosesFromInterpolatedStateTheBodyTheServerPosed) {
  constexpr int kTicks = 240;
  const Covered covered = Run(kTicks, [](int index) {
    const float turning = 0.01F * static_cast<float>(index);
    Command walker;
    if (index >= 60 && index < 100) {
      walker = Moving(Vec3(0.0F, 0.0F, -1.0F), turning);
    } else if (index >= 100 && index < 130) {
      walker = Moving(Vec3(1.0F, 0.0F, -1.0F), turning);
    } else if (index >= 130 && index < 160) {
      walker = Moving(Vec3(0.0F, 0.0F, -1.0F), turning, Stance::kStanding, true);
    } else if (index >= 160 && index < 200) {
      walker = Moving(Vec3(-1.0F, 0.0F, 0.0F), turning, Stance::kCrouching);
    } else if (index >= 200) {
      walker = Moving(Vec3(0.0F, 0.0F, 1.0F), turning, Stance::kProne);
    }
    Command holder;
    holder.pitch = -1.2F + (2.4F * static_cast<float>(index) / kTicks);
    holder.ads = index >= 80;
    if (index >= 120) {
      holder.movement.desired_stance = Stance::kCrouching;
    }
    if (index >= 180) {
      holder.movement.desired_stance = Stance::kStanding;
      holder.movement.direction = Vec3(0.0F, 0.0F, -1.0F);
      holder.yaw = std::numbers::pi_v<float> / 4.0F;
    }
    return std::pair{walker, holder};
  });

  EXPECT_EQ(covered.ticks, 2 * kTicks);
  EXPECT_GT(covered.between, 3 * kTicks);
  EXPECT_GT(covered.airborne, 0);
  EXPECT_GT(covered.changing_stance, 0);
  EXPECT_GT(covered.ads, 0);
  EXPECT_GT(covered.pitched, 0);
  EXPECT_GT(covered.strides, 0);
}

}  // namespace
