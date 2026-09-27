"""cook_scenario on single stages: what each authored prim becomes in the
client and server packs (ADR-0019, ADR-0032), and how malformed geometry is
refused.
"""

import shutil

import pytest
from conftest import FIXTURES_DIR, decode_mesh, decode_spawn_point, read_pack_contents

from pack import pack
from pack.cook import CookError, cook_scenario


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
