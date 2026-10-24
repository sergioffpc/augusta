"""cook_scenario on single stages.

What each authored prim becomes in the client and server packs (ADR-0019,
ADR-0032), and how malformed geometry is refused.
"""

import math
import shutil
import struct

from augusta_pack import _native
from augusta_pack.cook import cook_scenario
from augusta_pack.cook import CookError
from augusta_pack.sounds import CueSound
from augusta_pack.sounds import CueSounds
from augusta_pack.sounds import Sound
from conftest import decode_audio
from conftest import decode_cue_sound
from conftest import decode_hitbox
from conftest import decode_mesh
from conftest import decode_skeleton
from conftest import decode_spawn_point
from conftest import decode_texture
from conftest import encode_png
from conftest import FIXTURES_DIR
from conftest import KTX2_SUPERCOMPRESSION_ZSTD
from conftest import ktx_validate
from conftest import read_ktx2_header
from conftest import read_ktx2_level_sizes
from conftest import read_pack_contents
from conftest import VK_FORMAT_BC4_UNORM_BLOCK
from conftest import VK_FORMAT_BC5_UNORM_BLOCK
from conftest import VK_FORMAT_BC7_SRGB_BLOCK
from pxr import Gf
import pytest

# A rifle stage with its three markers, for every cook that is not about the
# rifle (ADR-0064).
RIFLE_FIXTURE = FIXTURES_DIR / "rifle_fixture.usda"


def cook_stage(stage_name, tmp_path, key_pair):
    """Cooks one fixture stage as a map with no characters.

    Returns:
        (client, server) contents.
    """
    client_path = tmp_path / "client.apak"
    server_path = tmp_path / "server.apak"
    cook_scenario(
        FIXTURES_DIR / stage_name,
        [],
        RIFLE_FIXTURE,
        client_path,
        server_path,
        key_pair.private_key,
    )
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
def test_malformed_geometry_is_refused_and_no_pack_is_written(
    stage_name, code, tmp_path, key_pair
):
    with pytest.raises(CookError) as raised:
        cook_stage(stage_name, tmp_path, key_pair)

    assert raised.value.code == code
    assert raised.value.prim_path == "TestMesh"
    assert not (tmp_path / "client.apak").exists()
    assert not (tmp_path / "server.apak").exists()


def test_a_visual_mesh_goes_into_the_client_pack_only(tmp_path, key_pair):
    client, server = cook_stage("mesh_fixture.usda", tmp_path, key_pair)

    assert client.paths_of_type(_native.AssetType.MESH) == {"TestMesh"}
    points, indices = decode_mesh(client.blob("TestMesh"))
    assert len(points) == 4
    assert len(indices) == 6
    assert server.paths_of_type(_native.AssetType.MESH) == set()


def test_duplicate_vertices_are_welded(tmp_path, key_pair):
    client, _ = cook_stage(
        "duplicate_triangles_fixture.usda", tmp_path, key_pair
    )

    points, _ = decode_mesh(client.blob("TestMesh"))
    assert len(points) == 4


def test_each_prim_kind_goes_to_the_packs_that_need_it(tmp_path, key_pair):
    client, server = cook_stage(
        "client_server_split_fixture.usda", tmp_path, key_pair
    )

    assert client.paths_of_type(_native.AssetType.MESH) == {"Root/Visual"}
    assert client.paths_of_type(_native.AssetType.TEXTURE) == {
        "Root/Visual/Mat/DiffuseTexture"
    }
    for contents in (client, server):
        assert contents.paths_of_type(_native.AssetType.COLLISION) == {
            "Root/Collider"
        }
        assert contents.paths_of_type(_native.AssetType.HITBOX) == {
            "Root/Hitbox"
        }
        assert contents.paths_of_type(_native.AssetType.SPAWN_POINT) == {
            "Root/Spawn"
        }
    assert server.paths_of_type(_native.AssetType.MESH) == set()
    assert server.paths_of_type(_native.AssetType.TEXTURE) == set()


def test_a_map_hitbox_carries_its_body_part_and_no_joint(tmp_path, key_pair):
    client, server = cook_stage(
        "client_server_split_fixture.usda", tmp_path, key_pair
    )

    for contents in (client, server):
        hitbox = decode_hitbox(contents.blob("Root/Hitbox"))
        assert hitbox.body_part == int(_native.BodyPart.TORSO)
        assert hitbox.joint == ""
        assert len(hitbox.points) == 4


