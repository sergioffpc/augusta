"""Reading a Match capture (ADR-0050) for `augusta-inspect`.

The read side of src/server/capture.cpp, which writes it, and
src/modules/protocol's capture records: the capture's magic, then each record
after its length in one byte (#463), the header first. A Command's contents are
counted, never decoded, so a capture's size does not slow the reader; every
other record is decoded in full.
"""

import dataclasses
import datetime
import struct

MAGIC = b"AUGCAP\r\n"
# The format version this reader reads (protocol::kCaptureFormatVersion).
FORMAT_VERSION = 2

_HEADER = 1
_JOIN = 2
_COMMAND = 3
_LEAVE = 4
_DEATH = 5
_MATCH_END = 6

_PACK_HASH_SIZE = 32
# A position travels as a signed count of 1/1024 in 3 bytes (augusta/grid.h's
# kPositionGrid).
_POSITION_BYTES = 3
_POSITION_STEP = 1.0 / 1024.0


class CaptureError(ValueError):
    """The file is not a capture this reader reads, or a record is malformed."""


@dataclasses.dataclass
class CapturedPlayer:
    """One player of the Match start and what it did."""

    number: int
    session: int
    character: str
    spawn: tuple[float, float, float]
    commands: int = 0
    # The offset of its Leave, its Death and the number of whoever killed it.
    left: int | None = None
    died: int | None = None
    killer: int | None = None


@dataclasses.dataclass
class CaptureHeader:
    """What a captured Match ran on and when it started."""

    format_version: int
    engine_version: str
    server_pack: bytes
    client_pack: bytes
    tick_rate_hz: int
    started: datetime.datetime


@dataclasses.dataclass
class Capture:
    """A capture's header and what its records say."""

    header: CaptureHeader
    players: list[CapturedPlayer]
    # The offset of the last record, its Match end's if it has one.
    last_offset: int
    ended: bool
    # The winner's number, None for a Draw or a capture with no Match end.
    winner: int | None
    # Whether the file ends partway through a record, which was dropped.
    torn: bool


def is_capture(data: bytes) -> bool:
    """Whether data starts with a capture's magic."""
    return data.startswith(MAGIC)


class _Reader:
    """Reads one record's fields front to back."""

    def __init__(self, payload: bytes) -> None:
        self._payload = payload
        self._at = 0

    def take(self, size: int) -> bytes:
        if self._at + size > len(self._payload):
            raise CaptureError("a record ends before its fields do")
        taken = self._payload[self._at : self._at + size]
        self._at += size
        return taken

    def u8(self) -> int:
        return self.take(1)[0]

    def u32(self) -> int:
        return struct.unpack("<I", self.take(4))[0]

    def i32(self) -> int:
        return struct.unpack("<i", self.take(4))[0]

    def i64(self) -> int:
        return struct.unpack("<q", self.take(8))[0]

    def string(self) -> str:
        return self.take(self.u8()).decode("utf-8", errors="replace")

    def position(self) -> tuple[float, float, float]:
        def axis() -> float:
            steps = int.from_bytes(
                self.take(_POSITION_BYTES), "little", signed=True
            )
            return steps * _POSITION_STEP

        return (axis(), axis(), axis())

    def end(self) -> None:
        if self._at != len(self._payload):
            raise CaptureError("bytes remain after a record")


def _frames(data: bytes) -> tuple[list[bytes], bool]:
    """Every whole record after the magic, and whether the last was cut off."""
    records = []
    at = len(MAGIC)
    while at < len(data):
        length = data[at]
        if at + 1 + length > len(data):
            return records, True
        records.append(data[at + 1 : at + 1 + length])
        at += 1 + length
    return records, False


