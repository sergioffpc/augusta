"""cook_scenario on single stages: what each authored prim becomes in the
client and server packs (ADR-0019, ADR-0032), and how malformed geometry is
refused.
"""

import shutil
import struct

import pytest
from conftest import (
    FIXTURES_DIR,
    decode_audio,
    decode_hitbox,
    decode_mesh,
    decode_spawn_point,
    decode_string,
    read_pack_contents,
)

from pack import pack
from pack.cook import CookError, cook_scenario
from pack.sounds import CueSounds, Sound


def cook_stage(stage_name, tmp_path, key_pair):
    """Cooks one fixture stage as a map with no characters; returns (client, server) contents."""
    client_path = tmp_path / "client.pack"
    server_path = tmp_path / "server.pack"
    cook_scenario(FIXTURES_DIR / stage_name, [], client_path, server_path, key_pair.private_key)
    return (
        read_pack_contents(client_path, key_pair.public_key),
        read_pack_contents(server_path, key_pair.public_key),
    )


@pytest.mark.parametrize(
    ("stage_name", "code"),
    [
        ("bad_topology_fixture.usda", "unsupported_topology"),
        ("inconsistent_topology_fixture.usda", "inconsistent_topology"),
        ("negative_index_fixture.usda", "negative_index"),
        ("out_of_range_index_fixture.usda", "index_out_of_range"),
    ],
)
def test_malformed_geometry_is_refused_and_no_pack_is_written(stage_name, code, tmp_path, key_pair):
    with pytest.raises(CookError) as raised:
        cook_stage(stage_name, tmp_path, key_pair)

    assert raised.value.code == code
    assert raised.value.prim_path == "TestMesh"
    assert not (tmp_path / "client.pack").exists()
    assert not (tmp_path / "server.pack").exists()


def test_a_visual_mesh_goes_into_the_client_pack_only(tmp_path, key_pair):
    client, server = cook_stage("mesh_fixture.usda", tmp_path, key_pair)

    assert client.paths_of_type(pack.ASSET_TYPE_MESH) == {"TestMesh"}
    points, indices = decode_mesh(client.blob("TestMesh"))
    assert len(points) == 4
    assert len(indices) == 6
    assert server.paths_of_type(pack.ASSET_TYPE_MESH) == set()


def test_duplicate_vertices_are_welded(tmp_path, key_pair):
    client, _ = cook_stage("duplicate_triangles_fixture.usda", tmp_path, key_pair)

    points, _ = decode_mesh(client.blob("TestMesh"))
    assert len(points) == 4


def test_each_prim_kind_goes_to_the_packs_that_need_it(tmp_path, key_pair):
    client, server = cook_stage("client_server_split_fixture.usda", tmp_path, key_pair)

    assert client.paths_of_type(pack.ASSET_TYPE_MESH) == {"Root/Visual"}
    assert client.paths_of_type(pack.ASSET_TYPE_TEXTURE) == {"Root/Visual/Mat/DiffuseTexture"}
    for contents in (client, server):
        assert contents.paths_of_type(pack.ASSET_TYPE_COLLISION) == {"Root/Collider"}
        assert contents.paths_of_type(pack.ASSET_TYPE_HITBOX) == {"Root/Hitbox"}
        assert contents.paths_of_type(pack.ASSET_TYPE_SPAWN_POINT) == {"Root/Spawn"}
    assert server.paths_of_type(pack.ASSET_TYPE_MESH) == set()
    assert server.paths_of_type(pack.ASSET_TYPE_TEXTURE) == set()


def test_a_map_hitbox_carries_its_body_part(tmp_path, key_pair):
    client, server = cook_stage("client_server_split_fixture.usda", tmp_path, key_pair)

    for contents in (client, server):
        body_part, points, _ = decode_hitbox(contents.blob("Root/Hitbox"))
        assert body_part == pack.BODY_PART_TORSO
        assert len(points) == 4


def test_collision_geometry_is_kept_as_authored(tmp_path, key_pair):
    client, _ = cook_stage("client_server_split_fixture.usda", tmp_path, key_pair)

    points, indices = decode_mesh(client.blob("Root/Collider"))
    assert points == [(0, 0, 0), (2, 0, 0), (2, 2, 0), (0, 2, 0)]
    assert indices == [0, 1, 2, 0, 2, 3]


def test_the_server_pack_carries_the_hash_of_its_client_pack(tmp_path, key_pair):
    client, server = cook_stage("client_server_split_fixture.usda", tmp_path, key_pair)

    assert server.blob(pack.CLIENT_PACK_PATH) == client.hash


