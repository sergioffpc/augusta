"""generate_mip_chain, the native module's mip generation (ADR-0017).

Checked here, before the levels are BC-encoded, since a level's filtering is
out of reach through the cook once its texels are BC blocks.
"""

from augusta_pack import _native
from conftest import alternating_texels
import pytest

BLACK = bytes([0, 0, 0, 255])
WHITE = bytes([255, 255, 255, 255])


def texels(level):
    """A level's (width, height, rgba) as a list of RGBA tuples."""
    _, _, rgba = level
    return [tuple(rgba[i : i + 4]) for i in range(0, len(rgba), 4)]


@pytest.mark.parametrize("texture_format", ["bc7", "dxt1"])
def test_a_colour_textures_levels_are_filtered_in_linear_light(
    texture_format,
):
    # Black and white average to half the light, which sRGB encodes as
    # ~188, not as the ~128 that averaging the stored values gives.
    image = alternating_texels(8, 8, BLACK, WHITE)

    levels = _native.generate_mip_chain(image, 8, 8, texture_format)

    for red, green, blue, alpha in texels(levels[1]):
        assert 186 <= red <= 190
        assert red == green == blue
        assert alpha == 255


@pytest.mark.parametrize("texture_format", ["bc5", "bc4"])
def test_a_data_textures_levels_are_filtered_as_stored(texture_format):
    # Transparent black beside opaque white: averaged as stored, every
    # channel lands halfway, alpha weighting none of the others.
    image = alternating_texels(8, 8, bytes(4), bytes([255] * 4))

    levels = _native.generate_mip_chain(image, 8, 8, texture_format)

    for texel in texels(levels[1]):
        for channel in texel:
            assert 127 <= channel <= 128


@pytest.mark.parametrize(
    ("width", "height", "sizes"),
    [
        (8, 8, [(8, 8), (4, 4), (2, 2), (1, 1)]),
        (5, 3, [(5, 3), (2, 1), (1, 1)]),
        (1, 4, [(1, 4), (1, 2), (1, 1)]),
        (1, 1, [(1, 1)]),
    ],
)
def test_the_chain_runs_down_to_1x1_at_the_ktx2_sizes(width, height, sizes):
    image = bytes(width * height * 4)

    levels = _native.generate_mip_chain(image, width, height, "bc7")

    assert [(w, h) for w, h, _ in levels] == sizes
    assert all(len(rgba) == w * h * 4 for w, h, rgba in levels)


def test_level_0_is_the_image_itself():
    image = bytes(range(64))

    levels = _native.generate_mip_chain(image, 4, 4, "bc7")

    assert levels[0] == (4, 4, image)