def test_collision_geometry_is_kept_as_authored(tmp_path, key_pair):
    client, _ = cook_stage(
        "client_server_split_fixture.usda", tmp_path, key_pair
    )

    points, indices = decode_mesh(client.blob("Root/Collider"))
    assert points == [(0, 0, 0), (2, 0, 0), (2, 2, 0), (0, 2, 0)]
    assert indices == [0, 1, 2, 0, 2, 3]


def test_the_server_pack_carries_the_hash_of_its_client_pack(
    tmp_path, key_pair
):
    client, server = cook_stage(
        "client_server_split_fixture.usda", tmp_path, key_pair
    )

    assert server.client_pack_hash == client.hash
    assert client.client_pack_hash is None


def test_a_spawn_point_carries_its_local_transform(tmp_path, key_pair):
    client, _ = cook_stage("scene_fixture.usda", tmp_path, key_pair)

    translation, rotation = decode_spawn_point(client.blob("Root/Spawn"))
    assert translation == (5, 0, 5)
    assert rotation == (0, 0, 0, 1)


def cook_texture(texture_format, tmp_path, key_pair, png=None):
    """Cooks the texture fixture with augusta:textureFormat set, if not None.

    The fixture's image is texture_fixture.png, or png's bytes if given.

    Returns:
        The KTX2 file of the client pack's texture blob.
    """
    stage_dir = tmp_path / "stage"
    stage_dir.mkdir()
    if png is None:
        shutil.copy(FIXTURES_DIR / "texture_fixture.png", stage_dir)
    else:
        (stage_dir / "texture_fixture.png").write_bytes(png)
    stage = (FIXTURES_DIR / "texture_fixture.usda").read_text()
    if texture_format is not None:
        stage = stage.replace(
            "asset inputs:file",
            f'uniform token augusta:textureFormat = "{texture_format}"\n'
            "            asset inputs:file",
        )
    (stage_dir / "texture_fixture.usda").write_text(stage)
    client_path = tmp_path / "client.apak"
    cook_scenario(
        stage_dir / "texture_fixture.usda",
        [],
        RIFLE_FIXTURE,
        client_path,
        tmp_path / "server.apak",
        key_pair.private_key,
    )
    client = read_pack_contents(client_path, key_pair.public_key)
    return decode_texture(client.blob("TestMesh/Mat/DiffuseTexture"))


def test_a_texture_cooks_to_a_ktx2_that_ktx_validate_accepts(
    tmp_path, key_pair
):
    ktx2 = cook_texture(None, tmp_path, key_pair)

    ktx_validate(ktx2, tmp_path)


@pytest.mark.parametrize(
    ("texture_format", "vk_format"),
    [
        ("bc7", VK_FORMAT_BC7_SRGB_BLOCK),
        ("bc5", VK_FORMAT_BC5_UNORM_BLOCK),
        ("bc4", VK_FORMAT_BC4_UNORM_BLOCK),
        ("dxt1", VK_FORMAT_BC7_SRGB_BLOCK),
        (None, VK_FORMAT_BC7_SRGB_BLOCK),
    ],
)
def test_a_texture_cooks_to_the_vk_format_its_texture_format_names(
    texture_format, vk_format, tmp_path, key_pair
):
    ktx2 = cook_texture(texture_format, tmp_path, key_pair)

    assert read_ktx2_header(ktx2).vk_format == vk_format
    ktx_validate(ktx2, tmp_path)


def test_a_texture_is_supercompressed_with_zstandard(tmp_path, key_pair):
    ktx2 = cook_texture(None, tmp_path, key_pair)

    assert (
        read_ktx2_header(ktx2).supercompression_scheme
        == KTX2_SUPERCOMPRESSION_ZSTD
    )


# The bytes of one 4x4 block of each VkFormat the cooker writes.
BLOCK_BYTES = {
    VK_FORMAT_BC7_SRGB_BLOCK: 16,
    VK_FORMAT_BC5_UNORM_BLOCK: 16,
    VK_FORMAT_BC4_UNORM_BLOCK: 8,
}


def level_byte_count(width, height, level, vk_format):
    """A level's size in whole 4x4 blocks.

    Its dimensions are the KTX2 specification's: max(1, floor(base /
    2^level)) on each axis.
    """
    level_width = max(1, width >> level)
    level_height = max(1, height >> level)
    blocks = math.ceil(level_width / 4) * math.ceil(level_height / 4)
    return blocks * BLOCK_BYTES[vk_format]


