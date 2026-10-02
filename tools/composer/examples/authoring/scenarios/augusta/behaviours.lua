-- Other game policy for this scenario (ADR-0022): spawn rules and anything
-- else that decides what happens next from simulation state, as opposed to a
-- tunable value (parameters.lua, ADR-0039) or mechanism (C++). The server
-- loads it from the scenario's server pack and SimulationWorld calls its hooks.

-- Called once at Match start with a read-only view of the Match:
--   match.player_count  how many players are in it
--   match.spawn_points  how many Spawn points the Map has
--   match.players       every player, each {session, entity, character}
-- Returns, for every player once, {session = <its Session ID>, spawn_point =
-- <a Spawn point index from 1>}. Each player gets a Spawn point of its own, in
-- order, starting over only when the Map has fewer Spawn points than players.
function assign_spawns(match)
  local assignment = {}
  for i, player in ipairs(match.players) do
    assignment[i] = { session = player.session, spawn_point = (i - 1) % match.spawn_points + 1 }
  end
  return assignment
end
