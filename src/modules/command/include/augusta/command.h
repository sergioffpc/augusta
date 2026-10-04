#ifndef AUGUSTA_COMMAND_H_
#define AUGUSTA_COMMAND_H_

#include <cstdint>

#include <glm/ext/quaternion_trigonometric.hpp>

#include "augusta/math.h"
#include "augusta/physics.h"
#include "augusta/tick.h"

// augusta::command is the Command (CONTEXT.md): one tick's player intent, in
// the shared core (ARCHITECTURE.md §5). The client's input sampler
// (augusta::input::Input) builds one per tick, ClientRuntime adds what its
// player was being shown (its Seen time, below), PredictionWorld applies it
// and the harness sends it; the server gets it back off the wire, screens and
// queues it, and SimulationWorld's CommandIngestion phase consumes it. None of
// those but the client's sampler links the device-facing input module.
//
// What a Command's view means is here with it: the client's camera and the
// server's Shots turn the same yaw and pitch into the same direction.
namespace augusta::command {

/// One tick's worth of player intent.
struct Command {
  // Movement direction, sprint, and desired stance for this tick -
  // passed straight through to physics::World::Step's MovementInput.
  physics::MovementInput movement;
  // View orientation for this tick, in radians, accumulated from mouse
  // movement. Yaw 0 looks down -Z, the renderer camera's forward, and a
  // positive yaw turns left (counter-clockwise seen from above, right-handed
  // about +Y); the client's sampler keeps it within one turn. A positive
  // pitch looks up; the sampler clamps it to input::kMaxLookPitch either way,
  // short of straight up/down (no gimbal flip). ViewRotation turns the pair
  // into a rotation. Determines aim direction for WeaponHandling (bullet
  // origin/direction, US-07) as well as view for Camera (US-06).
  float yaw = 0.0F;
  float pitch = 0.0F;
  // True while the aim-down-sights control is held (US-06). Hip-fire is
  // the default (false).
  bool ads = false;
  // True while the fire control is held (US-07). WeaponHandling, not
  // the sampler, turns a held fire control into discrete shots at the
  // weapon's fire rate - the sampler only reports raw intent.
  bool fire = false;
  // True on exactly the one tick the reload control was pressed
  // (US-08) - a rising edge, not a held state, regardless of how long
  // the control is actually held.
  bool reload = false;
  // The Seen time of the frame this command was sampled on: what the player
  // was shown of the other players (ADR-0044), the server tick of the Authoritative State update
  // being shown, and how far from it to the next one, 0 to 1. A round this
  // command fires is judged against the other players as they were then
  // (CONTEXT.md's Lag compensation). Only what the client says: the server
  // holds it within what it sent and within the Shooter's delay's cap.
  tick::Tick seen_tick = 0;
  float seen_fraction = 0.0F;
};

/// The rotation of a view with this yaw and pitch (see Command): yaw about +Y,
/// then pitch about the view's own +X. Applied to -Z, it gives where the view
/// looks.
[[nodiscard]] inline math::Quat ViewRotation(float yaw, float pitch) {
  return glm::angleAxis(yaw, math::Vec3(0.0F, 1.0F, 0.0F)) * glm::angleAxis(pitch, math::Vec3(1.0F, 0.0F, 0.0F));
}

/// The number a client gives each Command it sends (ADR-0038): from 1, one more
/// per command, over one connection, and the acknowledged sequence that answers
/// it. Wide enough never to wrap, as tick::Tick is, so every receiver orders
/// sequences as plain numbers; everything that holds one, the wire included,
/// takes its width from here.
using Sequence = std::uint64_t;

/// Where a view with this yaw and pitch looks, as a unit vector.
[[nodiscard]] inline math::Vec3 ViewDirection(float yaw, float pitch) {
  return ViewRotation(yaw, pitch) * math::Vec3(0.0F, 0.0F, -1.0F);
}

}  // namespace augusta::command

#endif  // AUGUSTA_COMMAND_H_
