-- The simulation's Parameters (ADR-0039): the tunable values that client and
-- server must agree on. Only the server reads this file; each client is sent
-- the result when it joins, and again after each reload. The tick rate is not
-- here: it is fixed while the server runs, so it is `tick_rate_hz` in
-- augustad.yaml. Copy it next to augustad.yaml and name it there with
-- the `parameters` key.
--
-- A value may be an expression of other values in this script. The script runs
-- once per load, in a sandbox with no io, os or randomness, and every key must
-- be spelled exactly: an unknown or missing key stops the server loading it.

-- Stamina (US-05): a full bar lasts 5 seconds of sprinting and refills in 10
-- of rest.
local sprint_seconds = 5
local rest_seconds = 10

return {
  stamina = {
    -- Fraction of stamina sprinting costs per second (0 or more).
    deplete_per_second = 1 / sprint_seconds,
    -- Fraction of stamina regained per second while not sprinting (0 or more).
    regen_per_second = 1 / rest_seconds,
    -- At or below this fraction of stamina a player is forced to walk
    -- (0 or more, and below 1).
    forced_walk_below = 0.1,
  },
}
