"""Pack container reading and verification (ADR-0031): the read-side
counterpart of pack.write_pack, for inspecting and verifying a pack without
the C++ runtime. Only the container is parsed (header, index, trailer) - blob
contents are never decoded here.

Mirrors the bounds checks of assets.cpp's ParsePackHeader/ParsePackIndex, and
ADR-0031's order for verification: the content hash and signature are checked
before the header/index are trusted.
"""

import hmac
from dataclasses import dataclass
from pathlib import Path

import blake3
import nacl.bindings
import nacl.exceptions

from pack import pack
from pack.wire import ByteReader, WireError

TRAILER_SIZE = pack.BLAKE3_HASH_SIZE + pack.ED25519_SIGNATURE_SIZE
_HASH_CHUNK_SIZE = 1 << 20

ASSET_TYPE_NAMES = {
    pack.ASSET_TYPE_MESH: "mesh",
    pack.ASSET_TYPE_TEXTURE: "texture",
    pack.ASSET_TYPE_AUDIO: "audio",
    pack.ASSET_TYPE_COLLISION: "collision",
    pack.ASSET_TYPE_SPAWN_POINT: "spawn-point",
    pack.ASSET_TYPE_HITBOX: "hitbox",
    pack.ASSET_TYPE_SCENE: "scene",
    pack.ASSET_TYPE_SCRIPT: "script",
}


class PackError(ValueError):
    """Raised when a file isn't a well-formed pack, or fails verification."""


@dataclass(frozen=True)
class IndexEntry:
    type: int
    path: str
    offset: int
    size: int

    @property
    def type_name(self) -> str:
        return ASSET_TYPE_NAMES[self.type]


@dataclass(frozen=True)
class PackInfo:
    version: int
    file_size: int
    data_offset: int
    index_offset: int
    entries: list[IndexEntry]
    hash: bytes
    signature: bytes


def _parse_index(index_bytes: bytes, index_offset: int, index_count: int) -> list[IndexEntry]:
    reader = ByteReader(index_bytes)
    entries: list[IndexEntry] = []
    seen_paths: set[str] = set()
    for _ in range(index_count):
        entry_type = reader.u8()
        if entry_type not in ASSET_TYPE_NAMES:
            raise PackError(f"index entry has unknown asset type {entry_type}")
        path = reader.string(pack.MAX_PATH_LENGTH)
        offset = reader.u64()
        size = reader.u64()
        # Every blob must lie within the data section, never alias into the
        # header, the index or the trailer.
        if offset < pack.HEADER_SIZE or offset > index_offset or size > index_offset - offset:
            raise PackError(f"index entry {path!r} points outside the data section")
        if path in seen_paths:
            raise PackError(f"duplicate index entry {path!r}")
        seen_paths.add(path)
        entries.append(IndexEntry(entry_type, path, offset, size))
    return entries


def read_pack(path: Path) -> PackInfo:
    """Parses the header, index and trailer of the pack at path. Doesn't
    verify the hash or signature (see verify_pack) and reads only those
    sections, never the data section.
    """
    file_size = path.stat().st_size
    if file_size < pack.HEADER_SIZE + TRAILER_SIZE:
        raise PackError("file is too small to be a pack")
    hashed_length = file_size - TRAILER_SIZE

    with open(path, "rb") as f:
        try:
            header = ByteReader(f.read(pack.HEADER_SIZE))
            if header.raw(len(pack.MAGIC)) != pack.MAGIC:
                raise PackError("bad magic - not an Augusta pack")
            version = header.u32()
            if version != pack.FORMAT_VERSION:
                raise PackError(f"unsupported format version {version} (expected {pack.FORMAT_VERSION})")
            data_offset = header.u64()
            index_offset = header.u64()
            index_count = header.u32()
            if data_offset != pack.HEADER_SIZE:
                raise PackError("header is internally inconsistent (data section offset)")
            if index_offset < pack.HEADER_SIZE or index_offset > hashed_length:
                raise PackError("index offset is outside the pack")
            if index_count > pack.MAX_ENTRIES:
                raise PackError(f"index count {index_count} exceeds the limit {pack.MAX_ENTRIES}")

            f.seek(index_offset)
            entries = _parse_index(f.read(hashed_length - index_offset), index_offset, index_count)
        except WireError as error:
            raise PackError(f"pack is truncated or malformed: {error}") from error

        f.seek(hashed_length)
        trailer = f.read(TRAILER_SIZE)

    return PackInfo(
        version=version,
        file_size=file_size,
        data_offset=data_offset,
        index_offset=index_offset,
        entries=entries,
        hash=trailer[: pack.BLAKE3_HASH_SIZE],
        signature=trailer[pack.BLAKE3_HASH_SIZE :],
    )


def verify_pack(path: Path, public_key: bytes) -> PackInfo:
    """Checks that the pack at path is intact and signed by public_key: its
    BLAKE3 hash over everything but the trailer matches the trailer's, and
    the trailer's Ed25519 signature over that hash verifies. Only then are
    the header and index parsed and returned. Raises PackError on any
    failure.
    """
    file_size = path.stat().st_size
    if file_size < pack.HEADER_SIZE + TRAILER_SIZE:
        raise PackError("file is too small to be a pack")
    hashed_length = file_size - TRAILER_SIZE

    hasher = blake3.blake3()
    with open(path, "rb") as f:
        remaining = hashed_length
        while remaining > 0:
            chunk = f.read(min(_HASH_CHUNK_SIZE, remaining))
            if not chunk:
                raise PackError("file changed while it was being read")
            hasher.update(chunk)
            remaining -= len(chunk)
        trailer = f.read(TRAILER_SIZE)

    stored_hash = trailer[: pack.BLAKE3_HASH_SIZE]
    signature = trailer[pack.BLAKE3_HASH_SIZE :]
    if not hmac.compare_digest(hasher.digest(), stored_hash):
        raise PackError("content hash mismatch - the pack is corrupted or has been modified")
    try:
        # crypto_sign_open takes the signature prepended to the signed
        # message - the counterpart of how pack.write_pack detaches it.
        nacl.bindings.crypto_sign_open(signature + stored_hash, public_key)
    except nacl.exceptions.BadSignatureError as error:
        raise PackError("signature is not valid for this public key") from error

    return read_pack(path)
