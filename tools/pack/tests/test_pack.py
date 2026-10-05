"""write_pack and verify_pack: the container a pack is (ADR-0031) and the
signature that makes it trusted (ADR-0018).
"""

import pytest
from conftest import read_pack_contents

from pack import keys, pack
from pack.reader import PackError, read_pack, verify_pack

ENTRIES = [
    pack.AssetEntry(type=pack.ASSET_TYPE_SCRIPT, path="parameters.lua", data=b"return {}"),
    pack.AssetEntry(type=pack.ASSET_TYPE_CHARACTERS, path=pack.CHARACTERS_PATH, data=pack.encode_characters_blob([])),
]


def test_a_written_pack_verifies_and_reads_back_every_entry(tmp_path, key_pair):
    path = tmp_path / "test.pack"
    pack_hash = pack.write_pack(path, ENTRIES, key_pair.private_key)

    contents = read_pack_contents(path, key_pair.public_key)
    assert contents.hash == pack_hash
    assert contents.entries == {entry.path: (entry.type, entry.data) for entry in ENTRIES}


def test_a_modified_byte_is_a_hash_mismatch(tmp_path, key_pair):
    path = tmp_path / "test.pack"
    pack.write_pack(path, ENTRIES, key_pair.private_key)
    data = bytearray(path.read_bytes())
    data[pack.HEADER_SIZE] ^= 0xFF
    path.write_bytes(data)

    with pytest.raises(PackError, match="content hash mismatch"):
        verify_pack(path, key_pair.public_key)


def test_a_pack_names_the_client_pack_it_was_written_with_and_none_otherwise(tmp_path, key_pair):
    client_hash = pack.write_pack(tmp_path / "client.pack", ENTRIES, key_pair.private_key)
    pack.write_pack(tmp_path / "server.pack", ENTRIES, key_pair.private_key, client_hash)

    assert verify_pack(tmp_path / "server.pack", key_pair.public_key).client_pack_hash == client_hash
    assert verify_pack(tmp_path / "client.pack", key_pair.public_key).client_pack_hash is None


def test_a_client_pack_hash_of_the_wrong_size_is_refused(tmp_path, key_pair):
    with pytest.raises(pack.WriteError, match="client_pack_hash"):
        pack.write_pack(tmp_path / "test.pack", ENTRIES, key_pair.private_key, bytes(pack.BLAKE3_HASH_SIZE - 1))


def test_a_pack_signed_by_another_key_does_not_verify(tmp_path, key_pair):
    path = tmp_path / "test.pack"
    pack.write_pack(path, ENTRIES, key_pair.private_key)
    other_public_key, _ = keys.generate_keypair()

    with pytest.raises(PackError, match="signature is not valid"):
        verify_pack(path, other_public_key)


def test_a_file_that_is_not_a_pack_is_refused(tmp_path):
    path = tmp_path / "test.pack"
    path.write_bytes(b"NOPE" + bytes(200))

    with pytest.raises(PackError, match="bad magic"):
        read_pack(path)


def test_two_entries_with_the_same_path_are_refused(tmp_path, key_pair):
    duplicate = [ENTRIES[0], ENTRIES[0]]

    with pytest.raises(pack.WriteError, match="duplicate"):
        pack.write_pack(tmp_path / "test.pack", duplicate, key_pair.private_key)
    assert not (tmp_path / "test.pack").exists()


def test_a_signing_key_of_the_wrong_size_is_refused(tmp_path, key_pair):
    with pytest.raises(pack.WriteError, match="64 bytes"):
        pack.write_pack(tmp_path / "test.pack", ENTRIES, key_pair.public_key)


def test_a_blob_over_its_limit_is_refused():
    with pytest.raises(pack.EncodeError):
        pack.encode_script_blob(bytes(pack.MAX_SCRIPT_BYTES + 1))
