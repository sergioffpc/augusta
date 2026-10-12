"""Agents against the in-process server, driven as a script drives them."""

import asyncio
from collections.abc import Callable
import math
import sys
import time

import augusta_agent
from conftest import CHARACTER
from conftest import EXAMPLE_PACKS
from conftest import serving
import pytest

TIMEOUT_S = 30.0
# Where a standing body's eye and torso are above its feet, near enough to aim
# by at the example Map's ranges.
EYE_M = 1.35
TORSO_M = 0.9
# A Burst, then a release long enough for the Recoil to come back down.
BURST_S = 0.1
RELEASE_S = 0.3
BURSTS_PER_RELOAD = 4
# How often a script that polls a view looks again.
POLL_S = 0.01


async def wait_until(
    condition: Callable[[], bool], agents: list[augusta_agent.Agent]
) -> None:
    async with asyncio.timeout(TIMEOUT_S):
        while not condition():
            for agent in agents:
                assert agent.failure is None, agent.failure
            await asyncio.sleep(POLL_S)


async def next_event_of_type(agent: augusta_agent.Agent, kind: type):
    async with asyncio.timeout(TIMEOUT_S):
        while not isinstance(event := await agent.next_event(), kind):
            pass
        return event


def aim_at(view: augusta_agent.View, target: int) -> augusta_agent.Command:
    """Fire held at target's torso, from view's own eye.

    Command's yaw 0 looks down -Z and grows to the left; its pitch grows up.
    """
    eye = view.own.position
    body = next(b.body for b in view.authoritative.bodies if b.entity == target)
    dx, dz = body.position[0] - eye[0], body.position[2] - eye[2]
    dy = (body.position[1] + TORSO_M) - (eye[1] + EYE_M)
    return augusta_agent.Command(
        yaw=math.atan2(-dx, -dz),
        pitch=math.atan2(dy, math.hypot(dx, dz)),
        fire=True,
    )


async def shoot_until_the_match_ends(agent: augusta_agent.Agent) -> list:
    """Every event agent sees up to its Match end, firing meanwhile.

    It fires in Bursts at the other player, reloading now and then.
    """
    seen = [await next_event_of_type(agent, augusta_agent.MatchStart)]
    own = agent.view().own_entity
    target = next(p.entity for p in seen[0].players if p.entity != own)

    async def shoot() -> None:
        bursts = 0
        while True:
            view = agent.view()
            if view.authoritative is not None:
                agent.set_raw(aim_at(view, target))
                await asyncio.sleep(BURST_S)
                bursts += 1
            agent.set_raw(
                augusta_agent.Command(reload=bursts % BURSTS_PER_RELOAD == 0)
            )
            await asyncio.sleep(RELEASE_S)

    shooting = asyncio.create_task(shoot())
    try:
        async with asyncio.timeout(TIMEOUT_S):
            while not isinstance(seen[-1], augusta_agent.MatchEnd):
                seen.append(await agent.next_event())
    finally:
        shooting.cancel()
    return seen


def test_a_bad_key_raises_with_the_reason():
    with pytest.raises(augusta_agent.PackError, match="test.key"):
        augusta_agent.load_pack(
            EXAMPLE_PACKS / "client.pack", EXAMPLE_PACKS / "test.key"
        )


def test_a_missing_pack_raises_with_the_reason():
    with pytest.raises(augusta_agent.PackError, match="missing.pack"):
        augusta_agent.load_pack(
            EXAMPLE_PACKS / "missing.pack", EXAMPLE_PACKS / "test.pub"
        )


def test_two_agents_are_admitted_and_reach_the_match(client_pack):
    async def scenario():
        with (
            serving(player_count=2) as server,
            augusta_agent.Agent(
                server.address, client_pack, CHARACTER
            ) as first,
            augusta_agent.Agent(
                server.address, client_pack, CHARACTER
            ) as second,
        ):
            agents = [first, second]
            for agent in agents:
                await next_event_of_type(agent, augusta_agent.MatchStart)

            sessions = {agent.view().session for agent in agents}
            for agent in agents:
                view = agent.view()
                assert view.phase == augusta_agent.Phase.MATCH
                assert {p.session for p in view.match_players} == sessions
                assert view.own_entity in {
                    player.entity for player in view.match_players
                }

    asyncio.run(scenario())


