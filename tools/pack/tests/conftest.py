"""Shared fixtures and readers for the cooker's tests.

The readers decode just enough of a pack's blobs (ADR-0031/ADR-0032) to assert
on what was cooked; the C++ runtime stays the reference decoder.
"""

import struct
import wave
from dataclasses import dataclass
from pathlib import Path

import pytest

from pack import keys, reader

FIXTURES_DIR = Path(__file__).parent / "fixtures"
EXAMPLES_ROOT = Path(__file__).parents[2] / "composer" / "examples"


@dataclass(frozen=True)
class KeyPair:
    public_key: bytes
    private_key: bytes


@dataclass(frozen=True)
class PackContents:
    hash: bytes
    # pack-relative path -> (asset type, blob)
    entries: dict[str, tuple[int, bytes]]

    def paths_of_type(self, asset_type: int) -> set[str]:
        return {path for path, (entry_type, _) in self.entries.items() if entry_type == asset_type}

    def blob(self, path: str) -> bytes:
        return self.entries[path][1]


@pytest.fixture
def key_pair() -> KeyPair:
    public_key, private_key = keys.generate_keypair()
    return KeyPair(public_key=public_key, private_key=private_key)


def read_pack_contents(path: Path, public_key: bytes) -> PackContents:
    """Verifies the pack at path against public_key, then reads every blob."""
    info = reader.verify_pack(path, public_key)
    data = path.read_bytes()
    entries = {entry.path: (entry.type, data[entry.offset : entry.offset + entry.size]) for entry in info.entries}
    return PackContents(hash=info.hash, entries=entries)


def decode_mesh(blob: bytes) -> tuple[list[tuple[float, float, float]], list[int]]:
    """A mesh/collision/hitbox blob: u32 point count, f32 xyz each, u32 index count, u32 each."""
    (point_count,) = struct.unpack_from("<I", blob, 0)
    offset = 4
    points = [struct.unpack_from("<3f", blob, offset + 12 * i) for i in range(point_count)]
    offset += 12 * point_count
    (index_count,) = struct.unpack_from("<I", blob, offset)
    offset += 4
    indices = list(struct.unpack_from(f"<{index_count}I", blob, offset))
    return points, indices


def decode_hitbox(blob: bytes) -> tuple[int, list[tuple[float, float, float]], list[int]]:
    """A hitbox blob: u8 body part, then a mesh blob."""
    points, indices = decode_mesh(blob[1:])
    return blob[0], points, indices


def decode_spawn_point(blob: bytes) -> tuple[tuple[float, ...], tuple[float, ...]]:
    """A spawn point blob: translation xyz, then rotation xyzw, all f32."""
    values = struct.unpack("<7f", blob)
    return values[:3], values[3:]


def write_wav(path: Path, frames: bytes, channels: int = 1, sample_width: int = 2, sample_rate: int = 22050) -> None:
    """Writes a PCM WAV file, creating its folder."""
    path.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(path), "wb") as wav:
        wav.setnchannels(channels)
        wav.setsampwidth(sample_width)
        wav.setframerate(sample_rate)
        wav.writeframes(frames)


def write_float_wav(path: Path, samples: int = 4, sample_rate: int = 22050) -> None:
    """Writes a mono 32-bit IEEE float WAV (format tag 3): a WAV that is not PCM."""
    data = struct.pack(f"<{samples}f", *([0.0] * samples))
    fmt = struct.pack("<HHIIHH", 3, 1, sample_rate, sample_rate * 4, 4, 32)
    body = b"WAVE" + b"fmt " + struct.pack("<I", len(fmt)) + fmt + b"data" + struct.pack("<I", len(data)) + data
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(b"RIFF" + struct.pack("<I", len(body)) + body)


def decode_audio(blob: bytes) -> tuple[int, int, bytes]:
    """An audio blob: u32 sample rate, u8 bits per sample, u32 sample byte count, the samples."""
    sample_rate, bits_per_sample, size = struct.unpack_from("<IBI", blob, 0)
    return sample_rate, bits_per_sample, blob[9 : 9 + size]


def decode_string(blob: bytes) -> str:
    """A blob holding one length-prefixed string."""
    (length,) = struct.unpack_from("<I", blob, 0)
    return blob[4 : 4 + length].decode("utf-8")
