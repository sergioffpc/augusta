-- Match objectives: this scenario's win condition (ADR-0022, US-14), last
-- player standing. The server calls on_tick in SimulationWorld's
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
