"""Shared fixtures: the example scenario's packs, as the C++ tests load them."""

from pathlib import Path

import augusta_agent
from augusta_agent._testing import Server
import pytest

EXAMPLE_PACKS = (
    Path(__file__).parents[3] / "tests" / "fixtures" / "example-packs"
)
CHARACTER = "soldier"


def serving(player_count: int) -> Server:
    """The example scenario served in-process for player_count players."""
    return Server(
        EXAMPLE_PACKS / "server.apak",
        EXAMPLE_PACKS / "test.pub",
        player_count=player_count,
    )


@pytest.fixture(scope="session")
def client_pack() -> augusta_agent.Pack:
    return augusta_agent.load_pack(
        EXAMPLE_PACKS / "client.apak", EXAMPLE_PACKS / "test.pub"
    )
