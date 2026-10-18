"""Writing a pack through augusta_assets' encoder, and verifying it.

augusta_assets' own tests pin the blobs' and the container's bytes (ADR-0031);
these check what reaches the cooker through its binding (ADR-0030): a pack it
writes verifies and reads back, and the encoder's refusals arrive as
EncodeError and WriteError with no pack left behind. The signature is what
makes a pack trusted (ADR-0018).
"""

from augusta_pack import _native
from augusta_pack import keys
from augusta_pack import pack
from augusta_pack.reader import PackError
from augusta_pack.reader import read_pack
from augusta_pack.reader import verify_pack
from conftest import read_pack_contents
import pytest

ENTRIES = [
    _native.AssetEntry(
        type=_native.AssetType.SCRIPT,
        path="parameters.lua",
        data=b"return {}",
    ),
    _native.AssetEntry(
        type=_native.AssetType.CHARACTERS,
        path=_native.CHARACTERS_PATH,
        data=_native.encode_characters_blob([]),
    ),
]


def test_a_written_pack_verifies_and_reads_back_every_entry(tmp_path, key_pair):
    path = tmp_path / "test.pack"
    pack_hash = _native.write_pack(path, ENTRIES, key_pair.private_key)

    contents = read_pack_contents(path, key_pair.public_key)
    assert contents.hash == pack_hash
    assert contents.entries == {
        entry.path: (int(entry.type), entry.data) for entry in ENTRIES
    }


def test_a_modified_byte_is_a_hash_mismatch(tmp_path, key_pair):
    path = tmp_path / "test.pack"
    _native.write_pack(path, ENTRIES, key_pair.private_key)
    data = bytearray(path.read_bytes())
    data[pack.HEADER_SIZE] ^= 0xFF
    path.write_bytes(data)

    with pytest.raises(PackError, match="content hash mismatch"):
        verify_pack(path, key_pair.public_key)


def test_a_pack_names_the_client_pack_it_was_written_with_and_none_otherwise(
    tmp_path, key_pair
):
    client_hash = _native.write_pack(
        tmp_path / "client.pack", ENTRIES, key_pair.private_key
    )
    _native.write_pack(
        tmp_path / "server.pack", ENTRIES, key_pair.private_key, client_hash
    )

    assert (
        verify_pack(
            tmp_path / "server.pack", key_pair.public_key
        ).client_pack_hash
        == client_hash
    )
    assert (
        verify_pack(
            tmp_path / "client.pack", key_pair.public_key
        ).client_pack_hash
        is None
    )


def test_a_pack_signed_by_another_key_does_not_verify(tmp_path, key_pair):
    path = tmp_path / "test.pack"
    _native.write_pack(path, ENTRIES, key_pair.private_key)
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

    with pytest.raises(_native.WriteError, match="share a path"):
        _native.write_pack(
            tmp_path / "test.pack", duplicate, key_pair.private_key
        )
    assert list(tmp_path.iterdir()) == []


def test_a_blob_over_its_limit_is_refused():
    characters = [f"c{i}" for i in range(_native.MAX_CHARACTERS + 1)]

    with pytest.raises(_native.EncodeError, match="exceeds"):
        _native.encode_characters_blob(characters)