def test_two_agents_see_their_match_start_its_deaths_and_its_end_in_order(
    client_pack,
):
    async def scenario():
        with (
            serving(player_count=2) as server,
            augusta_agent.Agent(
                server.address, client_pack, CHARACTER
            ) as first,
            augusta_agent.Agent(
                server.address, client_pack, CHARACTER
            ) as second,
        ):
            seen = await asyncio.gather(
                shoot_until_the_match_ends(first),
                shoot_until_the_match_ends(second),
            )

        for events in seen:
            kinds = [
                type(event)
                for event in events
                if not isinstance(
                    event, augusta_agent.Shot | augusta_agent.HitConfirmation
                )
            ]
            # Two players: the first Death ends the Match, unless both die
            # on one tick, a draw.
            assert kinds[0] == augusta_agent.MatchStart
            assert kinds[-1] == augusta_agent.MatchEnd
            assert set(kinds[1:-1]) == {augusta_agent.Death}
        victims = [
            {e.victim for e in events if isinstance(e, augusta_agent.Death)}
            for events in seen
        ]
        assert victims[0] == victims[1]
        assert any(isinstance(e, augusta_agent.Shot) for e in seen[0])

    asyncio.run(scenario())


def test_no_match_starts_until_an_agent_without_auto_ready_is_ready(
    client_pack,
):
    async def scenario():
        with (
            serving(player_count=2) as server,
            augusta_agent.Agent(
                server.address, client_pack, CHARACTER
            ) as eager,
            augusta_agent.Agent(
                server.address, client_pack, CHARACTER, auto_ready=False
            ) as late,
        ):
            agents = [eager, late]
            await wait_until(
                lambda: all(
                    (lobby := agent.view().lobby) is not None
                    and len(lobby.roster) == 2
                    for agent in agents
                ),
                agents,
            )
            # Long enough for a Match to start, were everyone Ready.
            await asyncio.sleep(1.0)
            assert all(
                agent.view().phase == augusta_agent.Phase.LOBBY
                for agent in agents
            )

            late.ready()

            for agent in agents:
                await next_event_of_type(agent, augusta_agent.MatchStart)

    asyncio.run(scenario())


def test_a_raw_command_moves_the_agent(client_pack):
    async def scenario():
        with (
            serving(player_count=1) as server,
            augusta_agent.Agent(
                server.address, client_pack, CHARACTER
            ) as agent,
        ):
            agent.set_raw(augusta_agent.Command(move=(0.0, 0.0, -1.0)))
            start = await next_event_of_type(agent, augusta_agent.MatchStart)
            own = agent.view().own_entity
            spawn = next(p.spawn for p in start.players if p.entity == own)

            await wait_until(
                lambda: agent.view().own.position[2] < spawn[2] - 1.0, [agent]
            )

    asyncio.run(scenario())


def test_a_script_that_blocks_does_not_stop_its_agent_sending_commands(
    client_pack,
):
    blocked_s = 2.0

    async def scenario():
        with (
            serving(player_count=1) as server,
            augusta_agent.Agent(
                server.address, client_pack, CHARACTER
            ) as agent,
        ):
            await next_event_of_type(agent, augusta_agent.MatchStart)
            await wait_until(
                lambda: agent.view().authoritative is not None, [agent]
            )
            before = agent.view().authoritative

            # Holds the loop, and the GIL: no other thread is switched in.
            switch_interval = sys.getswitchinterval()
            sys.setswitchinterval(blocked_s * 10)
            try:
                deadline = time.monotonic() + blocked_s
                while time.monotonic() < deadline:
                    pass
            finally:
                sys.setswitchinterval(switch_interval)

            after = agent.view().authoritative
            ticks = after.tick - before.tick
            commands = (
                after.acknowledged_sequence - before.acknowledged_sequence
            )
            assert ticks > 0
            # One Command a tick, give or take the few in flight.
            assert commands >= ticks * 0.9, (commands, ticks)

    asyncio.run(scenario())


def test_a_raw_intent_ends_replaced_when_another_is_set(client_pack):
    async def scenario():
        with (
            serving(player_count=1) as server,
            augusta_agent.Agent(
                server.address, client_pack, CHARACTER
            ) as agent,
        ):
            walking = agent.set_raw(
                augusta_agent.Command(move=(0.0, 0.0, -1.0))
            )
            agent.set_raw(augusta_agent.Command())

            async with asyncio.timeout(TIMEOUT_S):
                assert await walking == augusta_agent.IntentEnding.REPLACED

    asyncio.run(scenario())


