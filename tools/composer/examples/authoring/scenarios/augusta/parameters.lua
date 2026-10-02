-- The simulation's Parameters (ADR-0039): the tunable values that client and
-- server must agree on. Copy this file into a scenario's folder, as
-- parameters.lua (authoring/scenarios/<scenario>/parameters.lua) next to its
-- manifest.yaml (ADR-0041): the cooker packs it into the scenario's server
-- pack, so it is signed with the composed map/characters and fixed for the
-- run. Only the server reads it; each client is sent the result when it
-- joins. The tick rate is not here: it is fixed while the server runs, so it
-- is `simulation.tick_rate_hz` in augustad.yaml.
--
-- A value may be an expression of other values in this script. The script runs
-- once per load, in a sandbox with no io, os or randomness, and every key must
-- be spelled exactly: an unknown or missing key stops the server loading it.

-- Stamina (US-05): a full bar lasts 5 seconds of sprinting and refills in 10
-- of rest.
local sprint_seconds = 5
local rest_seconds = 10

-- Recoil (US-09): each round of a burst kicks the aim up, a little more for
-- the first rounds, and alternately left and right.
local recoil_pattern = {}
for round = 1, 30 do
  recoil_pattern[round] = {
    pitch = math.rad(round <= 5 and 0.35 or 0.2),
    yaw = math.rad(round % 2 == 0 and 0.05 or -0.05),
  }
end

-- Health and damage: a head shot kills; torso shots take three, limb shots four.
local health = 100

return {
  -- How many players a match needs to start (ADR-0043): a whole number from 1
  -- to 8. 1 lets a single player run the example alone.
  player_count = 1,
  stamina = {
    -- Fraction of stamina sprinting costs per second (0 or more).
    deplete_per_second = 1 / sprint_seconds,
    -- Fraction of stamina regained per second while not sprinting (0 or more).
    regen_per_second = 1 / rest_seconds,
    -- A player who runs stamina out is forced to walk until it is back above
    -- this fraction (0 or more, and below 1).
    forced_walk_below = 0.1,
  },
  -- The rifle every player carries (US-06 to US-09). Angles are in radians.
  rifle = {
    -- Rounds a full magazine holds (a whole number, 1 to 255).
    magazine_capacity = 30,
    -- Rounds per minute while fire is held (above 0). At most one round fires
    -- a tick, so a rate above the tick rate fires at the tick rate.
    rounds_per_minute = 600,
    -- Speed a round leaves the muzzle at, in m/s (above 0).
    muzzle_velocity = 800,
    -- Seconds a reload takes (0 or more).
    reload_seconds = 2.5,
    -- Each round's kick to the aim, in order ({pitch, yaw}, a positive pitch up
    -- and a positive yaw left; at most 64); past the last, the last repeats.
    recoil_pattern = recoil_pattern,
    -- How fast the recoil a burst built up returns to zero while not firing, in
    -- radians per second (0 or more).
    recoil_recovery_per_second = math.rad(10),
    -- What aiming down sights scales each kick by (above 0, at most 1).
    ads_recoil_scale = 0.5,
    -- The vertical field of view aiming down sights zooms to (above 0, below pi).
    ads_field_of_view = math.rad(40),
  },
  -- The rifle's rounds (US-10, US-12).
  ammo = {
    -- Downward acceleration in flight, in m/s^2 (0 or more).
    gravity = 9.81,
    -- Meters a round flies before it is a miss (above 0).
    max_range = 1000,
    -- Damage by the body part hit (0 or more each).
    damage = {
      head = health,
      torso = health / 3 + 1,
      limb = health / 4,
    },
  },
  -- Health every player starts a match with (above 0); it never regenerates.
  starting_health = health,
}
