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
import threading

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

    def put(
        self, folder: str, files: dict[str, Path], digests: dict[str, str]
    ) -> bool:
        folder = self.root / folder
        if folder.is_dir():
            held = {
                path.name: hashlib.sha256(path.read_bytes()).hexdigest()
                for path in folder.iterdir()
            }
            if held != digests:
                raise PublishError(f"{folder} holds other files")
            return False
        folder.mkdir(parents=True)
        for name, path in files.items():
            shutil.copyfile(path, folder / name)
        self.installs += 1
        return True


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
DIGEST = hashlib.sha256(b"").hexdigest()
# The node's own commands: the receiver is a bash script, and Windows' file
# modes do not stop a write.
node_only = pytest.mark.skipif(
    sys.platform == "win32", reason="the node's end runs on Linux"
)


@pytest.fixture
def node_volume(tmp_path, monkeypatch):
    """The asset-pack volume, reached by an ssh that runs the forced command.

    The ssh on PATH runs the receiver as sshd does for the publish key: the
    client's command in SSH_ORIGINAL_COMMAND, its stdin passed through. Each
    connection adds a line to the volume's sibling ssh.log.
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
        "#!/bin/bash\n"
        f"echo connection >>{tmp_path / 'ssh.log'}\n"
        "shift\n"
        f'SSH_ORIGINAL_COMMAND="$*" exec bash {receiver}\n'
    )
    ssh.chmod(0o755)
    monkeypatch.setenv("PATH", f"{bin_dir}{os.pathsep}{os.environ['PATH']}")
    yield volume
    # Sealed folders, which pytest could not remove.
    for path in volume.rglob("*"):
        if path.is_dir():
            path.chmod(0o750)


def connections(node_volume: Path) -> int:
    """How many times ssh reached the node so far."""
    log = node_volume.parent / "ssh.log"
    return len(log.read_text().splitlines()) if log.exists() else 0


def forced(command: str, stdin: bytes = b"") -> subprocess.CompletedProcess:
    """Runs command as the publish key, on the node."""
    return subprocess.run(
        ["ssh", "node", command], input=stdin, capture_output=True, check=False
    )


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def publish_command(folder: str, files: dict[str, bytes]) -> tuple[str, bytes]:
    """The publish command for files, by name, and the stdin it takes."""
    named = " ".join(
        f"{name}={sha256(data)}:{len(data)}" for name, data in files.items()
    )
    return f"publish {folder} {named}", b"".join(files.values())


def leftovers(node_volume: Path) -> list[Path]:
    """Whatever a publish left in a scenario's folder: versions, staging places.

    A scenario's folder itself may stay: a server is never pointed at one.
    """
    return [path for path in node_volume.glob("*/*")]


@node_only
def test_a_cook_is_published_through_the_forced_command_in_one_connection(
    node_volume, cook
):
    client_pack, server_pack, public_key, _ = cook
    release = publish.prepare("firebase", client_pack, server_pack, public_key)

    assert publish.publish(release, publish.SshNode("node"))

    assert connections(node_volume) == 1
    folder = node_volume / "firebase" / release.version
    assert sorted(
        path.name for path in (node_volume / "firebase").iterdir()
    ) == [release.version]
    assert (folder / "server.pack").read_bytes() == server_pack.read_bytes()
    assert (folder / "signing.pub").read_bytes() == public_key.read_bytes()


@node_only
def test_republishing_a_version_takes_one_connection_and_copies_nothing(
    node_volume, cook
):
    client_pack, server_pack, public_key, _ = cook
    release = publish.prepare("firebase", client_pack, server_pack, public_key)
    publish.publish(release, publish.SshNode("node"))

    assert not publish.publish(release, publish.SshNode("node"))

    assert connections(node_volume) == 2


@node_only
def test_a_version_holding_other_files_is_refused_in_one_connection(
    node_volume, cook
):
    client_pack, server_pack, public_key, _ = cook
    release = publish.prepare("firebase", client_pack, server_pack, public_key)
    command, stdin = publish_command(
        release.folder, {"server.pack": b"other", "signing.pub": b"key"}
    )
    assert forced(command, stdin).returncode == 0
    before = connections(node_volume)

    with pytest.raises(PublishError, match="holds other files"):
        publish.publish(release, publish.SshNode("node"))

    assert connections(node_volume) == before + 1
    folder = node_volume / "firebase" / release.version
    assert (folder / "server.pack").read_bytes() == b"other"


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

    result = forced(
        *publish_command(
            release.folder, {"server.pack": b"replaced", "signing.pub": b"key"}
        )
    )

    assert result.returncode == 4
    assert (folder / "server.pack").read_bytes() == before


@node_only
@pytest.mark.parametrize(
    "stdin",
    [b"packke", b"packkeyextra", b"pakckey", b""],
    ids=["short", "long", "other bytes", "empty"],
)
def test_a_publish_not_matching_what_was_sent_writes_nothing(
    node_volume, stdin
):
    command, _ = publish_command(
        f"firebase/{VERSION}", {"server.pack": b"pack", "signing.pub": b"key"}
    )

    result = forced(command, stdin)

    assert result.returncode == 1, result.stderr
    assert leftovers(node_volume) == []
    assert not (node_volume / "firebase" / VERSION).exists()


@node_only
def test_files_larger_than_a_pipe_arrive_whole(node_volume, tmp_path):
    # Neither size a multiple of the node's copy block, nor of a pipe's.
    server_pack = tmp_path / "server.pack"
    server_pack.write_bytes(os.urandom(3 * 65536 + 7))
    public_key = tmp_path / "signing.pub"
    public_key.write_bytes(os.urandom(65536 + 13))
    files = {"server.pack": server_pack, "signing.pub": public_key}
    digests = {name: sha256(path.read_bytes()) for name, path in files.items()}

    assert publish.SshNode("node").put(f"firebase/{VERSION}", files, digests)

    folder = node_volume / "firebase" / VERSION
    for name, path in files.items():
        assert (folder / name).read_bytes() == path.read_bytes()


@node_only
def test_a_publish_after_a_failed_one_is_published(node_volume):
    files = {"server.pack": b"pack", "signing.pub": b"key"}
    command, stdin = publish_command(f"firebase/{VERSION}", files)
    forced(command, stdin[:-1])

    assert forced(command, stdin).returncode == 0

    folder = node_volume / "firebase" / VERSION
    assert (folder / "server.pack").read_bytes() == b"pack"
    assert (folder / "signing.pub").read_bytes() == b"key"


@node_only
@pytest.mark.usefixtures("node_volume")
def test_the_forced_command_reports_a_version_published_with_those_files():
    command, stdin = publish_command(
        f"firebase/{VERSION}", {"server.pack": b"pack", "signing.pub": b"key"}
    )
    forced(command, stdin)

    assert forced(command, stdin).returncode == 3


@node_only
def test_two_publishes_of_one_version_take_turns(node_volume):
    files = {"server.pack": os.urandom(4 * 65536), "signing.pub": b"key"}
    command, stdin = publish_command(f"firebase/{VERSION}", files)
    publishes = [
        subprocess.Popen(
            ["ssh", "node", command],
            stdin=subprocess.PIPE,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        for _ in range(2)
    ]
    # Each fed from its own thread: whichever waits for the other still
    # takes its stdin.
    feeders = [
        threading.Thread(target=process.communicate, args=(stdin,))
        for process in publishes
    ]
    for feeder in feeders:
        feeder.start()
    for feeder in feeders:
        feeder.join()

    assert sorted(process.returncode for process in publishes) == [0, 3]
    assert leftovers(node_volume) == [node_volume / "firebase" / VERSION]


@node_only
def test_a_publish_whose_rename_fails_leaves_no_staging_place(
    node_volume, tmp_path, monkeypatch
):
    # The staging place is sealed by then: read-only, as the folder it was to
    # become.
    failing = tmp_path / "failing"
    failing.mkdir()
    mv = failing / "mv"
    mv.write_text("#!/bin/bash\nexit 1\n")
    mv.chmod(0o755)
    monkeypatch.setenv("PATH", f"{failing}{os.pathsep}{os.environ['PATH']}")

    result = forced(
        *publish_command(
            f"firebase/{VERSION}",
            {"server.pack": b"pack", "signing.pub": b"key"},
        )
    )

    assert result.returncode == 1
    assert leftovers(node_volume) == []


@node_only
def test_a_file_that_cannot_be_read_publishes_nothing(node_volume, tmp_path):
    server_pack = tmp_path / "server.pack"
    server_pack.write_bytes(b"pack")
    # It has a size, but no bytes to read.
    unreadable = tmp_path / "signing.pub"
    unreadable.mkdir()
    files = {"server.pack": server_pack, "signing.pub": unreadable}
    digests = {"server.pack": sha256(b"pack"), "signing.pub": DIGEST}

    with pytest.raises(PublishError, match=f"cannot read {unreadable}"):
        publish.SshNode("node").put(f"firebase/{VERSION}", files, digests)

    assert leftovers(node_volume) == []


@node_only
@pytest.mark.parametrize(
    "command",
    [
        "",
        "sh",
        "bash -c id",
        "rm -rf firebase",
        f"digests firebase/{VERSION}",
        f"receive firebase/{VERSION} server.pack",
        f"commit firebase/{VERSION} server.pack={DIGEST} signing.pub={DIGEST}",
        f"publish ../firebase/{VERSION} server.pack={DIGEST}:0 "
        f"signing.pub={DIGEST}:0",
        f"publish firebase/{VERSION}/.. server.pack={DIGEST}:0 "
        f"signing.pub={DIGEST}:0",
        f"publish firebase/../../etc server.pack={DIGEST}:0 "
        f"signing.pub={DIGEST}:0",
        f"publish firebase/0123456789AB server.pack={DIGEST}:0 "
        f"signing.pub={DIGEST}:0",
        f"publish Firebase/{VERSION} server.pack={DIGEST}:0 "
        f"signing.pub={DIGEST}:0",
        f"publish firebase/{VERSION} ../escape={DIGEST}:0 "
        f"signing.pub={DIGEST}:0",
        f"publish firebase/{VERSION} other.pack={DIGEST}:0 "
        f"signing.pub={DIGEST}:0",
        f"publish firebase/{VERSION} server.pack={DIGEST}:0",
        f"publish firebase/{VERSION} server.pack={DIGEST}:0 "
        f"server.pack={DIGEST}:0",
        f"publish firebase/{VERSION} server.pack=nothex:0 "
        f"signing.pub={DIGEST}:0",
        f"publish firebase/{VERSION} server.pack={DIGEST} "
        f"signing.pub={DIGEST}:0",
        f"publish firebase/{VERSION} server.pack={DIGEST}:-1 "
        f"signing.pub={DIGEST}:0",
        f"publish firebase/{VERSION} server.pack={DIGEST}:0x10 "
        f"signing.pub={DIGEST}:0",
        f"publish firebase/{VERSION} server.pack={DIGEST}:010 "
        f"signing.pub={DIGEST}:0",
        f"publish firebase/{VERSION} server.pack={DIGEST}:0 "
        f"signing.pub={DIGEST}:0 extra",
        f"publish firebase/{VERSION} server.pack={DIGEST}:0 "
        f"signing.pub={DIGEST}:0\nsh",
    ],
)
def test_the_forced_command_refuses_anything_but_publishing(
    node_volume, command
):
    result = forced(command, b"data")

    assert result.returncode == 2, result.stderr
    assert list(node_volume.rglob("*")) == []
    assert not (node_volume.parent / "escape").exists()