@pytest.mark.parametrize(("width", "height"), [(8, 8), (16, 4), (5, 3), (3, 7)])
@pytest.mark.parametrize("texture_format", ["bc7", "bc5", "bc4"])
def test_a_texture_cooks_with_its_full_mip_chain(
    width, height, texture_format, tmp_path, key_pair
):
    rgba = bytes(i % 256 for i in range(width * height * 4))
    png = encode_png(width, height, rgba)

    ktx2 = cook_texture(texture_format, tmp_path, key_pair, png)

    header = read_ktx2_header(ktx2)
    assert (header.width, header.height) == (width, height)
    level_count = math.floor(math.log2(max(width, height))) + 1
    assert read_ktx2_level_sizes(ktx2) == [
        level_byte_count(width, height, level, header.vk_format)
        for level in range(level_count)
    ]
    ktx_validate(ktx2, tmp_path)


def test_a_1x1_image_cooks_to_a_single_level(tmp_path, key_pair):
    png = encode_png(1, 1, bytes([10, 20, 30, 255]))

    ktx2 = cook_texture(None, tmp_path, key_pair, png)

    assert read_ktx2_header(ktx2).level_count == 1
    ktx_validate(ktx2, tmp_path)


def test_a_texture_whose_image_is_missing_is_refused(tmp_path, key_pair):
    stage_dir = tmp_path / "stage"
    stage_dir.mkdir()
    shutil.copy(FIXTURES_DIR / "texture_fixture.usda", stage_dir)

    with pytest.raises(CookError) as raised:
        cook_scenario(
            stage_dir / "texture_fixture.usda",
            [],
            RIFLE_FIXTURE,
            tmp_path / "client.apak",
            tmp_path / "server.apak",
            key_pair.private_key,
        )

    assert raised.value.code == "texture_load_failed"
    assert raised.value.prim_path == "TestMesh/Mat/DiffuseTexture"


def test_a_texture_whose_image_is_not_a_png_or_jpeg_is_refused(
    tmp_path, key_pair
):
    stage_dir = tmp_path / "stage"
    stage_dir.mkdir()
    shutil.copy(FIXTURES_DIR / "texture_fixture.usda", stage_dir)
    (stage_dir / "texture_fixture.png").write_bytes(b"not an image")

    with pytest.raises(CookError) as raised:
        cook_scenario(
            stage_dir / "texture_fixture.usda",
            [],
            RIFLE_FIXTURE,
            tmp_path / "client.apak",
            tmp_path / "server.apak",
            key_pair.private_key,
        )

    assert raised.value.code == "texture_load_failed"
    assert raised.value.prim_path == "TestMesh/Mat/DiffuseTexture"


def test_scripts_go_into_the_server_pack_only(tmp_path, key_pair):
    client_path = tmp_path / "client.apak"
    server_path = tmp_path / "server.apak"
    cook_scenario(
        FIXTURES_DIR / "mesh_fixture.usda",
        [],
        RIFLE_FIXTURE,
        client_path,
        server_path,
        key_pair.private_key,
        scripts=[("parameters.lua", b"return {}")],
    )

    client = read_pack_contents(client_path, key_pair.public_key)
    server = read_pack_contents(server_path, key_pair.public_key)
    assert client.paths_of_type(_native.AssetType.SCRIPT) == set()
    assert server.blob("parameters.lua") == b"return {}"


GUNSHOT = Sound(
    sample_rate=22050, bits_per_sample=16, samples=b"\x01\x00\xff\x7f"
)
DEATH = Sound(sample_rate=44100, bits_per_sample=8, samples=b"\x80\x90\xa0")


def cook_sounds(sounds, tmp_path, key_pair):
    """Cooks the mesh fixture with sounds, into client.apak and server.apak."""
    cook_scenario(
        FIXTURES_DIR / "mesh_fixture.usda",
        [],
        RIFLE_FIXTURE,
        tmp_path / "client.apak",
        tmp_path / "server.apak",
        key_pair.private_key,
        sounds=CueSounds(cues=sounds),
    )