def test_a_move_to_ends_arrived_at_its_point(client_pack):
    async def scenario():
        with (
            serving(player_count=1) as server,
            augusta_agent.Agent(
                server.address, client_pack, CHARACTER
            ) as agent,
        ):
            start = await next_event_of_type(agent, augusta_agent.MatchStart)
            own = agent.view().own_entity
            spawn = next(p.spawn for p in start.players if p.entity == own)
            point = (spawn[0], spawn[1], spawn[2] - 2.0)

            moving = agent.set_movement(augusta_agent.MoveTo(point))

            async with asyncio.timeout(TIMEOUT_S):
                assert await moving == augusta_agent.IntentEnding.ARRIVED
            position = agent.view().own.position
            assert (
                math.hypot(position[0] - point[0], position[2] - point[2]) < 1.0
            )

    asyncio.run(scenario())


def test_an_aim_at_ends_target_gone_once_its_target_dies(client_pack):
    async def scenario():
        with (
            serving(player_count=2) as server,
            augusta_agent.Agent(
                server.address, client_pack, CHARACTER
            ) as shooter,
            augusta_agent.Agent(
                server.address, client_pack, CHARACTER
            ) as target,
        ):
            start = await next_event_of_type(shooter, augusta_agent.MatchStart)
            await next_event_of_type(target, augusta_agent.MatchStart)
            victim = target.view().own_entity

            aiming = shooter.set_aim(augusta_agent.AimAt(victim))
            shooter.set_trigger(augusta_agent.FireBursts())

            async with asyncio.timeout(TIMEOUT_S):
                assert await aiming == augusta_agent.IntentEnding.TARGET_GONE
                death = await next_event_of_type(shooter, augusta_agent.Death)
            assert death.victim == victim
            assert victim in {p.entity for p in start.players}

    asyncio.run(scenario())


def test_clearing_a_channel_ends_its_intent_replaced_and_no_other(
    client_pack,
):
    async def scenario():
        with (
            serving(player_count=1) as server,
            augusta_agent.Agent(
                server.address, client_pack, CHARACTER
            ) as agent,
        ):
            moving = agent.set_movement(augusta_agent.Move((0.0, 0.0, -1.0)))
            looking = agent.set_aim(augusta_agent.Look(0.5, 0.0))
            firing = agent.set_trigger(augusta_agent.HoldFire())

            agent.clear_trigger()

            async with asyncio.timeout(TIMEOUT_S):
                assert await firing == augusta_agent.IntentEnding.REPLACED
            assert not moving.done()
            assert not looking.done()

    asyncio.run(scenario())


def test_an_agent_has_stats_after_a_match(client_pack):
    async def scenario():
        with (
            serving(player_count=2) as server,
            augusta_agent.Agent(
                server.address, client_pack, CHARACTER
            ) as first,
            augusta_agent.Agent(
                server.address, client_pack, CHARACTER
            ) as second,
        ):
            await asyncio.gather(
                shoot_until_the_match_ends(first),
                shoot_until_the_match_ends(second),
            )
            stats = [agent.stats() for agent in (first, second)]

        for agent_stats in stats:
            assert agent_stats.netcode.match_ticks > 0
            assert agent_stats.connection is not None
            assert agent_stats.connection.ping_ms >= 0
        assert sum(s.netcode.rounds_fired for s in stats) > 0
        assert sum(s.netcode.hit_confirmations for s in stats) > 0

    asyncio.run(scenario())


def test_an_unknown_character_fails_every_await_with_the_refusal(client_pack):
    async def scenario():
        with (
            serving(player_count=1) as server,
            augusta_agent.Agent(server.address, client_pack, "nobody") as agent,
        ):
            holding = agent.set_raw(augusta_agent.Command())

            async with asyncio.timeout(TIMEOUT_S):
                with pytest.raises(
                    augusta_agent.AgentFailed, match="no such character"
                ) as failed:
                    await agent.next_event()
                with pytest.raises(augusta_agent.AgentFailed):
                    await holding
                with pytest.raises(augusta_agent.AgentFailed):
                    await agent.set_raw(augusta_agent.Command())
                with pytest.raises(augusta_agent.AgentFailed):
                    await agent.set_trigger(augusta_agent.Reload())

            assert agent.failure == str(failed.value)

    asyncio.run(scenario())


def test_a_closed_agent_raises(client_pack):
    async def scenario():
        with serving(player_count=1) as server:
            agent = augusta_agent.Agent(server.address, client_pack, CHARACTER)
            agent.close()

            with pytest.raises(RuntimeError, match="closed"):
                agent.view()

    asyncio.run(scenario())
