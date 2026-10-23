"""tshark with the augusta shim over the protocol_decode seeds' pcapng.

The C++ pcapng encoder (src/modules/pcapng) wrote the fixture, one packet per
seed (tests/fuzz/README.md), so reading it here holds the writer, the shim and
the protocol's schema to one format (ADR-0076). Field values need no oracle:
all three share the one .proto.

Without tshark the tests skip, unless AUGUSTA_REQUIRE_TSHARK is set, as CI
sets it, so a missing tshark fails there.
"""

from collections.abc import Iterator
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
from typing import Any

import pytest

REPO_ROOT = Path(__file__).parents[3]
SHIM = Path(__file__).parents[1] / "augusta.lua"
# The protocol module's directory holding its one .proto, and nothing else.
SCHEMA_DIR = REPO_ROOT / "src" / "modules" / "protocol" / "proto"
FIXTURE = REPO_ROOT / "tests" / "fuzz" / "protocol_decode_seeds.pcapng"
SEEDS_DIR = REPO_ROOT / "tests" / "fuzz" / "corpus" / "protocol_decode"
REQUIRE_TSHARK = "AUGUSTA_REQUIRE_TSHARK"

# The fixture's nth packet is the seed whose name is nth in name order, and
# that name is the augusta.Message oneof member it holds.
SEEDS = sorted(path.name for path in SEEDS_DIR.iterdir())
# An engine version (augusta/version.h): SemVer, +dev in a Development build.
ENGINE_VERSION = r"\d+\.\d+\.\d+(?:-[0-9A-Za-z.-]+)?(?:\+[0-9A-Za-z.-]+)?"


def _wireshark_tool(name: str) -> str:
    """The path of one of Wireshark's command-line tools, or skips the test."""
    path = shutil.which(name)
    if path is None:
        if os.environ.get(REQUIRE_TSHARK):
            pytest.fail(f"{name} is not on PATH, and {REQUIRE_TSHARK} is set")
        pytest.skip(f"{name} is not on PATH")
    return path


@pytest.fixture(scope="module")
def wireshark_env(tmp_path_factory: pytest.TempPathFactory) -> dict[str, str]:
    """An environment whose Wireshark profile and plugins are empty.

    So the developer's own preferences, search paths or installed shim (a
    second Proto("augusta") is an error) never reach the run.
    """
    home = tmp_path_factory.mktemp("home")
    env = dict(os.environ)
    env.update(
        HOME=str(home),
        XDG_CONFIG_HOME=str(home / ".config"),
        APPDATA=str(home),
        WIRESHARK_CONFIG_DIR=str(home / "wireshark"),
    )
    return env


@pytest.fixture(scope="module")
def packets(wireshark_env: dict[str, str]) -> list[dict[str, Any]]:
    """Each packet's layers, as tshark -T json prints them."""
    tshark = _wireshark_tool("tshark")
    run = subprocess.run(
        [
            tshark,
            "-r",
            str(FIXTURE),
            "-X",
            f"lua_script:{SHIM}",
            "-o",
            f'uat:protobuf_search_paths:"{SCHEMA_DIR.as_posix()}","TRUE"',
            "-T",
            "json",
        ],
        capture_output=True,
        text=True,
        encoding="utf-8",
        env=wireshark_env,
        check=False,
    )
    assert run.returncode == 0, run.stderr
    # A Lua error loading the shim is printed, not fatal.
    assert not run.stderr, run.stderr
    return [packet["_source"]["layers"] for packet in json.loads(run.stdout)]


def _objects(node: Any) -> Iterator[dict[str, Any]]:
    """Every JSON object in node, node included.

    tshark puts a field's children in an object under the field's key, and
    sibling items sharing a key in one array.
    """
    if isinstance(node, dict):
        yield node
        for value in node.values():
            yield from _objects(value)
    elif isinstance(node, list):
        for value in node:
            yield from _objects(value)


def _envelope_members(layers: dict[str, Any]) -> list[str]:
    """The fields the packet's augusta.Message holds, by name, in order."""
    envelopes = [
        message
        for message in _objects(layers["protobuf"])
        if message.get("protobuf.message.name") == "augusta.Message"
    ]
    assert len(envelopes) == 1, envelopes
    # The envelope's own fields are its children; their fields lie deeper.
    return [
        child["protobuf.field.name"]
        for value in envelopes[0].values()
        for child in (value if isinstance(value, list) else [value])
        if isinstance(child, dict) and "protobuf.field.name" in child
    ]


def test_the_fixture_holds_one_packet_per_seed(packets):
    assert len(packets) == len(SEEDS)


@pytest.mark.parametrize(("number", "member"), list(enumerate(SEEDS)))
def test_each_packet_decodes_as_the_member_its_seed_names(
    packets, number, member
):
    layers = packets[number]

    assert "augusta" in layers
    assert "protobuf" in layers
    # One member and nothing after it: a byte the schema does not name would
    # be a field of its own, <UNKNOWN> or malformed.
    assert _envelope_members(layers) == [member]


@pytest.mark.parametrize("number", range(len(SEEDS)))
def test_no_packet_is_malformed_or_flagged(packets, number):
    keys = {key for item in _objects(packets[number]) for key in item}

    assert not {
        key
        for key in keys
        if key.startswith(("_ws.malformed", "_ws.expert", "_ws.short"))
    }


@pytest.mark.parametrize("number", range(len(SEEDS)))
def test_each_packet_shows_its_direction(packets, number):
    # The fixture alternates: outbound from packet 0, then inbound.
    expected = "Outbound" if number % 2 == 0 else "Inbound"

    assert packets[number]["augusta"]["augusta.direction"] == expected


def test_the_section_header_names_the_engine_version(wireshark_env):
    capinfos = _wireshark_tool("capinfos")
    run = subprocess.run(
        [capinfos, str(FIXTURE)],
        capture_output=True,
        text=True,
        encoding="utf-8",
        env=wireshark_env,
        check=True,
    )

    assert re.search(
        rf"^Capture application:\s+augustad {ENGINE_VERSION}$",
        run.stdout,
        re.MULTILINE,
    ), run.stdout
