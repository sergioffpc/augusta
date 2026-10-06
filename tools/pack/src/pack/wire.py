"""Byte-level writer/reader primitives for the pack wire format.

They are shared by pack.py's blob encoders and pack container assembly, and
reader.py's container parsing - the little-endian, length-prefixed-string
encoding (ADR-0007) src/modules/assets/wire_format.h defines on the C++ side
(that module's ByteReader/Append* is this project's validated reference). The
runtime still decodes blobs in C++; the reader here only covers the container
(header, index, trailer) for inspection and verification.
"""

import struct


class ByteWriter:
    """Accumulates a pack/blob's bytes in wire_format.h's own field order."""

    def __init__(self) -> None:
        self._buf = bytearray()

    def getvalue(self) -> bytes:
        """The bytes written so far."""
        return bytes(self._buf)

    def __len__(self) -> int:
        return len(self._buf)

    def u8(self, value: int) -> None:
        """Writes value as an unsigned 8-bit integer."""
        self._buf += struct.pack("<B", value)

    def u32(self, value: int) -> None:
        """Writes value as a little-endian unsigned 32-bit integer."""
        self._buf += struct.pack("<I", value)

    def u64(self, value: int) -> None:
        """Writes value as a little-endian unsigned 64-bit integer."""
        self._buf += struct.pack("<Q", value)

    def f32(self, value: float) -> None:
        """Writes value as a little-endian 32-bit float."""
        self._buf += struct.pack("<f", value)

    def raw(self, data: bytes) -> None:
        """Writes data as it is, with no length prefix."""
        self._buf += data

    def string(self, text: str, max_length: int) -> None:
        """Writes a length-prefixed string (name/path/property key or value).

        The write-side counterpart of wire_format.h's ByteReader::ReadString.
        """
        encoded = text.encode("utf-8")
        if len(encoded) > max_length:
            raise ValueError(
                f"string exceeds max length {max_length}: {text!r}"
            )
        self.u32(len(encoded))
        self.raw(encoded)

    def optional_path(self, path: str | None, max_length: int) -> None:
        """Writes path as a string, or nothing if it is None."""
        if path is not None:
            self.string(path, max_length)


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