def test_a_spawn_point_carries_its_local_transform(tmp_path, key_pair):
    client, _ = cook_stage("scene_fixture.usda", tmp_path, key_pair)

    translation, rotation = decode_spawn_point(client.blob("Root/Spawn"))
    assert translation == (5, 0, 5)
    assert rotation == (0, 0, 0, 1)


def test_a_texture_is_compressed_to_bc7_dds_by_default(tmp_path, key_pair):
    client, _ = cook_stage("texture_fixture.usda", tmp_path, key_pair)

    blob = client.blob("TestMesh/Mat/DiffuseTexture")
    assert blob[0] == pack.TEXTURE_FORMAT_BC7
    assert blob[5:9] == b"DDS "


def test_a_texture_whose_image_is_missing_is_refused(tmp_path, key_pair):
    stage_dir = tmp_path / "stage"
    stage_dir.mkdir()
    shutil.copy(FIXTURES_DIR / "texture_fixture.usda", stage_dir)

    with pytest.raises(CookError) as raised:
        cook_scenario(
            stage_dir / "texture_fixture.usda",
            [],
            tmp_path / "client.pack",
            tmp_path / "server.pack",
            key_pair.private_key,
        )

    assert raised.value.code == "texture_load_failed"
    assert raised.value.prim_path == "TestMesh/Mat/DiffuseTexture"


def test_scripts_go_into_the_server_pack_only(tmp_path, key_pair):
    client_path = tmp_path / "client.pack"
    server_path = tmp_path / "server.pack"
    cook_scenario(
        FIXTURES_DIR / "mesh_fixture.usda",
        [],
        client_path,
        server_path,
        key_pair.private_key,
        scripts=[("parameters.lua", b"return {}")],
    )

    client = read_pack_contents(client_path, key_pair.public_key)
    server = read_pack_contents(server_path, key_pair.public_key)
    assert client.paths_of_type(pack.ASSET_TYPE_SCRIPT) == set()
    assert server.blob("parameters.lua") == b"return {}"


def test_cue_sounds_go_into_the_client_pack_only_addressed_under_their_folder(tmp_path, key_pair):
    gunshot = Sound(sample_rate=22050, bits_per_sample=16, samples=b"\x01\x00\xff\x7f")
    death = Sound(sample_rate=44100, bits_per_sample=8, samples=b"\x80\x90\xa0")
    client_path = tmp_path / "client.pack"
    server_path = tmp_path / "server.pack"
    cook_scenario(
        FIXTURES_DIR / "mesh_fixture.usda",
        [],
        client_path,
        server_path,
        key_pair.private_key,
        sounds=CueSounds(path="sounds/test", cues=[("gunshot", gunshot), ("death", death)]),
    )

    client = read_pack_contents(client_path, key_pair.public_key)
    server = read_pack_contents(server_path, key_pair.public_key)
    assert client.paths_of_type(pack.ASSET_TYPE_AUDIO) == {"sounds/test/gunshot", "sounds/test/death"}
    assert decode_audio(client.blob("sounds/test/gunshot")) == (22050, 16, b"\x01\x00\xff\x7f")
    assert decode_audio(client.blob("sounds/test/death")) == (44100, 8, b"\x80\x90\xa0")
    assert decode_string(client.blob(pack.SOUNDS_PATH)) == "sounds/test"
    assert client.entries[pack.SOUNDS_PATH][0] == pack.ASSET_TYPE_SOUNDS
    assert server.paths_of_type(pack.ASSET_TYPE_AUDIO) == set()
    assert pack.SOUNDS_PATH not in server.entries


def test_more_characters_than_an_index_can_name_are_refused(tmp_path, key_pair):
    characters = [(f"characters/c{i}", FIXTURES_DIR / "mesh_fixture.usda") for i in range(pack.MAX_CHARACTERS + 1)]

    with pytest.raises(CookError) as raised:
        cook_scenario(
            FIXTURES_DIR / "mesh_fixture.usda",
            characters,
            tmp_path / "client.pack",
            tmp_path / "server.pack",
            key_pair.private_key,
        )

    assert raised.value.code == "characters_encode_failed"


# A character stage (ADR-0040): the Eye every character has, and the given
# hitboxes, each a 0.2 m cube at its height above the feet.
_CHARACTER_STAGE = """#usda 1.0
(
    defaultPrim = "Character"
    upAxis = "Y"
    metersPerUnit = 1
)

def Xform "Character"
{
    def Xform "Eye"
    {
        double3 xformOp:translate = (0, 1.7, 0)
        uniform token[] xformOpOrder = ["xformOp:translate"]
    }
HITBOXES
}
"""

