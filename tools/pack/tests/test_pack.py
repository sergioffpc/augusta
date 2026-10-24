"""Writing a pack through augusta_assets' encoder, and loading it back.

augusta_assets' own tests pin the blobs' and the container's bytes (ADR-0031)
and every reason a pack is refused; these check what reaches the cooker through
its binding (ADR-0030): a pack it writes loads back, the encoder's refusals
arrive as EncodeError and WriteError with no pack left behind, and a refused
load as LoadError with its reason. The signature is what makes a pack trusted
(ADR-0018).
"""

from augusta_pack import _native
from augusta_pack import keys
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


def test_a_written_pack_loads_back_every_entry(tmp_path, key_pair):
    path = tmp_path / "test.apak"
    pack_hash = _native.write_pack(path, ENTRIES, key_pair.private_key)

    contents = read_pack_contents(path, key_pair.public_key)
    assert contents.hash == pack_hash
    assert contents.entries == {
        entry.path: (entry.type, entry.data) for entry in ENTRIES
    }


def test_a_pack_names_the_client_pack_it_was_written_with_and_none_otherwise(
    tmp_path, key_pair
):
    client_hash = _native.write_pack(
        tmp_path / "client.apak", ENTRIES, key_pair.private_key
    )
    _native.write_pack(
        tmp_path / "server.apak", ENTRIES, key_pair.private_key, client_hash
    )

    server = read_pack_contents(tmp_path / "server.apak", key_pair.public_key)
    client = read_pack_contents(tmp_path / "client.apak", key_pair.public_key)
    assert server.client_pack_hash == client_hash
    assert client.client_pack_hash is None


def test_a_generated_keypair_signs_a_pack_its_public_key_loads(tmp_path):
    public_key, private_key = _native.generate_ed25519_keypair()
    path = tmp_path / "test.apak"
    pack_hash = _native.write_pack(path, ENTRIES, private_key)

    assert len(public_key) == keys.PUBLIC_KEY_SIZE
    assert len(private_key) == keys.PRIVATE_KEY_SIZE
    assert _native.load_pack(path, public_key).hash == pack_hash


def test_a_refused_pack_raises_load_error_with_its_reason(tmp_path, key_pair):
    path = tmp_path / "test.apak"
    _native.write_pack(path, ENTRIES, key_pair.private_key)
    other_public_key, _ = keys.generate_keypair()

    with pytest.raises(_native.LoadError) as refused:
        _native.load_pack(path, other_public_key)
    assert refused.value.reason == _native.LoadErrorReason.SIGNATURE_INVALID


def test_a_pack_of_another_format_names_both_versions(tmp_path, key_pair):
    path = tmp_path / "test.apak"
    _native.write_pack(path, ENTRIES, key_pair.private_key)
    pack = bytearray(path.read_bytes())
    # The format version: the little-endian u32 after the 4-byte magic.
    pack[4] = 2
    path.write_bytes(pack)

    with pytest.raises(
        _native.LoadError, match="is pack format 2; this build reads format 4$"
    ) as refused:
        _native.load_pack(path, key_pair.public_key)
    assert refused.value.reason == _native.LoadErrorReason.UNSUPPORTED_VERSION


def test_two_entries_with_the_same_path_are_refused(tmp_path, key_pair):
    duplicate = [ENTRIES[0], ENTRIES[0]]

    with pytest.raises(_native.WriteError, match="share a path") as refused:
        _native.write_pack(
            tmp_path / "test.apak", duplicate, key_pair.private_key
        )
    assert refused.value.reason == _native.WriteErrorReason.DUPLICATE_PATH
    assert list(tmp_path.iterdir()) == []


def test_a_blob_over_its_limit_is_refused():
    characters = [f"c{i}" for i in range(_native.MAX_CHARACTERS + 1)]

    with pytest.raises(_native.EncodeError, match="exceeds") as refused:
        _native.encode_characters_blob(characters)
    assert refused.value.reason == _native.EncodeErrorReason.TOO_LARGE


@pytest.mark.parametrize(
    ("bits_per_sample", "samples", "reason"),
    [
        (12, bytes(6), _native.EncodeErrorReason.UNSUPPORTED_SAMPLE_WIDTH),
        (16, bytes(3), _native.EncodeErrorReason.PARTIAL_SAMPLE),
    ],
)
def test_a_sound_the_runtime_could_not_play_is_refused(
    bits_per_sample, samples, reason
):
    sound = _native.AudioData(
        sample_rate=22050, bits_per_sample=bits_per_sample, samples=samples
    )

    with pytest.raises(_native.EncodeError, match="^audio ") as refused:
        _native.encode_audio_blob(sound)
    assert refused.value.reason == reason