def test_a_cues_variants_and_gain_go_into_the_client_pack_only(
    tmp_path, key_pair
):
    cook_sounds(
        [
            ("gunshot", CueSound(variants=[GUNSHOT, DEATH], gain_db=-6.0)),
            ("death", CueSound(variants=[DEATH])),
        ],
        tmp_path,
        key_pair,
    )

    client = read_pack_contents(tmp_path / "client.apak", key_pair.public_key)
    server = read_pack_contents(tmp_path / "server.apak", key_pair.public_key)
    assert client.paths_of_type(_native.AssetType.CUE_SOUND) == {
        "sounds/gunshot",
        "sounds/death",
    }
    assert decode_cue_sound(client.blob("sounds/gunshot")) == (
        -6.0,
        ["sounds/gunshot/0", "sounds/gunshot/1"],
    )
    assert decode_cue_sound(client.blob("sounds/death")) == (
        0.0,
        ["sounds/death/0"],
    )
    assert client.paths_of_type(_native.AssetType.AUDIO) == {
        "sounds/gunshot/0",
        "sounds/gunshot/1",
        "sounds/death/0",
    }
    assert decode_audio(client.blob("sounds/gunshot/0")) == (
        22050,
        16,
        b"\x01\x00\xff\x7f",
    )
    assert decode_audio(client.blob("sounds/gunshot/1")) == (
        44100,
        8,
        b"\x80\x90\xa0",
    )
    assert server.paths_of_type(_native.AssetType.AUDIO) == set()
    assert server.paths_of_type(_native.AssetType.CUE_SOUND) == set()


@pytest.mark.parametrize(
    "sound",
    [
        CueSound(variants=[]),
        CueSound(variants=[GUNSHOT], gain_db=math.inf),
        CueSound(variants=[GUNSHOT], gain_db=math.nan),
    ],
)
def test_a_cue_sound_the_client_cannot_play_is_refused_naming_the_cue(
    sound, tmp_path, key_pair
):
    with pytest.raises(CookError, match="hit_marker") as refused:
        cook_sounds([("hit_marker", sound)], tmp_path, key_pair)

    assert refused.value.code == "sound_encode_failed"
    assert not (tmp_path / "client.apak").exists()
    assert not (tmp_path / "server.apak").exists()


def test_more_characters_than_an_index_can_name_are_refused(tmp_path, key_pair):
    characters = [
        (f"c{i}", FIXTURES_DIR / "mesh_fixture.usda")
        for i in range(_native.MAX_CHARACTERS + 1)
    ]

    with pytest.raises(CookError) as raised:
        cook_scenario(
            FIXTURES_DIR / "mesh_fixture.usda",
            characters,
            RIFLE_FIXTURE,
            tmp_path / "client.apak",
            tmp_path / "server.apak",
            key_pair.private_key,
        )

    assert raised.value.code == "characters_encode_failed"


def test_a_character_named_as_a_root_prim_of_the_map_is_refused(
    tmp_path, key_pair
):
    with pytest.raises(CookError) as raised:
        cook_scenario(
            FIXTURES_DIR / "mesh_fixture.usda",
            [("TestMesh", FIXTURES_DIR / "mesh_fixture.usda")],
            RIFLE_FIXTURE,
            tmp_path / "client.apak",
            tmp_path / "server.apak",
            key_pair.private_key,
        )

    assert raised.value.code == "character_name_collides"
    assert raised.value.prim_path == "TestMesh"


# A character stage (ADR-0040): the Eye every character has, its Skeleton, and
# the given hitboxes, each a 0.2 m cube at its height above the feet.
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
SKELETON
HITBOXES
}
"""


def _rest(translation, axis=(0, 1, 0), degrees=0.0):
    """A joint's rest transform: turned degrees about axis, then translated."""
    matrix = Gf.Matrix4d(1.0)
    matrix.SetTransform(
        Gf.Rotation(Gf.Vec3d(*axis), degrees), Gf.Vec3d(*translation)
    )
    return matrix


# The Skeleton of a standing character, each joint relative to its parent: the
# root at the feet, and a neck at 1.7 m.
_JOINTS = [
    ("Root", _rest((0, 0, 0))),
    ("Root/Hips", _rest((0, 0.9, 0))),
    ("Root/Hips/Chest", _rest((0, 0.4, 0))),
    ("Root/Hips/Chest/Neck", _rest((0, 0.4, 0))),
]

# Every body part once, and a second limb: (prim name, body part, height, the
# joint it is authored under).
_EVERY_BODY_PART = [
    ("Head", "head", 1.8, "Root/Hips/Chest/Neck"),
    ("Torso", "torso", 1.3, "Root/Hips/Chest"),
    ("LeftLeg", "limb", 0.5, "Root/Hips"),
    ("RightLeg", "limb", 0.5, "Root/Hips"),
]