# Every body part once, and a second limb: (prim name, body part, height).
_EVERY_BODY_PART = [("Head", "head", 1.8), ("Torso", "torso", 1.3), ("LeftLeg", "limb", 0.5), ("RightLeg", "limb", 0.5)]


def _hitbox_prim(name, body_part, height):
    body_part_line = f'        custom token augusta:bodyPart = "{body_part}"\n' if body_part is not None else ""
    return (
        f'    def Cube "{name}"\n'
        "    {\n"
        "        custom bool augusta:hitbox = true\n"
        f"{body_part_line}"
        "        double size = 0.2\n"
        f"        double3 xformOp:translate = (0, {height}, 0)\n"
        '        uniform token[] xformOpOrder = ["xformOp:translate"]\n'
        "    }\n"
    )


def cook_character(hitboxes, tmp_path, key_pair):
    """Cooks a map with one character, characters/test, holding hitboxes; returns (client, server) contents."""
    stage_path = tmp_path / "character.usda"
    stage_path.write_text(
        _CHARACTER_STAGE.replace("HITBOXES", "".join(_hitbox_prim(*hitbox) for hitbox in hitboxes)), encoding="utf-8"
    )
    client_path = tmp_path / "client.pack"
    server_path = tmp_path / "server.pack"
    cook_scenario(
        FIXTURES_DIR / "mesh_fixture.usda",
        [("characters/test", stage_path)],
        client_path,
        server_path,
        key_pair.private_key,
    )
    return (
        read_pack_contents(client_path, key_pair.public_key),
        read_pack_contents(server_path, key_pair.public_key),
    )


def test_a_characters_hitboxes_go_into_both_packs_with_their_body_parts(tmp_path, key_pair):
    client, server = cook_character(_EVERY_BODY_PART, tmp_path, key_pair)

    expected = {
        "characters/test/Character/Head": pack.BODY_PART_HEAD,
        "characters/test/Character/Torso": pack.BODY_PART_TORSO,
        "characters/test/Character/LeftLeg": pack.BODY_PART_LIMB,
        "characters/test/Character/RightLeg": pack.BODY_PART_LIMB,
    }
    for contents in (client, server):
        assert contents.paths_of_type(pack.ASSET_TYPE_HITBOX) == set(expected)
        for path, body_part in expected.items():
            assert decode_hitbox(contents.blob(path))[0] == body_part


def test_a_characters_eye_goes_into_both_packs(tmp_path, key_pair):
    client, server = cook_character(_EVERY_BODY_PART, tmp_path, key_pair)

    for contents in (client, server):
        assert contents.paths_of_type(pack.ASSET_TYPE_EYE) == {"characters/test/Character/Eye"}
        assert struct.unpack("<3f", contents.blob("characters/test/Character/Eye")) == pytest.approx((0, 1.7, 0))


def test_a_characters_hitbox_is_placed_relative_to_its_feet(tmp_path, key_pair):
    client, _ = cook_character(_EVERY_BODY_PART, tmp_path, key_pair)

    _, points, _ = decode_hitbox(client.blob("characters/test/Character/Head"))
    assert min(y for _, y, _ in points) == pytest.approx(1.7)
    assert max(y for _, y, _ in points) == pytest.approx(1.9)


@pytest.mark.parametrize("missing", ["head", "torso", "limb"])
def test_a_character_without_a_hitbox_for_a_body_part_is_refused(missing, tmp_path, key_pair):
    hitboxes = [hitbox for hitbox in _EVERY_BODY_PART if hitbox[1] != missing]

    with pytest.raises(CookError) as raised:
        cook_character(hitboxes, tmp_path, key_pair)

    assert raised.value.code == "character_hitbox_missing"
    assert missing in raised.value.message
    assert not (tmp_path / "client.pack").exists()
    assert not (tmp_path / "server.pack").exists()


def test_a_hitbox_that_names_no_body_part_is_refused(tmp_path, key_pair):
    hitboxes = [*_EVERY_BODY_PART, ("Arm", None, 1.3)]

    with pytest.raises(CookError) as raised:
        cook_character(hitboxes, tmp_path, key_pair)

    assert raised.value.code == "hitbox_body_part_missing"
    assert raised.value.prim_path == "characters/test/Character/Arm"


def test_a_hitbox_whose_body_part_is_unknown_is_refused(tmp_path, key_pair):
    hitboxes = [*_EVERY_BODY_PART, ("Tail", "tail", 0.9)]

    with pytest.raises(CookError) as raised:
        cook_character(hitboxes, tmp_path, key_pair)

    assert raised.value.code == "hitbox_body_part_unknown"
    assert raised.value.prim_path == "characters/test/Character/Tail"
    assert "tail" in raised.value.message
