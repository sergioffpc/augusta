"""Byte-level reader primitives for the pack's container.

They are reader.py's, for its container parsing - the little-endian,
length-prefixed-string encoding (ADR-0007) src/modules/assets/wire_format.h
defines on the C++ side, whose ByteReader this mirrors. Packs are written by
augusta_assets itself, through the native module (ADR-0030); the reader here
only covers the container (header, index, trailer) for inspection and
verification.
"""

import struct


class WireError(ValueError):
    """Raised when a read runs past the buffer's end or a string is too long."""


class ByteReader:
    """Reads wire_format.h's own field order from a buffer.

    Fails with WireError rather than ever reading past the buffer's end.
    """

    def __init__(self, data: bytes) -> None:
        self._data = data
        self._pos = 0

    def _take(self, count: int) -> bytes:
        if count > len(self._data) - self._pos:
            raise WireError("unexpected end of data")
        chunk = self._data[self._pos : self._pos + count]
        self._pos += count
        return chunk

    def raw(self, count: int) -> bytes:
        """Reads the next count bytes as they are."""
        return self._take(count)

    def u8(self) -> int:
        """Reads an unsigned 8-bit integer."""
        return struct.unpack("<B", self._take(1))[0]

    def u32(self) -> int:
        """Reads a little-endian unsigned 32-bit integer."""
        return struct.unpack("<I", self._take(4))[0]

    def u64(self) -> int:
        """Reads a little-endian unsigned 64-bit integer."""
        return struct.unpack("<Q", self._take(8))[0]

    def string(self, max_length: int) -> str:
        """The read-side counterpart of ByteWriter.string."""
        length = self.u32()
        if length > max_length:
            raise WireError(
                f"string length {length} exceeds max length {max_length}"
            )
        return self._take(length).decode("utf-8")
