"""The pack container's constants the Python reader (reader.py) parses with.

Packs are written by augusta_assets' encoder, through the native module
(ADR-0030); these are kept, matching wire_format.h and assets.cpp, only for the
container reader behind augusta-inspect and augusta-verify (ADR-0031).
"""

MAX_PATH_LENGTH = 4096
MAX_ENTRIES = 1 << 20

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
ASSET_TYPE_EYE = 9
ASSET_TYPE_SOUNDS = 10

MAGIC = b"AUGP"
FORMAT_VERSION = 3
BLAKE3_HASH_SIZE = 32
# The header's flags byte (assets.cpp's kHeaderHasClientPack): whether it names
# the hash of the client pack cooked with this one.
HEADER_HAS_CLIENT_PACK = 1 << 0
# magic(4) + version(u32=4) + data_offset(u64=8) + index_offset(u64=8) +
# index_count(u32=4) + flags(u8=1) + client pack hash(32) - assets.cpp's
# kHeaderSize.
HEADER_SIZE = 4 + 4 + 8 + 8 + 4 + 1 + BLAKE3_HASH_SIZE
ED25519_SIGNATURE_SIZE = 64
