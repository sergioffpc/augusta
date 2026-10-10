"""Shared fixtures: the example scenario's packs, as the C++ tests load them."""

from pathlib import Path

import augusta_agent
import pytest

EXAMPLE_PACKS = (
    Path(__file__).parents[3] / "tests" / "fixtures" / "example-packs"
)
CHARACTER = "soldier"


@pytest.fixture(scope="session")
def client_pack() -> augusta_agent.Pack:
    return augusta_agent.load_pack(
        EXAMPLE_PACKS / "client.pack", EXAMPLE_PACKS / "test.pub"
    )
