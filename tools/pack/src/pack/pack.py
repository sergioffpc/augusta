"""Pure-Python reimplementation of augusta_assets' pack wire format
(ADR-0007/ADR-0018/ADR-0030/ADR-0031/ADR-0032): the Encode* blob functions
and WritePack (header/data/index/trailer, BLAKE3 hash, Ed25519 sign).

augusta_assets itself has no OpenUSD dependency (only libsodium/BLAKE3/
mio), so linking it here wouldn't touch the DLL-collision problem ADR-0030
describes - this is a separate, deliberate choice to keep the cooker's
write path entirely in Python rather than crossing into C++ for it via a
binding (see that ADR's Considered Options). Every constant and field
order below is validated byte-for-byte against augusta_assets' own
implementation (wire_format.h, encoder.cpp, assets.cpp): a pack written
here decodes identically to one WritePack() would have produced from the
same entries, confirmed by cross-checking BLAKE3 (the `blake3` package)
and Ed25519 (`pynacl`'s libsodium bindings) output against the real C++
blake3/libsodium libraries directly.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path

import blake3
import nacl.bindings

from pack.wire import ByteWriter

# Pragmatic v1 sanity limits (wire_format.h/assets.cpp) - narrowing
# guards, not just validation: every limit here is comfortably under
# UINT32_MAX.
MAX_PATH_LENGTH = 4096
MAX_MESH_POINTS = 16_000_000
MAX_MESH_INDICES = 48_000_000
MAX_SCENE_NODES = 1_000_000
MAX_PROPERTIES = 256
MAX_TEXTURE_BYTES = 256 * 1024 * 1024
# A Lua script is hand-written text; a megabyte is far beyond any real one.
MAX_SCRIPT_BYTES = 1024 * 1024
# A character index is one byte and zero is never valid (ADR-0042).
MAX_CHARACTERS = 255
MAX_ENTRIES = 1 << 20
MAX_PACK_SIZE = 8 * 1024 * 1024 * 1024

# Scene node optional-reference flag bits (wire_format.h).
_NODE_HAS_MESH = 1 << 0
_NODE_HAS_MATERIAL = 1 << 1
_NODE_HAS_COLLIDER = 1 << 2
_NODE_HAS_HITBOX = 1 << 3
_NODE_IS_SPAWN_POINT = 1 << 4

# AssetType (assets.h `enum class AssetType : uint8_t`) - the wire's own
# type tag, order-dependent, must match exactly.
ASSET_TYPE_MESH = 0
ASSET_TYPE_TEXTURE = 1
ASSET_TYPE_AUDIO = 2
ASSET_TYPE_COLLISION = 3
ASSET_TYPE_SPAWN_POINT = 4
ASSET_TYPE_HITBOX = 5
ASSET_TYPE_SCENE = 6
ASSET_TYPE_SCRIPT = 7
ASSET_TYPE_CHARACTERS = 8

# Pack-relative path of a scenario's character list (assets.h's
# kCharactersPath), in both of its packs.
CHARACTERS_PATH = "Characters"

# TextureFormat (assets.h `enum class TextureFormat : uint8_t`).
TEXTURE_FORMAT_BC7 = 0
TEXTURE_FORMAT_BC5 = 1
TEXTURE_FORMAT_BC4 = 2

# Sentinel parent_index for a SceneNode with no parent (assets.h's
# kSceneNodeNoParent).
NO_PARENT = 0xFFFFFFFF

MAGIC = b"AUGP"
FORMAT_VERSION = 1
# magic(4) + version(u32=4) + data_offset(u64=8) + index_offset(u64=8) +
# index_count(u32=4) - assets.cpp's kHeaderSize.
HEADER_SIZE = 4 + 4 + 8 + 8 + 4
BLAKE3_HASH_SIZE = 32
ED25519_SIGNATURE_SIZE = 64


class EncodeError(ValueError):
    """Raised when a blob exceeds a v1 size limit."""


@dataclass
class AssetEntry:
    type: int
    path: str
    data: bytes


@dataclass
class MeshData:
    points: list[tuple[float, float, float]]
    indices: list[int]


@dataclass
class SceneNode:
    name: str
    parent_index: int = NO_PARENT
    translation: tuple[float, float, float] = (0.0, 0.0, 0.0)
    rotation: tuple[float, float, float, float] = (0.0, 0.0, 0.0, 1.0)  # x, y, z, w
    scale: tuple[float, float, float] = (1.0, 1.0, 1.0)
    mesh_path: str | None = None
    material_path: str | None = None
    collider_path: str | None = None
    hitbox_path: str | None = None
    is_spawn_point: bool = False
    properties: list[tuple[str, str]] = field(default_factory=list)


def encode_mesh_blob(mesh: MeshData) -> bytes:
    if len(mesh.points) > MAX_MESH_POINTS or len(mesh.indices) > MAX_MESH_INDICES:
        raise EncodeError("mesh exceeds pack size limits")
    writer = ByteWriter()
    writer.u32(len(mesh.points))
    for x, y, z in mesh.points:
        writer.f32(x)
        writer.f32(y)
        writer.f32(z)
    writer.u32(len(mesh.indices))
    for index in mesh.indices:
        writer.u32(index)
    return writer.bytes()


def _encode_scene_node(writer: ByteWriter, node: SceneNode) -> None:
    writer.string(node.name, MAX_PATH_LENGTH)
    writer.u32(node.parent_index)
    for component in node.translation:
        writer.f32(component)
    for component in node.rotation:
        writer.f32(component)
    for component in node.scale:
        writer.f32(component)

    flags = 0
    if node.mesh_path is not None:
        flags |= _NODE_HAS_MESH
    if node.material_path is not None:
        flags |= _NODE_HAS_MATERIAL
    if node.collider_path is not None:
        flags |= _NODE_HAS_COLLIDER
    if node.hitbox_path is not None:
        flags |= _NODE_HAS_HITBOX
    if node.is_spawn_point:
        flags |= _NODE_IS_SPAWN_POINT
    writer.u8(flags)

    for path in (node.mesh_path, node.material_path, node.collider_path, node.hitbox_path):
        writer.optional_path(path, MAX_PATH_LENGTH)

    if len(node.properties) > MAX_PROPERTIES:
        raise EncodeError("scene node has too many properties")
    writer.u32(len(node.properties))
    for key, value in node.properties:
        writer.string(key, MAX_PATH_LENGTH)
        writer.string(value, MAX_PATH_LENGTH)


def encode_scene_blob(nodes: list[SceneNode]) -> bytes:
    if len(nodes) > MAX_SCENE_NODES:
        raise EncodeError("scene graph exceeds pack size limits")
    writer = ByteWriter()
    writer.u32(len(nodes))
    for node in nodes:
        _encode_scene_node(writer, node)
    return writer.bytes()


def encode_texture_blob(dds_bytes: bytes, texture_format: int) -> bytes:
    if len(dds_bytes) > MAX_TEXTURE_BYTES:
        raise EncodeError("texture exceeds pack size limits")
    writer = ByteWriter()
    writer.u8(texture_format)
    writer.u32(len(dds_bytes))
    writer.raw(dds_bytes)
    return writer.bytes()


def encode_script_blob(script: bytes) -> bytes:
    """A script blob is the script's text as it is: no framing, no terminator."""
    if len(script) > MAX_SCRIPT_BYTES:
        raise EncodeError("script exceeds pack size limits")
    return bytes(script)


