"""Shared fixtures and readers for the cooker's tests.

The readers decode just enough of a pack's blobs (ADR-0031/ADR-0032) to assert
on what was cooked; the C++ runtime stays the reference decoder.
"""

from dataclasses import dataclass
from pathlib import Path
import shutil
import struct
import subprocess
import wave
import zlib

from augusta_pack import _native
from augusta_pack import keys
import pytest

FIXTURES_DIR = Path(__file__).parent / "fixtures"
EXAMPLES_ROOT = Path(__file__).parents[2] / "composer" / "examples"
# Where the root build, which builds the native module, installs its vcpkg
# dependencies, ktx[tools] among them (the manifest's pack feature).
BUILD_DIR = Path(__file__).parents[3] / "build"

# The twelve bytes every KTX2 file starts with.
KTX2_IDENTIFIER = b"\xabKTX 20\xbb\r\n\x1a\n"
# The VkFormat values of the block formats the cooker writes (ADR-0017).
VK_FORMAT_BC4_UNORM_BLOCK = 139
VK_FORMAT_BC5_UNORM_BLOCK = 141
VK_FORMAT_BC7_SRGB_BLOCK = 146
# A KTX2 header's supercompressionScheme for Zstandard.
KTX2_SUPERCOMPRESSION_ZSTD = 2


@dataclass(frozen=True)
class KeyPair:
    public_key: bytes
    private_key: bytes


@dataclass(frozen=True)
class PackContents:
    hash: bytes
    client_pack_hash: bytes | None
    # pack-relative path -> (asset type, blob)
    entries: dict[str, tuple[_native.AssetType, bytes]]

    def paths_of_type(self, asset_type: _native.AssetType) -> set[str]:
        return {
            path
            for path, (entry_type, _) in self.entries.items()
            if entry_type == asset_type
        }

    def blob(self, path: str) -> bytes:
        return self.entries[path][1]


@pytest.fixture
def key_pair() -> KeyPair:
    public_key, private_key = keys.generate_keypair()
    return KeyPair(public_key=public_key, private_key=private_key)


def read_pack_contents(path: Path, public_key: bytes) -> PackContents:
    """Loads the pack at path against public_key, then reads every blob.

    Each blob is the bytes the loaded pack's index places it at.
    """
    loaded = _native.load_pack(path, public_key)
    data = path.read_bytes()
    entries = {
        entry.path: (entry.type, data[entry.offset : entry.offset + entry.size])
        for entry in loaded.entries
    }
    return PackContents(
        hash=loaded.hash,
        client_pack_hash=loaded.client_pack_hash,
        entries=entries,
    )


def decode_mesh(
    blob: bytes,
) -> tuple[list[tuple[float, float, float]], list[int]]:
    """A mesh/collision/hitbox blob's points and indices.

    The blob is a u32 point count, f32 xyz each, u32 index count, u32 each.
    """
    (point_count,) = struct.unpack_from("<I", blob, 0)
    offset = 4
    points = [
        struct.unpack_from("<3f", blob, offset + 12 * i)
        for i in range(point_count)
    ]
    offset += 12 * point_count
    (index_count,) = struct.unpack_from("<I", blob, offset)
    offset += 4
    indices = list(struct.unpack_from(f"<{index_count}I", blob, offset))
    return points, indices


@dataclass(frozen=True)
class Hitbox:
    body_part: int
    joint: str
    points: list[tuple[float, float, float]]
    indices: list[int]


def decode_hitbox(blob: bytes) -> Hitbox:
    """A hitbox blob: u8 body part, the joint's name, then a mesh blob.

    The name is a u32 length and its bytes.
    """
    (length,) = struct.unpack_from("<I", blob, 1)
    joint = blob[5 : 5 + length].decode("utf-8")
    points, indices = decode_mesh(blob[5 + length :])
    return Hitbox(blob[0], joint, points, indices)


@dataclass(frozen=True)
class Joint:
    name: str
    parent: int
    translation: tuple[float, float, float]
    # x, y, z, w.
    rotation: tuple[float, float, float, float]
    scale: tuple[float, float, float]


def decode_skeleton(blob: bytes) -> list[Joint]:
    """A skeleton blob's joints, in order.

    The blob is a u32 joint count, then each joint's name as a u32 length and
    its bytes, its parent's index as a u32, then its translation xyz, rotation
    xyzw and scale xyz, all f32.
    """
    (count,) = struct.unpack_from("<I", blob, 0)
    offset = 4
    joints = []
    for _ in range(count):
        (length,) = struct.unpack_from("<I", blob, offset)
        name = blob[offset + 4 : offset + 4 + length].decode("utf-8")
        offset += 4 + length
        parent, *transform = struct.unpack_from("<I10f", blob, offset)
        offset += 44
        joints.append(
            Joint(
                name,
                parent,
                tuple(transform[0:3]),
                tuple(transform[3:7]),
                tuple(transform[7:10]),
            )
        )
    assert offset == len(blob)
    return joints


def decode_spawn_point(
    blob: bytes,
) -> tuple[tuple[float, ...], tuple[float, ...]]:
    """A spawn point blob: translation xyz, then rotation xyzw, all f32."""
    values = struct.unpack("<7f", blob)
    return values[:3], values[3:]


def write_wav(
    path: Path,
    frames: bytes,
    channels: int = 1,
    sample_width: int = 2,
    sample_rate: int = 22050,
) -> None:
    """Writes a PCM WAV file, creating its folder."""
    path.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(path), "wb") as wav:
        wav.setnchannels(channels)
        wav.setsampwidth(sample_width)
        wav.setframerate(sample_rate)
        wav.writeframes(frames)


