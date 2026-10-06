"""augusta-keygen's key files (ADR-0018): raw, fixed-size, and usable to sign
and verify a pack.
"""

import pytest
from pack import keys, pack
from pack.reader import verify_pack


def test_keygen_writes_a_keypair_that_signs_and_verifies_a_pack(tmp_path):
    pub_path, key_path = keys.write_keypair(tmp_path / "test")

    assert pub_path.stat().st_size == keys.PUBLIC_KEY_SIZE
    assert key_path.stat().st_size == keys.PRIVATE_KEY_SIZE
    pack_path = tmp_path / "test.pack"
    pack.write_pack(pack_path, [], keys.read_private_key(key_path))
    verify_pack(pack_path, keys.read_public_key(pub_path))


def test_a_key_file_of_the_wrong_size_is_refused(tmp_path):
    path = tmp_path / "short.key"
    path.write_bytes(bytes(keys.PRIVATE_KEY_SIZE - 1))

    with pytest.raises(ValueError, match="64-byte"):
        keys.read_private_key(path)
    with pytest.raises(ValueError, match="32-byte"):
        keys.read_public_key(path)
