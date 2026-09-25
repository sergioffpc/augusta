#ifndef AUGUSTA_COMMAND_H_
#define AUGUSTA_COMMAND_H_

#include "augusta/physics.h"

// augusta::command is the Command (CONTEXT.md): one tick's player intent, in
// the shared core (ARCHITECTURE.md §5). The client's input sampler
// (augusta::input::Input) builds one per tick, PredictionWorld applies it and
// the harness sends it; the server gets it back off the wire, screens and
// queues it, and SimulationWorld's CommandIngestion phase consumes it. None of
// those but the client's sampler links the device-facing input module.
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
  // short of straight up/down (no gimbal flip). input::ViewRotation turns the
  // pair into a rotation. Determines aim direction for WeaponHandling (bullet
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
};

}  // namespace augusta::command

#endif  // AUGUSTA_COMMAND_H_