def _skeleton_prim(joints, translation=None):
    """A Skeleton prim of joints, (name, rest transform) each."""
    names = ", ".join(f'"{name}"' for name, _ in joints)
    rests = ", ".join(str(rest) for _, rest in joints)
    translate = (
        f"        double3 xformOp:translate = {translation}\n"
        '        uniform token[] xformOpOrder = ["xformOp:translate"]\n'
        if translation is not None
        else ""
    )
    return (
        '    def Skeleton "Skeleton"\n'
        "    {\n"
        f"        uniform token[] joints = [{names}]\n"
        f"        uniform matrix4d[] restTransforms = [{rests}]\n"
        f"{translate}"
        "    }\n"
    )


def _hitbox_prim(name, body_part, height, joint):
    """A hitbox bound rigidly to joint, as UsdSkel binds one: none if None."""
    body_part_line = (
        f'        custom token augusta:bodyPart = "{body_part}"\n'
        if body_part is not None
        else ""
    )
    binding_schema = (
        ' (\n        prepend apiSchemas = ["SkelBindingAPI"]\n    )'
        if joint is not None
        else ""
    )
    binding = (
        f'        uniform token[] skel:joints = ["{joint}"]\n'
        "        int[] primvars:skel:jointIndices = [0] (\n"
        '            elementSize = 1\n            interpolation = "constant"\n'
        "        )\n"
        if joint is not None
        else ""
    )
    return (
        f'    def Cube "{name}"{binding_schema}\n'
        "    {\n        custom bool augusta:hitbox = true\n"
        f"{body_part_line}"
        f"{binding}"
        "        double size = 0.2\n"
        f"        double3 xformOp:translate = (0, {height}, 0)\n"
        '        uniform token[] xformOpOrder = ["xformOp:translate"]\n'
        "    }\n"
    )


_SKELETON = _skeleton_prim(_JOINTS)


def cook_character(hitboxes, tmp_path, key_pair, skeleton=_SKELETON):
    """Cooks a map with one character, named test, holding hitboxes.

    Its Skeleton is the skeleton prim's text, none if it is empty.

    Returns:
        (client, server) contents.
    """
    stage_path = tmp_path / "character.usda"
    stage_path.write_text(
        _CHARACTER_STAGE.replace("SKELETON", skeleton).replace(
            "HITBOXES", "".join(_hitbox_prim(*hitbox) for hitbox in hitboxes)
        ),
        encoding="utf-8",
    )
    client_path = tmp_path / "client.apak"
    server_path = tmp_path / "server.apak"
    cook_scenario(
        FIXTURES_DIR / "mesh_fixture.usda",
        [("test", stage_path)],
        RIFLE_FIXTURE,
        client_path,
        server_path,
        key_pair.private_key,
    )
    return (
        read_pack_contents(client_path, key_pair.public_key),
        read_pack_contents(server_path, key_pair.public_key),
    )


def test_a_characters_hitboxes_go_into_both_packs_with_parts_and_joints(
    tmp_path, key_pair
):
    client, server = cook_character(_EVERY_BODY_PART, tmp_path, key_pair)

    expected = {
        "test/Character/Head": (
            int(_native.BodyPart.HEAD),
            "Root/Hips/Chest/Neck",
        ),
        "test/Character/Torso": (
            int(_native.BodyPart.TORSO),
            "Root/Hips/Chest",
        ),
        "test/Character/LeftLeg": (int(_native.BodyPart.LIMB), "Root/Hips"),
        "test/Character/RightLeg": (int(_native.BodyPart.LIMB), "Root/Hips"),
    }
    for contents in (client, server):
        assert contents.paths_of_type(_native.AssetType.HITBOX) == set(expected)
        for path, (body_part, joint) in expected.items():
            hitbox = decode_hitbox(contents.blob(path))
            assert (hitbox.body_part, hitbox.joint) == (body_part, joint)


