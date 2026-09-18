"""Byte-level writer primitives shared by pack.py's blob encoders and pack
container assembly - the little-endian, length-prefixed-string encoding
(ADR-0007) src/modules/assets/wire_format.h defines on the C++ side (that
module's ByteReader/Append* is this project's validated reference). Write-
only: this project never reads packs back, only writes them - the runtime
decodes, in C++.
"""

import struct


class ByteWriter:
    """Accumulates a pack/blob's bytes in wire_format.h's own field order."""

    def __init__(self) -> None:
        self._buf = bytearray()

    def bytes(self) -> bytes:
        return bytes(self._buf)

    def __len__(self) -> int:
        return len(self._buf)

    def u8(self, value: int) -> None:
        self._buf += struct.pack("<B", value)

    def u32(self, value: int) -> None:
        self._buf += struct.pack("<I", value)

    def u64(self, value: int) -> None:
        self._buf += struct.pack("<Q", value)

    def f32(self, value: float) -> None:
        self._buf += struct.pack("<f", value)

    def raw(self, data: bytes) -> None:
        self._buf += data

    def string(self, text: str, max_length: int) -> None:
        """A length-prefixed string (name/path/property key or value) -
        the write-side counterpart of wire_format.h's ByteReader::ReadString.
        """
        encoded = text.encode("utf-8")
        if len(encoded) > max_length:
            raise ValueError(f"string exceeds max length {max_length}: {text!r}")
        self.u32(len(encoded))
        self.raw(encoded)

    def optional_path(self, path: str | None, max_length: int) -> None:
        if path is not None:
            self.string(path, max_length)
