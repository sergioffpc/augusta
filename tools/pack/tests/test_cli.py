"""augusta-pack end to end on the example scenario.

Runs usd-optimize, validation and the cook into a signed client/server pack
pair.
"""

from augusta_pack import _native
from augusta_pack import cli
from augusta_pack import keys
from conftest import EXAMPLES_ROOT
from conftest import read_pack_contents


def test_the_example_scenario_cooks_into_a_signed_pack_pair(tmp_path):
    pub_path, key_path = keys.write_keypair(tmp_path / "test")
    client_path = tmp_path / "client.apak"
    server_path = tmp_path / "server.apak"

    status = cli.main(
        [
            "firebase",
            "--assets-root",
            str(EXAMPLES_ROOT),
            "--signing-key",
            str(key_path),
            "--client-output-pack",
            str(client_path),
            "--server-output-pack",
            str(server_path),
        ]
    )

    assert status == 0
    public_key = keys.read_public_key(pub_path)
    client = read_pack_contents(client_path, public_key)
    server = read_pack_contents(server_path, public_key)
    assert client.paths_of_type(_native.AssetType.EYE) == {
        "soldier/Character/Eye"
    }
    assert server.paths_of_type(_native.AssetType.EYE) == {
        "soldier/Character/Eye"
    }
    hitboxes = {
        f"soldier/Character/{name}Hitbox"
        for name in (
            "Head",
            "Torso",
            "LeftArm",
            "RightArm",
            "LeftLeg",
            "RightLeg",
        )
    }
    assert client.paths_of_type(_native.AssetType.HITBOX) == hitboxes
    assert server.paths_of_type(_native.AssetType.HITBOX) == hitboxes
    assert server.client_pack_hash == client.hash
    assert server.paths_of_type(_native.AssetType.SCRIPT) == {
        "parameters.lua",
        "rules.lua",
    }
    assert client.paths_of_type(_native.AssetType.SCRIPT) == set()
    cues = (
        "gunshot",
        "hit_marker",
        "hit_taken",
        "death",
        "match_won",
        "match_lost",
    )
    assert client.paths_of_type(_native.AssetType.AUDIO) == {
        f"sounds/{cue}" for cue in cues
    }
    assert server.paths_of_type(_native.AssetType.AUDIO) == set()


def test_a_missing_scenario_fails_without_writing_a_pack(tmp_path, capsys):
    _, key_path = keys.write_keypair(tmp_path / "test")

    status = cli.main(
        [
            "missing",
            "--assets-root",
            str(EXAMPLES_ROOT),
            "--signing-key",
            str(key_path),
            "--client-output-pack",
            str(tmp_path / "client.apak"),
        ]
    )

    assert status == 1
    assert "Scenario not found" in capsys.readouterr().err
    assert not (tmp_path / "client.apak").exists()
