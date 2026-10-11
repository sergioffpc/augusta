"""augusta-publish.

Which packs it agrees to publish, the folder it names, and that a folder on the
node is never overwritten. The node is a local folder standing in for the
asset-pack volume. SshNode is tested against the node's own end, the publish
key's forced command (clusters/onprem/node/augusta-publish-receive.sh), run
locally by an ssh that stands in for sshd; ssh itself is not run.
"""

import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import sys

from augusta_pack import keys
from augusta_pack import pack
from augusta_pack import publish
from augusta_pack.publish import PublishError
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

    def digests(self, folder: str) -> dict[str, str] | None:
        folder = self.root / folder
        if not folder.is_dir():
            return None
        return {
            path.name: hashlib.sha256(path.read_bytes()).hexdigest()
            for path in folder.iterdir()
        }

    def install(self, folder: str, files: dict[str, Path]) -> None:
        folder = self.root / folder
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
        "server.pack",
        "signing.pub",
    ]
    assert (folder / "server.pack").read_bytes() == server_pack.read_bytes()
    assert (folder / "signing.pub").read_bytes() == public_key.read_bytes()


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


RECEIVER = (
    Path(__file__).parents[3]
    / "clusters"
    / "onprem"
    / "node"
    / "augusta-publish-receive.sh"
)
VERSION = "0123456789ab"
# The node's own commands: the receiver is a bash script, and Windows' file
# modes do not stop a write.
node_only = pytest.mark.skipif(
    sys.platform == "win32", reason="the node's end runs on Linux"
)


@pytest.fixture
def node_volume(tmp_path, monkeypatch):
    """The asset-pack volume, reached by an ssh that runs the forced command.

    The ssh on PATH runs the receiver as sshd does for the publish key: the
    client's command in SSH_ORIGINAL_COMMAND, its stdin passed through.
    """
    volume = tmp_path / "asset-packs"
    volume.mkdir()
    receiver = tmp_path / "receiver.sh"
    script = RECEIVER.read_text()
    line = f"readonly PACKS={publish.VOLUME_ROOT}\n"
    assert line in script
    receiver.write_text(script.replace(line, f"readonly PACKS={volume}\n"))
    bin_dir = tmp_path / "bin"
    bin_dir.mkdir()
    ssh = bin_dir / "ssh"
    ssh.write_text(
        f'#!/bin/bash\nshift\nSSH_ORIGINAL_COMMAND="$*" exec bash {receiver}\n'
    )
    ssh.chmod(0o755)
    monkeypatch.setenv("PATH", f"{bin_dir}{os.pathsep}{os.environ['PATH']}")
    yield volume
    # Sealed folders, which pytest could not remove.
    for path in volume.rglob("*"):
        if path.is_dir():
            path.chmod(0o750)


def forced(command: str, stdin: bytes = b"") -> subprocess.CompletedProcess:
    """Runs command as the publish key, on the node."""
    return subprocess.run(
        ["ssh", "node", command], input=stdin, capture_output=True, check=False
    )


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


@node_only
def test_a_cook_is_published_through_the_forced_command(node_volume, cook):
    client_pack, server_pack, public_key, _ = cook
    release = publish.prepare("firebase", client_pack, server_pack, public_key)

    assert publish.publish(release, publish.SshNode("node"))

    folder = node_volume / "firebase" / release.version
    assert sorted(
        path.name for path in (node_volume / "firebase").iterdir()
    ) == [release.version]
    assert (folder / "server.pack").read_bytes() == server_pack.read_bytes()
    assert (folder / "signing.pub").read_bytes() == public_key.read_bytes()
    assert not publish.publish(release, publish.SshNode("node"))


@node_only
def test_a_published_folder_is_read_only_to_the_publish_user(node_volume, cook):
    if os.geteuid() == 0:
        pytest.skip("root writes whatever the modes say")
    client_pack, server_pack, public_key, _ = cook
    release = publish.prepare("firebase", client_pack, server_pack, public_key)
    publish.publish(release, publish.SshNode("node"))
    folder = node_volume / "firebase" / release.version

    assert folder.stat().st_mode & 0o777 == 0o550
    for name in ("server.pack", "signing.pub"):
        assert (folder / name).stat().st_mode & 0o777 == 0o440
    with pytest.raises(PermissionError):
        (folder / "server.pack").write_bytes(b"replaced")
    with pytest.raises(PermissionError):
        (folder / "extra").write_bytes(b"added")


@node_only
def test_the_forced_command_never_writes_into_a_published_version(
    node_volume, cook
):
    client_pack, server_pack, public_key, _ = cook
    release = publish.prepare("firebase", client_pack, server_pack, public_key)
    publish.publish(release, publish.SshNode("node"))
    folder = node_volume / "firebase" / release.version
    before = (folder / "server.pack").read_bytes()

    received = forced(f"receive {release.folder} server.pack", b"replaced")
    committed = forced(
        f"commit {release.folder} server.pack={sha256(b'replaced')}"
    )

    assert received.returncode != 0
    assert committed.returncode != 0
    assert (folder / "server.pack").read_bytes() == before


@node_only
def test_a_commit_that_does_not_match_what_was_received_writes_nothing(
    node_volume,
):
    forced(f"receive firebase/{VERSION} server.pack", b"pack")
    forced(f"receive firebase/{VERSION} signing.pub", b"key")

    result = forced(
        f"commit firebase/{VERSION} server.pack={sha256(b'other')} "
        f"signing.pub={sha256(b'key')}"
    )

    assert result.returncode != 0
    assert not (node_volume / "firebase" / VERSION).exists()


@node_only
def test_a_commit_naming_fewer_files_than_were_received_writes_nothing(
    node_volume,
):
    forced(f"receive firebase/{VERSION} server.pack", b"pack")
    forced(f"receive firebase/{VERSION} signing.pub", b"key")

    result = forced(f"commit firebase/{VERSION} server.pack={sha256(b'pack')}")

    assert result.returncode != 0
    assert not (node_volume / "firebase" / VERSION).exists()


@node_only
@pytest.mark.parametrize(
    "command",
    [
        "",
        "sh",
        "bash -c id",
        "rm -rf firebase",
        f"digests ../firebase/{VERSION}",
        f"digests firebase/{VERSION}/..",
        "digests firebase/../../etc",
        "digests firebase/0123456789AB",
        f"receive firebase/{VERSION} ../escape",
        f"receive firebase/{VERSION} other.pack",
        f"receive firebase/{VERSION}",
        f"receive Firebase/{VERSION} server.pack",
        f"commit firebase/{VERSION} server.pack=nothex",
        f"commit firebase/{VERSION} ../x={sha256(b'')}",
        f"digests firebase/{VERSION} extra",
    ],
)
def test_the_forced_command_refuses_anything_but_publishing(
    node_volume, command
):
    result = forced(command, b"data")

    assert result.returncode == 2, result.stderr
    assert list(node_volume.rglob("*")) == []
    assert not (node_volume.parent / "escape").exists()