def test_a_characters_skeleton_goes_into_both_packs_with_every_joint(
    tmp_path, key_pair
):
    client, server = cook_character(_EVERY_BODY_PART, tmp_path, key_pair)

    for contents in (client, server):
        assert contents.paths_of_type(_native.AssetType.SKELETON) == {
            "test/Character/Skeleton"
        }
        joints = decode_skeleton(contents.blob("test/Character/Skeleton"))
        # Root carries no hitbox, and is a joint all the same.
        assert [(joint.name, joint.parent) for joint in joints] == [
            ("Root", _native.JOINT_NO_PARENT),
            ("Root/Hips", 0),
            ("Root/Hips/Chest", 1),
            ("Root/Hips/Chest/Neck", 2),
        ]
        chest = joints[2]
        assert chest.translation == pytest.approx((0, 0.4, 0))
        assert chest.rotation == pytest.approx((0, 0, 0, 1))
        assert chest.scale == pytest.approx((1, 1, 1))
    assert client.blob("test/Character/Skeleton") == server.blob(
        "test/Character/Skeleton"
    )


def test_a_characters_eye_goes_into_both_packs(tmp_path, key_pair):
    client, server = cook_character(_EVERY_BODY_PART, tmp_path, key_pair)

    for contents in (client, server):
        assert contents.paths_of_type(_native.AssetType.EYE) == {
            "test/Character/Eye"
        }
        assert struct.unpack(
            "<3f", contents.blob("test/Character/Eye")
        ) == pytest.approx((0, 1.7, 0))


def test_a_characters_hitbox_is_cooked_into_its_joints_space(
    tmp_path, key_pair
):
    client, _ = cook_character(_EVERY_BODY_PART, tmp_path, key_pair)

    # 1.7 to 1.9 m above the feet, and the neck at 1.7 m.
    points = decode_hitbox(client.blob("test/Character/Head")).points
    assert min(y for _, y, _ in points) == pytest.approx(0.0, abs=1e-6)
    assert max(y for _, y, _ in points) == pytest.approx(0.2)


def test_a_hitbox_under_a_turned_joint_is_cooked_turned_back(
    tmp_path, key_pair
):
    # The neck at 1.3 m, turned a quarter about +X: its +Y is the character's
    # +Z, so the head 0.5 m above it is 0.5 m along the neck's -Z.
    joints = [
        ("Root", _rest((0, 0, 0))),
        ("Root/Neck", _rest((0, 1.3, 0), axis=(1, 0, 0), degrees=90.0)),
    ]
    hitboxes = [
        ("Head", "head", 1.8, "Root/Neck"),
        ("Torso", "torso", 1.3, "Root"),
        ("Leg", "limb", 0.5, "Root"),
    ]

    client, _ = cook_character(
        hitboxes, tmp_path, key_pair, skeleton=_skeleton_prim(joints)
    )

    points = decode_hitbox(client.blob("test/Character/Head")).points
    for axis, low, high in ((0, -0.1, 0.1), (1, -0.1, 0.1), (2, -0.6, -0.4)):
        assert min(point[axis] for point in points) == pytest.approx(low)
        assert max(point[axis] for point in points) == pytest.approx(high)


def test_a_character_without_a_skeleton_is_refused(tmp_path, key_pair):
    with pytest.raises(CookError) as raised:
        cook_character(_EVERY_BODY_PART, tmp_path, key_pair, skeleton="")

    assert raised.value.code == "character_skeleton_missing"
    assert raised.value.prim_path == "test/Character"
    assert not (tmp_path / "client.apak").exists()
    assert not (tmp_path / "server.apak").exists()


def test_a_hitbox_authored_under_no_joint_is_refused(tmp_path, key_pair):
    hitboxes = [*_EVERY_BODY_PART, ("Arm", "limb", 1.3, None)]

    with pytest.raises(CookError) as raised:
        cook_character(hitboxes, tmp_path, key_pair)

    assert raised.value.code == "hitbox_joint_missing"
    assert raised.value.prim_path == "test/Character/Arm"
    assert not (tmp_path / "client.apak").exists()


def test_a_hitbox_under_a_joint_its_skeleton_lacks_is_refused(
    tmp_path, key_pair
):
    hitboxes = [*_EVERY_BODY_PART, ("Tail", "limb", 0.9, "Root/Hips/Tail")]

    with pytest.raises(CookError) as raised:
        cook_character(hitboxes, tmp_path, key_pair)

    assert raised.value.code == "hitbox_joint_unknown"
    assert raised.value.prim_path == "test/Character/Tail"
    assert "Root/Hips/Tail" in raised.value.message


