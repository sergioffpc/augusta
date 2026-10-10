"""Agents against the in-process server, driven as a script drives them."""

from collections.abc import Callable
import time

import augusta_agent
from augusta_agent._testing import Server
from conftest import CHARACTER
from conftest import EXAMPLE_PACKS
import pytest

TIMEOUT_S = 30.0


def wait_until(
    condition: Callable[[], bool], agents: list[augusta_agent.Agent]
) -> None:
    deadline = time.monotonic() + TIMEOUT_S
    while not condition():
        for agent in agents:
            assert agent.failure is None, agent.failure
        assert time.monotonic() < deadline, "timed out"
        time.sleep(0.01)


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
    with (
        Server(
            EXAMPLE_PACKS / "server.pack",
            EXAMPLE_PACKS / "test.pub",
            player_count=2,
        ) as server,
        augusta_agent.Agent(server.address, client_pack, CHARACTER) as first,
        augusta_agent.Agent(server.address, client_pack, CHARACTER) as second,
    ):
        agents = [first, second]
        for agent in agents:
            agent.set_raw(augusta_agent.Command(move=(0.0, 0.0, -1.0)))

        wait_until(
            lambda: all(
                agent.view().phase == augusta_agent.Phase.MATCH
                for agent in agents
            ),
            agents,
        )

        sessions = {agent.view().session for agent in agents}
        for agent in agents:
            view = agent.view()
            assert {player.session for player in view.match_players} == sessions
            assert view.own_entity in {
                player.entity for player in view.match_players
            }


def test_a_raw_command_moves_the_agent(client_pack):
    with (
        Server(
            EXAMPLE_PACKS / "server.pack",
            EXAMPLE_PACKS / "test.pub",
            player_count=1,
        ) as server,
        augusta_agent.Agent(server.address, client_pack, CHARACTER) as agent,
    ):
        agent.set_raw(augusta_agent.Command(move=(0.0, 0.0, -1.0)))
        wait_until(
            lambda: agent.view().phase == augusta_agent.Phase.MATCH, [agent]
        )
        view = agent.view()
        spawn = next(
            player.spawn
            for player in view.match_players
            if player.entity == view.own_entity
        )

        wait_until(
            lambda: agent.view().own.position[2] < spawn[2] - 1.0, [agent]
        )


def test_a_closed_agent_raises(client_pack):
    with Server(
        EXAMPLE_PACKS / "server.pack",
        EXAMPLE_PACKS / "test.pub",
        player_count=1,
    ) as server:
        agent = augusta_agent.Agent(server.address, client_pack, CHARACTER)
        agent.close()

        with pytest.raises(RuntimeError, match="closed"):
            agent.view()
