-- Last player standing: a scenario's rules (ADR-0022), the Game policy that
-- decides how the mechanism is used - who spawns where, and when the Match is
-- won - as opposed to a tunable value (scripts/parameters/, ADR-0039) or
-- mechanism (C++). A scenario's manifest names it as scripts.rules (ADR-0041);
-- the cooker packs it into the server pack as rules.lua, and SimulationWorld
-- calls its hooks. A hook it does not define leaves that concern to the
-- mechanism.

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

-- The win condition (US-14), last player standing. Called in SimulationWorld's
-- Scripts/Behaviours phase, once a tick, after Damage has resolved the tick's
-- deaths (ADR-0023), with a read-only view of the Match:
--
--   match.tick          the tick
--   match.player_count  how many players the Match started with (ADR-0043)
--   match.players       every player still in it, by session: session, entity,
--                       character, alive, health, killed (on this tick) and,
--                       if so, killer (the killer's entity)
--
-- It decides by what it returns: nil to let the Match go on, {winner =
-- <session>} to end it with that player, alive, as the winner, or {draw =
-- true} to end it with no winner. A player who leaves is no longer in
-- match.players, so the last one left wins by the same rule.
function on_tick(match)
  local alive = {}
  for _, player in ipairs(match.players) do
    if player.alive then
      alive[#alive + 1] = player
    end
  end
  -- A Match of two or more ends when one is left; one started alone (for
  -- development) when its player dies.
  local last = match.player_count >= 2 and 1 or 0
  if #alive > last then
    return nil
  end
  if #alive == 1 then
    return { winner = alive[1].session }
  end
  return { draw = true }
end