@pytest.mark.parametrize(
    "skeleton",
    [
        _skeleton_prim([("Root", _rest((0, 0.1, 0))), *_JOINTS[1:]]),
        _skeleton_prim(
            [("Root", _rest((0, 0, 0), degrees=90.0)), *_JOINTS[1:]]
        ),
        _skeleton_prim(_JOINTS, translation=(0, 0.1, 0)),
    ],
    ids=["root_raised", "root_turned", "skeleton_moved"],
)
def test_a_skeleton_rooted_away_from_the_characters_origin_is_refused(
    skeleton, tmp_path, key_pair
):
    with pytest.raises(CookError) as raised:
        cook_character(_EVERY_BODY_PART, tmp_path, key_pair, skeleton=skeleton)

    assert raised.value.code == "skeleton_root_off_origin"
    assert raised.value.prim_path == "test/Character/Skeleton"
    assert "Root" in raised.value.message


def _joints(count):
    """A skeleton of count joints: a root at the feet, and children of it."""
    return [
        ("Root", _rest((0, 0, 0))),
        *((f"Root/J{i}", _rest((0, 1, 0))) for i in range(count - 1)),
    ]


def test_a_skeleton_with_as_many_joints_as_the_format_allows_cooks(
    tmp_path, key_pair
):
    joints = _joints(_native.MAX_JOINTS)

    client, _ = cook_character(
        [
            (name, part, height, "Root")
            for name, part, height, _ in _EVERY_BODY_PART
        ],
        tmp_path,
        key_pair,
        skeleton=_skeleton_prim(joints),
    )

    assert len(decode_skeleton(client.blob("test/Character/Skeleton"))) == 128


def test_a_skeleton_with_more_joints_than_the_format_allows_is_refused(
    tmp_path, key_pair
):
    with pytest.raises(CookError) as raised:
        cook_character(
            _EVERY_BODY_PART,
            tmp_path,
            key_pair,
            skeleton=_skeleton_prim(_joints(_native.MAX_JOINTS + 1)),
        )

    assert raised.value.code == "skeleton_too_many_joints"
    assert raised.value.prim_path == "test/Character/Skeleton"
    assert "129" in raised.value.message
    assert "128" in raised.value.message
    assert not (tmp_path / "client.apak").exists()


@pytest.mark.parametrize(
    "skeleton",
    [
        # A child before its parent.
        _skeleton_prim([_JOINTS[1], _JOINTS[0], *_JOINTS[2:]]),
        # One name for two joints.
        _skeleton_prim([*_JOINTS, _JOINTS[3]]),
        # A rest transform short.
        _skeleton_prim(_JOINTS).replace(f", {_JOINTS[3][1]}]", "]"),
    ],
    ids=["misordered", "duplicate", "rest_transform_missing"],
)
def test_a_skeleton_usdskel_would_not_pose_is_refused(
    skeleton, tmp_path, key_pair
):
    with pytest.raises(CookError) as raised:
        cook_character(_EVERY_BODY_PART, tmp_path, key_pair, skeleton=skeleton)

    assert raised.value.code == "skeleton_invalid"
    assert raised.value.prim_path == "test/Character/Skeleton"


@pytest.mark.parametrize("missing", ["head", "torso", "limb"])
def test_a_character_without_a_hitbox_for_a_body_part_is_refused(
    missing, tmp_path, key_pair
):
    hitboxes = [hitbox for hitbox in _EVERY_BODY_PART if hitbox[1] != missing]

    with pytest.raises(CookError) as raised:
        cook_character(hitboxes, tmp_path, key_pair)

    assert raised.value.code == "character_hitbox_missing"
    assert missing in raised.value.message
    assert not (tmp_path / "client.apak").exists()
    assert not (tmp_path / "server.apak").exists()


def test_a_hitbox_that_names_no_body_part_is_refused(tmp_path, key_pair):
    hitboxes = [*_EVERY_BODY_PART, ("Arm", None, 1.3, "Root/Hips/Chest")]

    with pytest.raises(CookError) as raised:
        cook_character(hitboxes, tmp_path, key_pair)

    assert raised.value.code == "hitbox_body_part_missing"
    assert raised.value.prim_path == "test/Character/Arm"


def test_a_hitbox_whose_body_part_is_unknown_is_refused(tmp_path, key_pair):
    hitboxes = [*_EVERY_BODY_PART, ("Tail", "tail", 0.9, "Root/Hips")]

    with pytest.raises(CookError) as raised:
        cook_character(hitboxes, tmp_path, key_pair)

    assert raised.value.code == "hitbox_body_part_unknown"
    assert raised.value.prim_path == "test/Character/Tail"
    assert "tail" in raised.value.message