def encode_characters_blob(characters: list[str]) -> bytes:
    """A u32 count, then each character's path relative to authoring/ as a
    length-prefixed string, in manifest order: character index N is element
    N-1 (ADR-0042).
    """
    if len(characters) > MAX_CHARACTERS:
        raise EncodeError(f"a scenario composes at most {MAX_CHARACTERS} characters, this one names {len(characters)}")
    writer = ByteWriter()
    writer.u32(len(characters))
    for character in characters:
        writer.string(character, MAX_PATH_LENGTH)
    return writer.bytes()


def encode_spawn_point_blob(
    translation: tuple[float, float, float], rotation: tuple[float, float, float, float]
) -> bytes:
    writer = ByteWriter()
    for component in translation:
        writer.f32(component)
    for component in rotation:
        writer.f32(component)
    return writer.bytes()


class WriteError(RuntimeError):
    """Raised when entries can't be assembled into a valid pack."""


def _validate_entries(entries: list[AssetEntry]) -> None:
    if len(entries) > MAX_ENTRIES:
        raise WriteError("too many entries")
    paths = [entry.path for entry in entries]
    for path in paths:
        if len(path.encode("utf-8")) > MAX_PATH_LENGTH:
            raise WriteError(f"entry path too long: {path!r}")
    if len(set(paths)) != len(paths):
        raise WriteError("duplicate entry path")


def write_pack(output_path: Path, entries: list[AssetEntry], signing_key: bytes) -> None:
    """Writes entries into a new pack file at output_path, in ADR-0031's
    header/data/index/trailer order, signing the trailer with signing_key
    (the raw 64-byte Ed25519 secret key, libsodium's own seed+pubkey
    layout - e.g. from keys.py's generate_keypair). Atomic: assembled into
    a temporary file first, renamed into place only once fully written.
    """
    _validate_entries(entries)
    if len(signing_key) != 64:
        raise WriteError(f"signing_key must be 64 bytes, got {len(signing_key)}")

    data_section = bytearray()
    offsets: list[int] = []
    sizes: list[int] = []
    cursor = HEADER_SIZE
    for entry in entries:
        offsets.append(cursor)
        sizes.append(len(entry.data))
        data_section += entry.data
        cursor += len(entry.data)
    index_offset = cursor

    index_section = ByteWriter()
    for entry, offset, size in zip(entries, offsets, sizes):
        index_section.u8(entry.type)
        index_section.string(entry.path, MAX_PATH_LENGTH)
        index_section.u64(offset)
        index_section.u64(size)

    header = ByteWriter()
    header.raw(MAGIC)
    header.u32(FORMAT_VERSION)
    header.u64(HEADER_SIZE)
    header.u64(index_offset)
    header.u32(len(entries))

    total_size = len(header) + len(data_section) + len(index_section) + BLAKE3_HASH_SIZE + ED25519_SIGNATURE_SIZE
    if total_size > MAX_PACK_SIZE:
        raise WriteError("pack exceeds size limit")

    hashed = header.bytes() + bytes(data_section) + index_section.bytes()
    pack_hash = blake3.blake3(hashed).digest()
    # crypto_sign (attached signing) prepends the 64-byte detached
    # signature to the message it signs - libsodium implements
    # crypto_sign_detached as exactly this, minus re-appending the
    # message, so signed[:64] here is byte-identical to what
    # crypto_sign_detached(hash, signing_key) would have produced. PyNaCl
    # (nacl.bindings) exposes no detached-signing entry point directly.
    signed = nacl.bindings.crypto_sign(pack_hash, signing_key)
    signature = signed[:ED25519_SIGNATURE_SIZE]

    output_path = Path(output_path)
    tmp_path = output_path.with_name(output_path.name + ".tmp")
    with open(tmp_path, "wb") as f:
        f.write(hashed)
        f.write(pack_hash)
        f.write(signature)
    tmp_path.replace(output_path)
