"""augusta-inspect and augusta-verify on packs (ADR-0031).

Both read packs through augusta_assets (ADR-0030): inspect shows a pack's
header, index and trailer without a key, and verify accepts exactly the packs
the client and server load, naming why it refused one with the runtime's own
phrase for it.
"""

from augusta_pack import _native
from augusta_pack import keys
from augusta_pack import pack_cli
import pytest

ENTRIES = [
    _native.AssetEntry(
        type=_native.AssetType.SCRIPT, path="parameters.lua", data=b"return {}"
    ),
    _native.AssetEntry(
        type=_native.AssetType.SCRIPT, path="rules.lua", data=b"x"
    ),
]


@pytest.fixture
def signed_pack(tmp_path, key_pair):
    """A pack of ENTRIES signed by key_pair, its public key file and hash."""
    path = tmp_path / "test.apak"
    pack_hash = _native.write_pack(path, ENTRIES, key_pair.private_key)
    public_key = tmp_path / "test.pub"
    public_key.write_bytes(key_pair.public_key)
    return path, public_key, pack_hash


def test_inspect_shows_the_header_index_and_trailer(capsys, signed_pack):
    path, _, pack_hash = signed_pack
    signature = path.read_bytes()[-64:]

    status = pack_cli.inspect_main([str(path)])

    assert status == 0
    # A 61-byte header, the two blobs, an index of 21 bytes per entry plus
    # its path, and the 96-byte trailer.
    assert capsys.readouterr().out.splitlines() == [
        f"{path}: 232 bytes",
        "",
        "Header (offset 0, 61 bytes)",
        "  magic         AUGP",
        "  version       4",
        "  data offset   61",
        "  index offset  71",
        "  index count   2",
        "  client pack   none",
        "",
        "Data (offset 61, 10 bytes)",
        "",
        "Index (offset 71, 65 bytes, 2 entries)",
        "  script  61  9  parameters.lua",
        "  script  70  1  rules.lua",
        "",
        "Trailer (offset 136, 96 bytes) - not verified, see augusta-verify",
        f"  BLAKE3 hash   {pack_hash.hex()}",
        f"  signature     {signature.hex()}",
    ]


def test_inspect_shows_the_client_pack_a_server_pack_names(
    capsys, tmp_path, key_pair
):
    client_hash = _native.write_pack(
        tmp_path / "client.apak", [], key_pair.private_key
    )
    server = tmp_path / "server.apak"
    _native.write_pack(server, [], key_pair.private_key, client_hash)

    assert pack_cli.inspect_main([str(server)]) == 0

    assert (
        f"  client pack   {client_hash.hex()}"
        in capsys.readouterr().out.splitlines()
    )


def test_inspect_refuses_a_file_that_is_not_a_pack(capsys, tmp_path):
    path = tmp_path / "test.apak"
    path.write_bytes(b"NOPE" + bytes(200))

    assert pack_cli.inspect_main([str(path)]) == 1

    assert capsys.readouterr().err == (
        f"{path} is not an augusta pack file (bad magic)\n"
    )


def test_verify_accepts_a_pack_signed_by_the_given_key(capsys, signed_pack):
    path, public_key, pack_hash = signed_pack

    status = pack_cli.verify_main([str(path), "--public-key", str(public_key)])

    assert status == 0
    assert capsys.readouterr().out.splitlines() == [
        f"OK: {path}",
        "  2 entries, 232 bytes",
        f"  BLAKE3 {pack_hash.hex()}",
        f"  signed by {public_key}",
    ]


def test_verify_refuses_a_modified_byte(capsys, signed_pack):
    path, public_key, _ = signed_pack
    data = bytearray(path.read_bytes())
    # Inside the data section, right after the 61-byte header.
    data[62] ^= 0xFF
    path.write_bytes(data)

    status = pack_cli.verify_main([str(path), "--public-key", str(public_key)])

    assert status == 1
    assert capsys.readouterr().err == (
        f"{path} failed its integrity check (content hash mismatch)\n"
    )


def test_verify_refuses_another_keys_signature(capsys, tmp_path, signed_pack):
    path, _, _ = signed_pack
    other_public_key, _ = keys.generate_keypair()
    other = tmp_path / "other.pub"
    other.write_bytes(other_public_key)

    status = pack_cli.verify_main([str(path), "--public-key", str(other)])

    assert status == 1
    assert capsys.readouterr().err == f"{path} failed signature verification\n"


def test_verify_refuses_a_file_that_is_not_a_pack(
    capsys, tmp_path, signed_pack
):
    _, public_key, _ = signed_pack
    path = tmp_path / "not.apak"
    path.write_bytes(b"NOPE" + bytes(200))

    status = pack_cli.verify_main([str(path), "--public-key", str(public_key)])

    assert status == 1
    assert capsys.readouterr().err == (
        f"{path} is not an augusta pack file (bad magic)\n"
    )


def _verify_without_extension(tmp_path, file_name: str) -> int:
    """Writes a pack as tmp_path/file_name, then verifies tmp_path/game."""
    public_key_path, private_key_path = keys.write_keypair(tmp_path / "test")
    _native.write_pack(
        tmp_path / file_name, ENTRIES, keys.read_private_key(private_key_path)
    )
    return pack_cli.verify_main(
        [str(tmp_path / "game"), "--public-key", str(public_key_path)]
    )


def test_a_pack_named_without_its_extension_is_found_as_apak(tmp_path):
    assert _verify_without_extension(tmp_path, "game.apak") == 0


def test_the_retired_pack_extension_is_not_tried(tmp_path):
    assert _verify_without_extension(tmp_path, "game.pack") == 1
