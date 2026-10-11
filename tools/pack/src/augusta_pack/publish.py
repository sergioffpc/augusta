"""augusta-publish.

Puts a scenario's server pack where the cluster's servers read it (ADR-0026), on
the k3s node's shared asset-pack volume, at
/srv/augusta/asset-packs/<scenario>/<version>/{server.pack,signing.pub}.

Before anything is copied, both packs of the scenario's cook verify against
the public key and the server pack names that client pack in its header
(ADR-0031): a server pack is never published without the client pack players
need to join it. The version is the first 12 hex characters of the server
pack's BLAKE3 hash, so a folder is never overwritten: a new cook is a new
folder, and the old one stays in place as the rollback. Publishing a version
already on the node checks it holds the same files and changes nothing.

Only the server pack and the public key go to the node, the key renamed to
signing.pub as the chart expects. The client pack stays local; nothing here
distributes it. Publishing does not deploy: an environment serves the new
pack once its HelmRelease names the printed version (clusters/onprem/apps/).

The node is reached with the OpenSSH client (ssh) on PATH, as the node's
augusta-publish user, whose key does nothing but run the node's end of
publishing (clusters/onprem/node/augusta-publish-receive.sh, ADR-0057): no
shell, no sudo. That end writes a version's folder once, and leaves it
read-only.
"""

import argparse
from dataclasses import dataclass
import hashlib
from pathlib import Path
import re
import subprocess
import sys
from typing import Protocol

from augusta_pack.assets_root import default_assets_root
from augusta_pack.keys import read_public_key
from augusta_pack.reader import verify_pack

VOLUME_ROOT = "/srv/augusta/asset-packs"
SERVER_PACK_NAME = "server.pack"
PUBLIC_KEY_NAME = "signing.pub"
VERSION_LENGTH = 12

# What the chart can name a server by (charts/augustad): a DNS label short
# enough for augustad-<scenario>-metrics to stay one.
_SCENARIO_PATTERN = re.compile(r"[a-z0-9]([a-z0-9-]{0,44}[a-z0-9])?")


class PublishError(Exception):
    """Raised when a scenario's packs cannot be published."""


@dataclass(frozen=True)
class Release:
    """A verified server pack ready to publish, and where it goes."""

    scenario: str
    version: str
    # Name on the node -> local file.
    files: dict[str, Path]

    @property
    def folder(self) -> str:
        """The release's folder, relative to the volume."""
        return f"{self.scenario}/{self.version}"

    @property
    def directory(self) -> str:
        """The release's folder on the node."""
        return f"{VOLUME_ROOT}/{self.folder}"


class Node(Protocol):
    """The asset-pack volume of the node the servers run on."""

    def digests(self, folder: str) -> dict[str, str] | None:
        """The SHA-256 of each file in folder, by name.

        None if folder does not exist.
        """

    def install(self, folder: str, files: dict[str, Path]) -> None:
        """Creates folder holding files, each under its name, all at once.

        It never exists partly written. Fails if it exists already.
        """


def prepare(
    scenario: str, client_pack: Path, server_pack: Path, public_key_path: Path
) -> Release:
    """Checks the scenario can be published, and names its folder.

    Raises:
        PublishError: the packs or the key can't be published.
    """
    if not _SCENARIO_PATTERN.fullmatch(scenario):
        raise PublishError(
            f"scenario {scenario!r} cannot name a server: lowercase letters, "
            f"digits and '-', "
            "starting and ending with a letter or digit, at most 46 characters"
        )
    try:
        public_key = read_public_key(public_key_path)
        client = verify_pack(client_pack, public_key)
        server = verify_pack(server_pack, public_key)
    except (OSError, ValueError) as error:
        # PackError and read_public_key's size error are both ValueErrors.
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
        scenario=scenario,
        version=server.hash.hex()[:VERSION_LENGTH],
        files={SERVER_PACK_NAME: server_pack, PUBLIC_KEY_NAME: public_key_path},
    )


def publish(release: Release, node: Node) -> bool:
    """Puts release on node, unless it is there already.

    Returns whether it copied anything. Raises PublishError if the folder holds
    other files or the copy does not match.
    """
    expected = {name: _sha256(path) for name, path in release.files.items()}
    existing = node.digests(release.folder)
    if existing is not None:
        if existing != expected:
            raise PublishError(
                f"{release.directory} already exists on the node and holds "
                f"other files"
            )
        return False
    node.install(release.folder, release.files)
    if node.digests(release.folder) != expected:
        raise PublishError(
            f"{release.directory} on the node does not match the local files"
        )
    return True


def _sha256(path: Path) -> str:
    with open(path, "rb") as f:
        return hashlib.file_digest(f, "sha256").hexdigest()


class SshNode:
    """The node reached as host by ssh, through its publish key's command.

    Each call is one of the commands
    clusters/onprem/node/augusta-publish-receive.sh takes.
    """

    # Exit status of digests when the folder is missing, apart from ssh's own
    # 255 and the forced command's 1 and 2.
    _MISSING = 3

    def __init__(self, host: str):
        self._host = host

    def digests(self, folder: str) -> dict[str, str] | None:
        """See Node.digests."""
        result = self._ssh(f"digests {folder}", check=False)
        if result.returncode == self._MISSING:
            return None
        if result.returncode != 0:
            raise PublishError(
                f"cannot read {folder} on {self._host}: {result.stderr.strip()}"
            )
        digests = {}
        for line in result.stdout.splitlines():
            digest, name = line.split(maxsplit=1)
            digests[name.lstrip("*")] = digest
        return digests

    def install(self, folder: str, files: dict[str, Path]) -> None:
        """See Node.install."""
        # Received beside the folder, then renamed into it once it holds every
        # file with its digest: a failed copy never leaves a folder a server
        # could be pointed at.
        for name, path in files.items():
            with open(path, "rb") as stdin:
                self._ssh(f"receive {folder} {name}", stdin=stdin)
        self._ssh(
            f"commit {folder} "
            + " ".join(
                f"{name}={_sha256(path)}" for name, path in files.items()
            )
        )

    def _ssh(
        self, command: str, check: bool = True, stdin=subprocess.DEVNULL
    ) -> subprocess.CompletedProcess:
        args = ["ssh", self._host, command]
        try:
            result = subprocess.run(
                args, stdin=stdin, capture_output=True, text=True, check=False
            )
        except OSError as error:
            raise PublishError(f"cannot run ssh: {error}") from error
        if check and result.returncode != 0:
            raise PublishError(
                f"ssh to {self._host} failed: {result.stderr.strip()}"
            )
        return result


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
        "Default: <assets-root>/keys/signing.pub",
    )
    args = parser.parse_args(argv)

    packs = args.assets_root / "packs" / args.scenario
    try:
        release = prepare(
            args.scenario,
            args.client_pack or packs / "client.pack",
            args.server_pack or packs / "server.pack",
            args.public_key or args.assets_root / "keys" / "signing.pub",
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
        f'"{release.version}"'
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
