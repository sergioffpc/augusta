"""The Python half of the pack format's contract (ADR-0013).

Cooking the example scenario with the committed test key gives, byte for byte,
the golden packs the C++ runtime's tests load (tests/cooked_pack_test.cpp). A
cook is deterministic, so any difference is a change to the format or to the
example, and the golden packs are regenerated with the command in README.md.
"""

from conftest import EXAMPLES_ROOT
from pack import cli

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
