"""augusta-publish.

Which packs it agrees to publish, the folder it names, and that a folder on the
node is never overwritten. The node is a local folder standing in for the
asset-pack volume; ssh and scp themselves are not run.
"""

import hashlib
from pathlib import Path
import shutil

from pack import keys
from pack import pack
from pack import publish
from pack.publish import PublishError
import pytest

ENTRIES = [
    pack.AssetEntry(
        type=pack.ASSET_TYPE_SCRIPT, path="parameters.lua", data=b"return {}"
    ),
]


class LocalNode:
    """The asset-pack volume as a local folder."""

    def __init__(self, root: Path):
        self.root = root
        self.installs = 0

    def _local(self, directory: str) -> Path:
        return self.root / directory.removeprefix(publish.VOLUME_ROOT + "/")

    def digests(self, directory: str) -> dict[str, str] | None:
        folder = self._local(directory)
        if not folder.is_dir():
            return None
        return {
            path.name: hashlib.sha256(path.read_bytes()).hexdigest()
            for path in folder.iterdir()
        }

    def install(self, directory: str, files: dict[str, Path]) -> None:
        folder = self._local(directory)
        folder.mkdir(parents=True)
        for name, path in files.items():
            shutil.copyfile(path, folder / name)
        self.installs += 1


@pytest.fixture
def cook(tmp_path, key_pair):
    """A cook's pack pair, signed by key_pair, and its public key file."""
    client_hash = pack.write_pack(
        tmp_path / "client.pack", ENTRIES, key_pair.private_key
    )
    server_hash = pack.write_pack(
        tmp_path / "server.pack", ENTRIES, key_pair.private_key, client_hash
    )
    public_key = tmp_path / "local-name.pub"
    public_key.write_bytes(key_pair.public_key)
    return (
        tmp_path / "client.pack",
        tmp_path / "server.pack",
        public_key,
        server_hash,
    )


def test_a_cook_is_published_under_its_scenario_and_server_pack_hash(
    tmp_path, cook
):
    client_pack, server_pack, public_key, server_hash = cook
    node = LocalNode(tmp_path / "node")

    release = publish.prepare("firebase", client_pack, server_pack, public_key)
    copied = publish.publish(release, node)

    assert copied
    assert release.version == server_hash.hex()[:12]
    assert (
        release.directory
        == f"/srv/augusta/asset-packs/firebase/{release.version}"
    )
    folder = node.root / "firebase" / release.version
    assert sorted(path.name for path in folder.iterdir()) == [
        "augusta.pub",
        "server.pack",
    ]
    assert (folder / "server.pack").read_bytes() == server_pack.read_bytes()
    assert (folder / "augusta.pub").read_bytes() == public_key.read_bytes()


def test_publishing_a_version_already_on_the_node_copies_nothing(
    tmp_path, cook
):
    client_pack, server_pack, public_key, _ = cook
    node = LocalNode(tmp_path / "node")
    release = publish.prepare("firebase", client_pack, server_pack, public_key)
    publish.publish(release, node)

    assert not publish.publish(release, node)
    assert node.installs == 1


def test_a_version_folder_holding_other_files_is_never_overwritten(
    tmp_path, cook
):
    client_pack, server_pack, public_key, _ = cook
    node = LocalNode(tmp_path / "node")
    release = publish.prepare("firebase", client_pack, server_pack, public_key)
    folder = node.root / "firebase" / release.version
    folder.mkdir(parents=True)
    (folder / "server.pack").write_bytes(b"something else")

    with pytest.raises(PublishError, match="holds other files"):
        publish.publish(release, node)
    assert (folder / "server.pack").read_bytes() == b"something else"


def test_a_server_pack_from_another_cook_is_refused(tmp_path, cook, key_pair):
    client_pack, _, public_key, _ = cook
    other_entries = [
        pack.AssetEntry(
            type=pack.ASSET_TYPE_SCRIPT,
            path="parameters.lua",
            data=b"return { x = 1 }",
        )
    ]
    other_client_hash = pack.write_pack(
        tmp_path / "other-client.pack", other_entries, key_pair.private_key
    )
    other_server = tmp_path / "other-server.pack"
    pack.write_pack(
        other_server, ENTRIES, key_pair.private_key, other_client_hash
    )

    with pytest.raises(PublishError, match="not from the same cook"):
        publish.prepare("firebase", client_pack, other_server, public_key)


def test_a_client_pack_given_as_the_server_pack_is_refused(cook):
    client_pack, _, public_key, _ = cook

    with pytest.raises(PublishError, match="not a server pack"):
        publish.prepare("firebase", client_pack, client_pack, public_key)


def test_packs_signed_by_another_key_are_refused(tmp_path, cook):
    client_pack, server_pack, _, _ = cook
    other_key = tmp_path / "other.pub"
    other_key.write_bytes(keys.generate_keypair()[0])

    with pytest.raises(PublishError, match="signature is not valid"):
        publish.prepare("firebase", client_pack, server_pack, other_key)


def test_a_missing_pack_is_refused(tmp_path, cook):
    client_pack, _, public_key, _ = cook

    with pytest.raises(PublishError):
        publish.prepare(
            "firebase", client_pack, tmp_path / "missing.pack", public_key
        )


@pytest.mark.parametrize(
    "scenario",
    ["Firebase", "-firebase", "firebase-", "../firebase", "a_b", "a" * 47, ""],
)
def test_a_scenario_name_no_server_can_be_named_by_is_refused(scenario, cook):
    client_pack, server_pack, public_key, _ = cook

    with pytest.raises(PublishError, match="cannot name a server"):
        publish.prepare(scenario, client_pack, server_pack, public_key)
