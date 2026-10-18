"""augusta-publish.

Puts a scenario's server pack where the cluster's servers read it (ADR-0026), on
the k3s node's shared asset-pack volume, in the tree of the Environment that
serves it, at
/srv/augusta/asset-packs/<environment>/<scenario>/<version>/{server.pack,signing.pub}.
Each Environment signs its packs with its own key (keys/<environment>.key), so
one cook signed for develop and for staging is two folders, each with its own
signing.pub.

Before anything is copied, both packs of the scenario's cook verify against
the public key and the server pack names that client pack in its header
(ADR-0031): a server pack is never published without the client pack players
need to join it. The version is the first 12 hex characters of the server
pack's BLAKE3 hash, which does not cover the signature, so a folder is
never overwritten: a new cook is a new folder, and the old one stays in place
as the rollback. Publishing a version
already on the node checks it holds the same files and changes nothing.

Only the server pack and the public key go to the node, the key renamed to
signing.pub as the chart expects. The client pack stays local; nothing here
distributes it. Publishing does not deploy: an environment serves the new
pack once its HelmRelease names the printed version, with the pack format it
was cooked in beside it (clusters/onprem/apps/).

The node is reached with the OpenSSH client (ssh) on PATH, as the node's
augusta-publish user, whose key does nothing but run the node's end of
publishing (clusters/onprem/node/augusta-publish-receive.sh, ADR-0057): no
shell, no sudo. A publish is one connection, so one touch of a FIDO2 key. That
end writes a version's folder once, and leaves it read-only. It takes the
Environments below alone.
"""

import argparse
from collections.abc import Iterable
import contextlib
from dataclasses import dataclass
import hashlib
from pathlib import Path
import re
import subprocess
import sys
import tempfile
from typing import IO, Protocol

from augusta_pack import _native
from augusta_pack.assets_root import default_assets_root
from augusta_pack.keys import read_public_key

VOLUME_ROOT = "/srv/augusta/asset-packs"
SERVER_PACK_NAME = "server.pack"
PUBLIC_KEY_NAME = "signing.pub"
VERSION_LENGTH = 12
# The Environments the node serves, each from its own tree: the node's end
# (augusta-publish-receive.sh) and scripts/bootstrap-node.sh name them too.
ENVIRONMENTS = ("develop", "staging")

# What the chart can name a server by (charts/augustad): a DNS label short
# enough for augustad-<scenario>-metrics to stay one.
_SCENARIO_PATTERN = re.compile(r"[a-z0-9]([a-z0-9-]{0,44}[a-z0-9])?")


class PublishError(Exception):
    """Raised when a scenario's packs cannot be published."""


@dataclass(frozen=True)
class Release:
    """A verified server pack ready to publish, and where it goes."""

    environment: str
    scenario: str
    version: str
    # The pack format version the server pack was cooked in (ADR-0031).
    pack_format: int
    # Name on the node -> local file.
    files: dict[str, Path]

    @property
    def folder(self) -> str:
        """The release's folder, relative to the volume."""
        return f"{self.environment}/{self.scenario}/{self.version}"

    @property
    def directory(self) -> str:
        """The release's folder on the node."""
        return f"{VOLUME_ROOT}/{self.folder}"


class Node(Protocol):
    """The asset-pack volume of the node the servers run on."""

    def put(
        self, folder: str, files: dict[str, Path], digests: dict[str, str]
    ) -> bool:
        """Creates folder holding files, each under its name, all at once.

        digests is the SHA-256 of each file, by name. folder never exists
        partly written. Returns False, copying nothing, if it holds exactly
        those files already.

        Raises:
            PublishError: folder holds other files, or what arrived is not
                files.
        """


def prepare(
    environment: str,
    scenario: str,
    client_pack: Path,
    server_pack: Path,
    public_key_path: Path,
) -> Release:
    """Checks the scenario can be published to environment; names its folder.

    Raises:
        PublishError: the environment is not one the node serves, or the packs
            or the key can't be published.
    """
    if environment not in ENVIRONMENTS:
        raise PublishError(
            f"{environment!r} is not an Environment the node serves: "
            f"{', '.join(ENVIRONMENTS)}"
        )
    if not _SCENARIO_PATTERN.fullmatch(scenario):
        raise PublishError(
            f"scenario {scenario!r} cannot name a server: lowercase letters, "
            f"digits and '-', "
            "starting and ending with a letter or digit, at most 46 characters"
        )
    try:
        public_key = read_public_key(public_key_path)
        client = _native.load_pack(client_pack, public_key)
        server = _native.load_pack(server_pack, public_key)
        pack_format = _native.read_pack_layout(server_pack).version
    except (OSError, ValueError) as error:
        # LoadError and read_public_key's size error are both ValueErrors.
        raise PublishError(str(error)) from error
    if server.client_pack_hash is None:
        raise PublishError(
            f"{server_pack} is not a server pack: its header names no client "
            f"pack"
        )
    if server.client_pack_hash != client.hash:
        raise PublishError(
            f"{server_pack} and {client_pack} are not from the same cook: the "
            f"server pack names client pack {server.client_pack_hash.hex()}, "
            f"not {client.hash.hex()}"
        )
    return Release(
        environment=environment,
        scenario=scenario,
        version=server.hash.hex()[:VERSION_LENGTH],
        pack_format=pack_format,
        files={SERVER_PACK_NAME: server_pack, PUBLIC_KEY_NAME: public_key_path},
    )


def publish(release: Release, node: Node) -> bool:
    """Puts release on node, unless it is there already.

    Returns whether it copied anything. Raises PublishError if the folder holds
    other files or the copy does not match.
    """
    digests = {name: _sha256(path) for name, path in release.files.items()}
    return node.put(release.folder, release.files, digests)