def write_float_wav(
    path: Path, samples: int = 4, sample_rate: int = 22050
) -> None:
    """Writes a mono 32-bit IEEE float WAV (format tag 3): not PCM."""
    data = struct.pack(f"<{samples}f", *([0.0] * samples))
    fmt = struct.pack("<HHIIHH", 3, 1, sample_rate, sample_rate * 4, 4, 32)
    body = (
        b"WAVE"
        + b"fmt "
        + struct.pack("<I", len(fmt))
        + fmt
        + b"data"
        + struct.pack("<I", len(data))
        + data
    )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(b"RIFF" + struct.pack("<I", len(body)) + body)


def decode_audio(blob: bytes) -> tuple[int, int, bytes]:
    """An audio blob's sample rate, bits per sample and samples.

    The blob is a u32 sample rate, u8 bits per sample, u32 sample byte count,
    then the samples.
    """
    sample_rate, bits_per_sample, size = struct.unpack_from("<IBI", blob, 0)
    return sample_rate, bits_per_sample, blob[9 : 9 + size]


@dataclass(frozen=True)
class Ktx2Header:
    vk_format: int
    width: int
    height: int
    level_count: int
    supercompression_scheme: int


def decode_texture(blob: bytes) -> bytes:
    """A texture blob's KTX2 file: a u32 byte count, then the file, alone."""
    (size,) = struct.unpack_from("<I", blob, 0)
    assert len(blob) == 4 + size
    return blob[4:]


def read_ktx2_header(ktx2: bytes) -> Ktx2Header:
    """The header fields of a KTX2 file the tests assert on."""
    assert ktx2[:12] == KTX2_IDENTIFIER
    # identifier, then vkFormat, typeSize, pixelWidth, pixelHeight,
    # pixelDepth, layerCount, faceCount, levelCount and
    # supercompressionScheme, u32 each.
    fields = struct.unpack_from("<9I", ktx2, 12)
    return Ktx2Header(
        vk_format=fields[0],
        width=fields[2],
        height=fields[3],
        level_count=fields[7],
        supercompression_scheme=fields[8],
    )


def read_ktx2_level_sizes(ktx2: bytes) -> list[int]:
    """Each level's uncompressedByteLength, from level 0 down.

    The level index follows the header and the DFD/KVD/SGD offsets and
    lengths, at byte 80: a u64 byteOffset, byteLength and
    uncompressedByteLength per level.
    """
    level_count = max(1, read_ktx2_header(ktx2).level_count)
    return [
        struct.unpack_from("<3Q", ktx2, 80 + 24 * level)[2]
        for level in range(level_count)
    ]


def encode_png(width: int, height: int, rgba: bytes) -> bytes:
    """An 8-bit RGBA PNG of rgba, row by row from the top."""
    assert len(rgba) == width * height * 4

    def chunk(kind: bytes, data: bytes) -> bytes:
        return (
            struct.pack(">I", len(data))
            + kind
            + data
            + struct.pack(">I", zlib.crc32(kind + data))
        )

    stride = width * 4
    # Filter type 0 (none) before each row.
    raw = b"".join(
        b"\0" + rgba[row * stride : (row + 1) * stride] for row in range(height)
    )
    return (
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(raw))
        + chunk(b"IEND", b"")
    )


def alternating_texels(
    width: int, height: int, even: bytes, odd: bytes
) -> bytes:
    """A width x height RGBA image whose texels alternate, like a checkerboard.

    even is the texel where x + y is even, odd where it is odd.
    """
    return b"".join(
        even if (x + y) % 2 == 0 else odd
        for y in range(height)
        for x in range(width)
    )


def _find_ktx() -> str:
    """Khronos' ktx tool, from the same vcpkg ktx port as the encoder."""
    for name in ("ktx.exe", "ktx"):
        found = sorted(BUILD_DIR.glob(f"*/vcpkg_installed/*/tools/ktx/{name}"))
        if found:
            return str(found[0])
    on_path = shutil.which("ktx")
    if on_path is None:
        pytest.fail(
            "ktx not found: build the root project with the tools' Python "
            "(tools/README.md), whose vcpkg pack feature installs it "
            "(ktx[tools])"
        )
    return on_path


def ktx_validate(ktx2: bytes, tmp_path: Path) -> None:
    """Fails the test unless `ktx validate` accepts the KTX2 file."""
    path = tmp_path / "texture.ktx2"
    path.write_bytes(ktx2)
    result = subprocess.run(
        [_find_ktx(), "validate", "--warnings-as-errors", str(path)],
        capture_output=True,
        text=True,
        check=False,
    )
    assert result.returncode == 0, result.stdout + result.stderr


def decode_string(blob: bytes) -> str:
    """A blob holding one length-prefixed string."""
    (length,) = struct.unpack_from("<I", blob, 0)
    return blob[4 : 4 + length].decode("utf-8")


def decode_cue_sound(blob: bytes) -> tuple[float, list[str]]:
    """A cue-sound blob's gain in decibels and its variants' paths.

    The blob is an f32 gain, a u32 variant count, then each path as a u32
    length and its bytes.
    """
    gain_db, count = struct.unpack_from("<fI", blob, 0)
    offset = 8
    variants = []
    for _ in range(count):
        (length,) = struct.unpack_from("<I", blob, offset)
        variants.append(blob[offset + 4 : offset + 4 + length].decode("utf-8"))
        offset += 4 + length
    assert offset == len(blob)
    return gain_db, variants
