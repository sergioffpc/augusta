"""load_test.py against the in-process server, run as its command line is."""

import importlib.util
from pathlib import Path

from augusta_agent._testing import Server
from conftest import EXAMPLE_PACKS
from conftest import serving

_SPEC = importlib.util.spec_from_file_location(
    "load_test", Path(__file__).parents[1] / "examples" / "load_test.py"
)
load_test = importlib.util.module_from_spec(_SPEC)
_SPEC.loader.exec_module(load_test)


def run(server: Server, *options: str) -> int:
    return load_test.main(
        [
            "--server",
            server.address,
            "--pack",
            str(EXAMPLE_PACKS / "client.apak"),
            "--public-key",
            str(EXAMPLE_PACKS / "test.pub"),
            *options,
        ]
    )


def test_exits_0_once_every_agent_has_seen_the_match_ends(capsys):
    with serving(player_count=2) as server:
        assert run(server, "--matches", "2", "--timeout", "60") == 0

    lines = capsys.readouterr().out.splitlines()
    assert [line.split()[0] for line in lines] == ["agent=0", "agent=1"]
    assert all("failure=-" in line for line in lines)


def test_exits_1_when_the_timeout_passes_first(capsys):
    # One player alone never ends a Match.
    with serving(player_count=1) as server:
        assert run(server, "--timeout", "2") == 1

    assert "timed out" in capsys.readouterr().err


def test_plays_a_match_through_an_impaired_link():
    # The server is in this process too: its packets are impaired as well.
    with serving(player_count=2) as server:
        code = run(
            server,
            "--timeout",
            "60",
            "--latency-ms",
            "30",
            "--jitter-mean-ms",
            "5",
            "--jitter-max-ms",
            "20",
            "--loss-percent",
            "1",
            "--reorder-percent",
            "1",
            "--reorder-delay-ms",
            "10",
        )

    assert code == 0


def test_exits_1_when_an_agent_is_refused(capsys):
    with serving(player_count=2) as server:
        assert run(server, "--character", "nobody", "--timeout", "30") == 1

    assert "no such character" in capsys.readouterr().err
