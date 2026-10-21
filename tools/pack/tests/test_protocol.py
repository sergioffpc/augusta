"""The protocol's Python module, generated from its one schema (ADR-0007).

The root build generates augusta_pack.augusta_pb2 from
src/modules/protocol/proto/augusta.proto, the file the C++ is generated from,
so the bytes asserted here are the ones the C++ writes for the same record.
"""

from augusta_pack import augusta_pb2

# A Leave of player 2 at offset 6: CaptureRecord's field 1004 (tag 8034 as a
# varint), 4 bytes long, holding offset = 6 (field 1) and player = 2 (field 2).
LEAVE_BYTES = b"\xe2\x3e\x04\x08\x06\x10\x02"


def test_a_capture_record_encodes_as_the_schema_numbers_it():
    record = augusta_pb2.CaptureRecord(
        leave=augusta_pb2.CapturedLeave(offset=6, player=2)
    )

    assert record.SerializeToString() == LEAVE_BYTES


def test_a_capture_record_reads_back_as_it_was_written():
    record = augusta_pb2.CaptureRecord()
    record.header.server_pack = bytes([1]) * 32
    record.header.client_pack = bytes([2]) * 32
    record.header.engine_version = "5.0.0"
    record.header.started_unix_ms = 1_791_281_700_123
    record.header.tick_rate_hz = 60

    read = augusta_pb2.CaptureRecord.FromString(record.SerializeToString())

    assert read.WhichOneof("record") == "header"
    assert read.header.server_pack == bytes([1]) * 32
    assert read.header.client_pack == bytes([2]) * 32
    assert read.header.engine_version == "5.0.0"
    assert read.header.started_unix_ms == 1_791_281_700_123
    assert read.header.tick_rate_hz == 60


def test_a_leave_reads_from_its_bytes():
    read = augusta_pb2.CaptureRecord.FromString(LEAVE_BYTES)

    assert read.WhichOneof("record") == "leave"
    assert (read.leave.offset, read.leave.player) == (6, 2)