def _sha256(path: Path) -> str:
    with open(path, "rb") as f:
        return hashlib.file_digest(f, "sha256").hexdigest()


class SshNode:
    """The node reached as host by ssh, through its publish key's command.

    A put is one connection running the one command
    clusters/onprem/node/augusta-publish-receive.sh takes, publish: one
    authentication, so one touch of a FIDO2 key.
    """

    # The forced command's exit status for a folder that holds those files
    # already, apart from ssh's own 255 and the forced command's 1, 2 and 4.
    _PUBLISHED = 3
    _CHUNK = 1 << 16

    def __init__(self, host: str):
        self._host = host

    def put(
        self, folder: str, files: dict[str, Path], digests: dict[str, str]
    ) -> bool:
        """See Node.put."""
        # The files go back to back on stdin, each its size, in the command's
        # order: the node parses no archive, and takes names from the command.
        try:
            named = " ".join(
                f"{name}={digests[name]}:{path.stat().st_size}"
                for name, path in files.items()
            )
        except OSError as error:
            raise PublishError(
                f"cannot read {error.filename}: {error}"
            ) from error
        # A file, not a pipe: ssh never blocks on its stderr while stdin is
        # still being written.
        with tempfile.TemporaryFile() as stderr:
            try:
                ssh = subprocess.Popen(
                    ["ssh", self._host, f"publish {folder} {named}"],
                    stdin=subprocess.PIPE,
                    stdout=subprocess.DEVNULL,
                    stderr=stderr,
                )
            except OSError as error:
                raise PublishError(f"cannot run ssh: {error}") from error
            try:
                self._send(files.values(), ssh.stdin)
                ssh.wait()
            finally:
                # Interrupted, or a file unreadable: the node, its stdin cut
                # short, publishes nothing.
                if ssh.poll() is None:
                    ssh.kill()
                    ssh.wait()
            stderr.seek(0)
            message = stderr.read().decode(errors="replace").strip()
        if ssh.returncode == self._PUBLISHED:
            return False
        if ssh.returncode != 0:
            raise PublishError(
                f"publishing {folder} on {self._host}: {message}"
            )
        return True

    @classmethod
    def _send(cls, paths: Iterable[Path], stdin: IO[bytes]) -> None:
        """Writes the files to ssh's stdin back to back, then closes it.

        Stops early once the node stops reading.

        Raises:
            PublishError: a file cannot be read.
        """
        try:
            for path in paths:
                with open(path, "rb") as f:
                    while chunk := f.read(cls._CHUNK):
                        if not _write(stdin, chunk):
                            return
        except OSError as error:
            raise PublishError(f"cannot read {path}: {error}") from error
        finally:
            with contextlib.suppress(OSError):
                stdin.close()


def _write(stdin: IO[bytes], chunk: bytes) -> bool:
    """Writes chunk to ssh's stdin; returns whether the node still reads.

    It stops once the folder is there already, or it refused: its exit status
    and stderr say which.
    """
    try:
        stdin.write(chunk)
    except OSError:
        # BrokenPipeError; EINVAL on Windows.
        return False
    return True


def main(argv: list[str] | None = None) -> int:
    """Publishes the scenario argv names; returns the process's exit code."""
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument(
        "scenario",
        help="Scenario name (ADR-0041), which also names its server in the "
        "cluster: its packs default to "
        "<assets-root>/packs/<scenario>/{client,server}.pack, where "
        "augusta-pack writes them.",
    )
    parser.add_argument(
        "--environment",
        required=True,
        choices=ENVIRONMENTS,
        help="The Environment that serves the pack, which names its tree on "
        "the node and the key its packs are signed with.",
    )
    parser.add_argument(
        "--host",
        required=True,
        help="The k3s node as its augusta-publish user, as ssh names it "
        "(augusta-publish@<node>, or an alias in ~/.ssh/config with that "
        "User and the publish key).",
    )
    parser.add_argument(
        "--assets-root",
        type=Path,
        default=default_assets_root(),
        help="Hermetic environment root, for the defaults below (default: "
        "inferred from this interpreter's own venv).",
    )
    parser.add_argument(
        "--client-pack",
        type=Path,
        default=None,
        help="Default: <assets-root>/packs/<scenario>/client.pack",
    )
    parser.add_argument(
        "--server-pack",
        type=Path,
        default=None,
        help="Default: <assets-root>/packs/<scenario>/server.pack",
    )
    parser.add_argument(
        "--public-key",
        type=Path,
        default=None,
        help="The key both packs are signed with, published as signing.pub. "
        "Default: <assets-root>/keys/<environment>.pub",
    )
    args = parser.parse_args(argv)

    packs = args.assets_root / "packs" / args.scenario
    try:
        release = prepare(
            args.environment,
            args.scenario,
            args.client_pack or packs / "client.pack",
            args.server_pack or packs / "server.pack",
            args.public_key
            or args.assets_root / "keys" / f"{args.environment}.pub",
        )
        copied = publish(release, SshNode(args.host))
    except PublishError as error:
        print(error, file=sys.stderr)
        return 1

    print(
        f"{'Published' if copied else 'Already published'}: "
        f"{args.host}:{release.directory}"
    )
    # Quoted: a version of digits alone would read as a number in YAML.
    print(
        f"  serve it with servers.{release.scenario}.packVersion: "
        f'"{release.version}" and packFormat: {release.pack_format} in '
        f"clusters/onprem/apps/{release.environment}.yaml"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