def cook_rifle(rifle_stage_path, tmp_path, key_pair):
    """Cooks the mesh fixture with the rifle at rifle_stage_path.

    Returns:
        (client, server) contents.
    """
    client_path = tmp_path / "client.apak"
    server_path = tmp_path / "server.apak"
    cook_scenario(
        FIXTURES_DIR / "mesh_fixture.usda",
        [],
        rifle_stage_path,
        client_path,
        server_path,
        key_pair.private_key,
    )
    return (
        read_pack_contents(client_path, key_pair.public_key),
        read_pack_contents(server_path, key_pair.public_key),
    )


def decode_rifle(blob):
    """A rifle blob: the Grip, Muzzle and Sight, each (translation, xyzw)."""
    values = struct.unpack("<21f", blob)
    return [(values[i : i + 3], values[i + 3 : i + 7]) for i in (0, 7, 14)]


def test_the_rifles_markers_go_into_both_packs(tmp_path, key_pair):
    client, server = cook_rifle(RIFLE_FIXTURE, tmp_path, key_pair)

    half = math.sqrt(0.5)
    for contents in (client, server):
        assert contents.paths_of_type(_native.AssetType.RIFLE) == {
            _native.RIFLE_PATH
        }
        grip, muzzle, sight = decode_rifle(contents.blob(_native.RIFLE_PATH))
        assert grip == (
            pytest.approx((0, -0.1, 0.1)),
            pytest.approx((0, 0, 0, 1)),
        )
        # A quarter turn about +Y.
        assert muzzle == (
            pytest.approx((0, 0, -0.7)),
            pytest.approx((0, half, 0, half)),
        )
        assert sight == (
            pytest.approx((0, 0.07, -0.1)),
            pytest.approx((0, 0, 0, 1)),
        )


def test_no_rifle_mesh_goes_into_either_pack(tmp_path, key_pair):
    client, server = cook_rifle(RIFLE_FIXTURE, tmp_path, key_pair)

    for contents in (client, server):
        assert not any(
            path.startswith(_native.RIFLE_PATH)
            for path in contents.paths_of_type(_native.AssetType.MESH)
        )


def test_a_z_up_rifles_markers_are_turned_y_up(tmp_path, key_pair):
    stage_path = tmp_path / "rifle.usda"
    stage_path.write_text(
        RIFLE_FIXTURE.read_text(encoding="utf-8").replace(
            'upAxis = "Y"', 'upAxis = "Z"'
        ),
        encoding="utf-8",
    )

    client, _ = cook_rifle(stage_path, tmp_path, key_pair)

    grip, _, _ = decode_rifle(client.blob(_native.RIFLE_PATH))
    # Z-up (0, -0.1, 0.1) is Y-up (0, 0.1, 0.1).
    assert grip[0] == pytest.approx((0, 0.1, 0.1))


@pytest.mark.parametrize("marker", ["Grip", "Muzzle", "Sight"])
def test_a_rifle_missing_a_marker_is_refused_naming_it_and_its_stage(
    marker, tmp_path, key_pair
):
    stage_path = tmp_path / "rifle.usda"
    stage_path.write_text(
        RIFLE_FIXTURE.read_text(encoding="utf-8").replace(
            f'def Xform "{marker}"', 'def Xform "Elsewhere"'
        ),
        encoding="utf-8",
    )

    with pytest.raises(CookError) as raised:
        cook_rifle(stage_path, tmp_path, key_pair)

    assert raised.value.code == "rifle_marker_missing"
    assert marker in raised.value.message
    assert str(stage_path) in raised.value.message
    assert not (tmp_path / "client.apak").exists()
    assert not (tmp_path / "server.apak").exists()


def test_a_rifle_stage_without_a_default_prim_is_refused(tmp_path, key_pair):
    stage_path = tmp_path / "rifle.usda"
    stage_path.write_text(
        RIFLE_FIXTURE.read_text(encoding="utf-8").replace(
            'defaultPrim = "Rifle"', ""
        ),
        encoding="utf-8",
    )

    with pytest.raises(CookError) as raised:
        cook_rifle(stage_path, tmp_path, key_pair)

    assert raised.value.code == "rifle_root_missing"
    assert str(stage_path) in raised.value.message