def _read_header(payload: bytes) -> CaptureHeader:
    reader = _Reader(payload)
    if reader.u8() != _HEADER:
        raise CaptureError("the capture does not start with its header")
    format_version = reader.u8()
    if format_version != FORMAT_VERSION:
        raise CaptureError(
            f"capture format version {format_version}, this reader reads "
            f"{FORMAT_VERSION}"
        )
    header = CaptureHeader(
        format_version=format_version,
        engine_version=reader.string(),
        server_pack=reader.take(_PACK_HASH_SIZE),
        client_pack=reader.take(_PACK_HASH_SIZE),
        tick_rate_hz=reader.u8(),
        started=datetime.datetime.fromtimestamp(
            reader.i64() / 1000, tz=datetime.UTC
        ),
    )
    reader.end()
    return header


def read_capture(data: bytes) -> Capture:
    """Reads the capture data holds.

    Raises:
        CaptureError: data is no capture, or a record is malformed.
    """
    if not is_capture(data):
        raise CaptureError("the file does not start with a capture's magic")
    records, torn = _frames(data)
    if not records:
        raise CaptureError("the capture has no header")
    capture = Capture(
        header=_read_header(records[0]),
        players=[],
        last_offset=0,
        ended=False,
        winner=None,
        torn=torn,
    )

    def player(number: int) -> CapturedPlayer:
        if not 1 <= number <= len(capture.players):
            raise CaptureError(
                f"a record names player {number}, who never joined"
            )
        return capture.players[number - 1]

    for payload in records[1:]:
        reader = _Reader(payload)
        kind = reader.u8()
        offset = reader.u32()
        # As src/server/capture.cpp's Follows: Joins first, at 0, in order,
        # then the events in tick order, and nothing after the Match end.
        if capture.ended or offset < capture.last_offset:
            raise CaptureError("a record is out of order")
        capture.last_offset = offset
        if kind == _JOIN:
            number = reader.u8()
            if number != len(capture.players) + 1 or offset != 0:
                raise CaptureError(f"player {number} joins out of order")
            capture.players.append(
                CapturedPlayer(
                    number=number,
                    session=reader.u32(),
                    character=reader.string(),
                    spawn=reader.position(),
                )
            )
            reader.end()
        elif kind == _COMMAND:
            player(reader.u8()).commands += 1
        elif kind == _LEAVE:
            player(reader.u8()).left = offset
            reader.end()
        elif kind == _DEATH:
            victim = player(reader.u8())
            victim.died = offset
            victim.killer = player(reader.u8()).number
            reader.end()
        elif kind == _MATCH_END:
            winner = reader.u8()
            capture.ended = True
            capture.winner = player(winner).number if winner else None
            reader.end()
        else:
            raise CaptureError(f"unknown capture record type {kind}")
    return capture


def format_capture(capture: Capture) -> list[str]:
    """What `augusta-inspect` prints for capture, line by line."""
    header = capture.header
    ticks = capture.last_offset + 1
    seconds = ticks / header.tick_rate_hz if header.tick_rate_hz else 0.0
    lines = [
        "Match capture",
        f"  format version  {header.format_version}",
        f"  engine version  {header.engine_version}",
        f"  server pack     {header.server_pack.hex()}",
        f"  client pack     {header.client_pack.hex()}",
        f"  tick rate       {header.tick_rate_hz} Hz",
        "  started         "
        + header.started.isoformat(timespec="milliseconds"),
        f"  length          {ticks} ticks, {seconds:.2f} s",
        "",
        f"Players ({len(capture.players)}; --player takes the number)",
    ]
    for played in capture.players:
        x, y, z = played.spawn
        line = (
            f"  {played.number}  session {played.session}  "
            f"{played.character}  spawn ({x:g}, {y:g}, {z:g})  "
            f"{played.commands} commands"
        )
        if played.died is not None:
            line += f"  died at {played.died}, killed by {played.killer}"
        if played.left is not None:
            line += f"  left at {played.left}"
        lines.append(line)
    lines.append("")
    if not capture.ended:
        lines.append("Match end  none: the capture stopped before it")
    elif capture.winner is None:
        lines.append(f"Match end  at {capture.last_offset}, a Draw")
    else:
        lines.append(
            f"Match end  at {capture.last_offset}, won by {capture.winner}"
        )
    lines.append(
        "Torn       yes: its last record was cut short and dropped"
        if capture.torn
        else "Torn       no"
    )
    return lines
