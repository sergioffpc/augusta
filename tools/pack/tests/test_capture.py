"""augusta-inspect on a Match capture (ADR-0050).

The C++ half writes captures (src/server/capture.cpp); the capture_read fuzz
target's seed is one it wrote, so reading it here holds both halves to one
format.
"""

from conftest import EXAMPLES_ROOT
from pack import capture
from pack import pack_cli
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
        (1, 7, "soldier"),
        (2, 8, "sniper"),
    ]
    assert read.players[1].spawn == (-3.0, 0.0, 4.0)
    assert [p.commands for p in read.players] == [1, 1]
    assert (read.players[1].died, read.players[1].killer) == (5, 1)
    assert read.players[1].left == 6
    assert read.ended and read.winner == 1
    assert read.last_offset == 6
    assert not read.torn


def test_a_capture_cut_short_drops_and_reports_its_last_record():
    read = capture.read_capture(SEED.read_bytes()[:-1])

    assert read.torn
    assert not read.ended


def test_another_format_version_is_refused():
    data = bytearray(SEED.read_bytes())
    # After the magic, the header's one-byte length and its type.
    data[len(capture.MAGIC) + 2] = capture.FORMAT_VERSION + 1

    with pytest.raises(capture.CaptureError):
        capture.read_capture(bytes(data))


def test_inspect_prints_a_capture_it_tells_from_a_pack(capsys, tmp_path):
    path = tmp_path / "match.capture"
    path.write_bytes(SEED.read_bytes())

    status = pack_cli.inspect_main([str(path)])

    out = capsys.readouterr().out
    assert status == 0
    assert "Match capture" in out
    assert "length          7 ticks, 0.12 s" in out
    assert (
        "  2  session 8  sniper  spawn (-3, 0, 4)  1 commands  "
        "died at 5, killed by 1  left at 6" in out
    )
    assert "Match end  at 6, won by 1" in out


def test_a_record_after_the_match_end_is_refused():
    # A Leave of player 1 at offset 9: type, offset, player, after its length.
    data = SEED.read_bytes() + bytes([6, 4, 9, 0, 0, 0, 1])

    with pytest.raises(capture.CaptureError):
        capture.read_capture(data)
