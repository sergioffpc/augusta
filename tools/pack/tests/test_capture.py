"""augusta-inspect on a Match capture (ADR-0050).

The C++ half writes captures (src/modules/capture); the capture_read fuzz
target's seed is one it wrote, so reading it here holds both halves to one
format.
"""

from augusta_pack import augusta_pb2
from augusta_pack import capture
from augusta_pack import pack_cli
from conftest import EXAMPLES_ROOT
import pytest

SEED = (
    EXAMPLES_ROOT.parent.parent.parent
    / "tests"
    / "fuzz"
    / "corpus"
    / "capture_read"
    / "match"
)


def test_a_capture_the_server_wrote_reads_as_it_was_played():
    read = capture.read_capture(SEED.read_bytes())

    assert read.header.engine_version == "2.0.1"
    assert read.header.server_pack == bytes([1]) * 32
    assert read.header.client_pack == bytes([2]) * 32
    assert read.header.tick_rate_hz == 60
    assert read.header.started.isoformat() == "2026-10-09T10:15:00.123000+00:00"
    assert [(p.number, p.session, p.character) for p in read.players] == [
        (1, "0190a1b2-c3d4-7000-8000-000000000007", "soldier"),
        (2, "0190a1b2-c3d4-7000-8000-000000000008", "sniper"),
    ]
    assert read.players[1].spawn == (-3.0, 0.0, 4.0)
    assert [p.commands for p in read.players] == [1, 1]
    assert (read.players[1].died, read.players[1].killer) == (5, 1)
    assert read.players[1].left == 6
    assert read.ended and read.winner == 1
    assert read.last_offset == 6
    assert not read.torn


def test_the_seed_holds_every_record_kind_the_schema_has():
    # So the test above reads every kind, and the reader, which refuses a
    # kind it does not name, still names every member of the oneof.
    data = SEED.read_bytes()
    kinds = set()
    at = len(capture.MAGIC) + 1
    while at < len(data):
        length = data[at]
        record = augusta_pb2.CaptureRecord.FromString(
            data[at + 1 : at + 1 + length]
        )
        kinds.add(record.WhichOneof("record"))
        at += 1 + length
    oneof = augusta_pb2.CaptureRecord.DESCRIPTOR.oneofs_by_name["record"]

    assert kinds == {field.name for field in oneof.fields}


def test_a_capture_cut_short_drops_and_reports_its_last_record():
    read = capture.read_capture(SEED.read_bytes()[:-1])

    assert read.torn
    assert not read.ended


def test_another_format_version_is_refused():
    data = bytearray(SEED.read_bytes())
    # The one byte after the magic.
    data[len(capture.MAGIC)] = 4

    with pytest.raises(capture.CaptureError, match="format version 4"):
        capture.read_capture(bytes(data))


def test_a_version_2_capture_is_refused():
    # A v2 file has no version byte: its magic is followed by its header
    # record's length (81), then the record: its type 1, its format version 2,
    # then the 79 bytes of the fields v3 keeps.
    record = bytes([1, 2]) + bytes(79)
    data = capture.MAGIC + bytes([len(record)]) + record

    with pytest.raises(capture.CaptureError, match="format version 81"):
        capture.read_capture(data)


def test_text_that_is_not_utf8_is_refused():
    header = augusta_pb2.CaptureRecord(
        header=augusta_pb2.CaptureHeader(engine_version=b"\xc3\x28")
    ).SerializeToString()
    data = capture.MAGIC + bytes([capture.FORMAT_VERSION, len(header)]) + header

    with pytest.raises(capture.CaptureError, match="not UTF-8"):
        capture.read_capture(data)


def test_inspect_prints_a_capture_it_tells_from_a_pack(capsys, tmp_path):
    path = tmp_path / "match.acap"
    path.write_bytes(SEED.read_bytes())

    status = pack_cli.inspect_main([str(path)])

    out = capsys.readouterr().out
    assert status == 0
    assert "Match capture" in out
    assert "format version  3" in out
    assert "length          7 ticks, 0.12 s" in out
    assert (
        "  2  session 0190a1b2-c3d4-7000-8000-000000000008  sniper  "
        "spawn (-3, 0, 4)  1 commands  "
        "died at 5, killed by 1  left at 6" in out
    )
    assert "Match end  at 6, won by 1" in out


def test_a_record_after_the_match_end_is_refused():
    leave = augusta_pb2.CaptureRecord(
        leave=augusta_pb2.CapturedLeave(offset=9, player=1)
    ).SerializeToString()
    data = SEED.read_bytes() + bytes([len(leave)]) + leave

    with pytest.raises(capture.CaptureError):
        capture.read_capture(data)
