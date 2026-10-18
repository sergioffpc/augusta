"""The cook's determinism, end to end (ADR-0013, ADR-0030).

Cooking the example scenario with the committed test key gives, byte for byte,
the golden packs the C++ runtime's tests load (tests/cooked_pack_test.cpp): it
pins everything from the USD stage walk to the signed pack. A cook is
deterministic, so a difference is a bug in the cook unless the change that
caused it meant to change the format or the example.
"""

from augusta_pack import cli
from conftest import EXAMPLES_ROOT

GOLDEN_DIR = (
    EXAMPLES_ROOT.parent.parent.parent / "tests" / "fixtures" / "example-packs"
)


def test_the_example_scenario_cooks_into_the_golden_packs(tmp_path):
    client_path = tmp_path / "client.pack"
    server_path = tmp_path / "server.pack"

    status = cli.main(
        [
            "firebase",
            "--assets-root",
            str(EXAMPLES_ROOT),
            "--signing-key",
            str(GOLDEN_DIR / "test.key"),
            "--client-output-pack",
            str(client_path),
            "--server-output-pack",
            str(server_path),
        ]
    )

    assert status == 0
    assert client_path.read_bytes() == (GOLDEN_DIR / "client.pack").read_bytes()
    assert server_path.read_bytes() == (GOLDEN_DIR / "server.pack").read_bytes()
