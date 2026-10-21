"""Reading a Match capture (ADR-0050) for `augusta-inspect`.

The read side of src/modules/capture, which reads and writes it: the capture's
magic, then its format version in one byte, then each record after its length
in one byte (#463), the header first. Each record is one augusta.CaptureRecord,
read through augusta_pb2, the module generated from the protocol's one schema
(ADR-0007), so this reader never copies the format by hand. A Command is only
counted.
"""

import dataclasses
import datetime

from google.protobuf import message as protobuf_message

from augusta_pack import augusta_pb2

MAGIC = b"AUGCAP\r\n"
# The format version this reader reads (protocol::kCaptureFormatVersion).
FORMAT_VERSION = 3

# A position travels as a signed count of 1/1024 m (augusta/grid.h's
# kPositionGrid).
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


def _read_format_version(data: bytes) -> int:
    """The byte after the magic, refused unless it is this reader's version.

    A capture before version 3 holds its version inside its header record, so
    the byte after its magic is that record's length, and it is refused here.
    """
    if len(data) <= len(MAGIC):
        raise CaptureError("the capture has no format version")
    version = data[len(MAGIC)]
    if version != FORMAT_VERSION:
        raise CaptureError(
            f"capture format version {version}, this reader reads "
            f"{FORMAT_VERSION}"
        )
    return version


def _frames(data: bytes) -> tuple[list[bytes], bool]:
    """Every whole record after the version, and whether the last is torn."""
    records = []
    at = len(MAGIC) + 1
    while at < len(data):
        length = data[at]
        if at + 1 + length > len(data):
            return records, True
        records.append(data[at + 1 : at + 1 + length])
        at += 1 + length
    return records, False


def _parse(payload: bytes) -> augusta_pb2.CaptureRecord:
    try:
        return augusta_pb2.CaptureRecord.FromString(payload)
    except protobuf_message.DecodeError as error:
        raise CaptureError("a record does not parse") from error


def _text(field: bytes) -> str:
    """A text field, which the schema holds as bytes of UTF-8."""
    try:
        return field.decode("utf-8")
    except UnicodeDecodeError as error:
        raise CaptureError("a record's text is not UTF-8") from error


def _read_header(payload: bytes, format_version: int) -> CaptureHeader:
    record = _parse(payload)
    if record.WhichOneof("record") != "header":
        raise CaptureError("the capture does not start with its header")
    header = record.header
    return CaptureHeader(
        format_version=format_version,
        engine_version=_text(header.engine_version),
        server_pack=header.server_pack,
        client_pack=header.client_pack,
        tick_rate_hz=header.tick_rate_hz,
        started=datetime.datetime.fromtimestamp(
            header.started_unix_ms / 1000, tz=datetime.UTC
        ),
    )


def read_capture(data: bytes) -> Capture:
    """Reads the capture data holds.

    Raises:
        CaptureError: data is no capture, is of another format version, or a
            record is malformed.
    """
    if not is_capture(data):
        raise CaptureError("the file does not start with a capture's magic")
    format_version = _read_format_version(data)
    records, torn = _frames(data)
    if not records:
        raise CaptureError("the capture has no header")
    capture = Capture(
        header=_read_header(records[0], format_version),
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
        record = _parse(payload)
        kind = record.WhichOneof("record")
        if kind is None:
            raise CaptureError("a record is of no type this reader knows")
        if kind == "header":
            raise CaptureError("the capture has a second header")
        event = getattr(record, kind)
        offset = event.offset
        # As src/modules/capture/capture.cpp's RecordOrder: Joins first, at
        # 0, in order, then the events in tick order, and nothing after the
        # Match end.
        if capture.ended or offset < capture.last_offset:
            raise CaptureError("a record is out of order")
        capture.last_offset = offset
        if kind == "join":
            if event.player != len(capture.players) + 1 or offset != 0:
                raise CaptureError(f"player {event.player} joins out of order")
            capture.players.append(
                CapturedPlayer(
                    number=event.player,
                    session=event.session,
                    character=_text(event.character),
                    spawn=(
                        event.spawn_x * _POSITION_STEP,
                        event.spawn_y * _POSITION_STEP,
                        event.spawn_z * _POSITION_STEP,
                    ),
                )
            )
        elif kind == "command":
            player(event.player).commands += 1
        elif kind == "leave":
            player(event.player).left = offset
        elif kind == "death":
            victim = player(event.victim)
            victim.died = offset
            victim.killer = player(event.killer).number
        elif kind == "match_end":
            capture.ended = True
            capture.winner = (
                player(event.winner).number if event.winner else None
            )
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
