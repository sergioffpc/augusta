"""augusta-publish: puts a scenario's server pack where the cluster's servers
read it (ADR-0026), on the k3s node's shared asset-pack volume, at
/srv/augusta/asset-packs/<scenario>/<version>/{server.pack,augusta.pub}.

Before anything is copied, both packs of the scenario's cook verify against
the public key and the server pack names that client pack in its header
(ADR-0031): a server pack is never published without the client pack players
need to join it. The version is the first 12 hex characters of the server
pack's BLAKE3 hash, so a folder is never overwritten: a new cook is a new
folder, and the old one stays in place as the rollback. Publishing a version
already on the node checks it holds the same files and changes nothing.

Only the server pack and the public key go to the node, the key renamed to
augusta.pub as the chart expects. The client pack stays local; nothing here
distributes it. Publishing does not deploy: an environment serves the new
pack once its HelmRelease names the printed version (clusters/onprem/apps/).

The node is reached with the OpenSSH client (ssh, scp) on PATH, as a user
that can sudo without a password: the volume belongs to root.
"""

import argparse
import hashlib
import re
import shlex
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Protocol

from pack.assets_root import default_assets_root
from pack.keys import read_public_key
from pack.reader import verify_pack

VOLUME_ROOT = "/srv/augusta/asset-packs"
SERVER_PACK_NAME = "server.pack"
PUBLIC_KEY_NAME = "augusta.pub"
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
    def directory(self) -> str:
        return f"{VOLUME_ROOT}/{self.scenario}/{self.version}"


class Node(Protocol):
    """The asset-pack volume of the node the servers run on."""

    def digests(self, directory: str) -> dict[str, str] | None:
        """The SHA-256 of each file in directory, by name, or None if
        directory does not exist."""

    def install(self, directory: str, files: dict[str, Path]) -> None:
        """Creates directory holding files, each under its name, all at once:
        it never exists partly written. Fails if it exists already."""


def prepare(scenario: str, client_pack: Path, server_pack: Path, public_key_path: Path) -> Release:
    """Checks the scenario can be published from these packs and key, and
    names the folder it goes to. Raises PublishError."""
    if not _SCENARIO_PATTERN.fullmatch(scenario):
        raise PublishError(
            f"scenario {scenario!r} cannot name a server: lowercase letters, digits and '-', "
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
        raise PublishError(f"{server_pack} is not a server pack: its header names no client pack")
    if server.client_pack_hash != client.hash:
        raise PublishError(
            f"{server_pack} and {client_pack} are not from the same cook: the server pack names client pack "
            f"{server.client_pack_hash.hex()}, not {client.hash.hex()}"
        )
    return Release(
        scenario=scenario,
        version=server.hash.hex()[:VERSION_LENGTH],
        files={SERVER_PACK_NAME: server_pack, PUBLIC_KEY_NAME: public_key_path},
    )


def publish(release: Release, node: Node) -> bool:
    """Puts release on node, unless it is there already. Returns whether it
    copied anything. Raises PublishError if the folder holds other files or
    the copy does not match."""
    expected = {name: _sha256(path) for name, path in release.files.items()}
    existing = node.digests(release.directory)
    if existing is not None:
        if existing != expected:
            raise PublishError(f"{release.directory} already exists on the node and holds other files")
        return False
    node.install(release.directory, release.files)
    if node.digests(release.directory) != expected:
        raise PublishError(f"{release.directory} on the node does not match the local files")
    return True


def _sha256(path: Path) -> str:
    with open(path, "rb") as f:
        return hashlib.file_digest(f, "sha256").hexdigest()


class SshNode:
    """The node reached as host by ssh and scp."""

    # Exit status of the digests command when the folder is missing, apart
    # from ssh's own 255 and sha256sum's 1.
    _MISSING = 3

    def __init__(self, host: str):
        self._host = host

    def digests(self, directory: str) -> dict[str, str] | None:
        folder = shlex.quote(directory)
        result = self._ssh(f"test -d {folder} || exit {self._MISSING}; cd {folder} && sha256sum -- *", check=False)
        if result.returncode == self._MISSING:
            return None
        if result.returncode != 0:
            raise PublishError(f"cannot read {directory} on {self._host}: {result.stderr.strip()}")
        digests = {}
        for line in result.stdout.splitlines():
            digest, name = line.split(maxsplit=1)
            digests[name.lstrip("*")] = digest
        return digests

    def install(self, directory: str, files: dict[str, Path]) -> None:
        upload = self._ssh("mktemp -d").stdout.strip()
        try:
            for name, path in files.items():
                self._run(["scp", "-q", str(path), f"{self._host}:{upload}/{name}"])
            # Assembled beside its final place, then renamed into it: a
            # failed copy never leaves a folder a server could be pointed at.
            staging = f"{directory}.partial"
            installs = " && ".join(
                f"sudo install -m 0644 {shlex.quote(f'{upload}/{name}')} {shlex.quote(f'{staging}/{name}')}"
                for name in files
            )
            self._ssh(
                f"sudo rm -rf {shlex.quote(staging)} && sudo install -d -m 0755 {shlex.quote(staging)} && "
                f"{installs} && sudo mv -T {shlex.quote(staging)} {shlex.quote(directory)} "
                f"|| {{ sudo rm -rf {shlex.quote(staging)}; exit 1; }}"
            )
        finally:
            self._ssh(f"rm -rf {shlex.quote(upload)}", check=False)

    def _ssh(self, command: str, check: bool = True) -> subprocess.CompletedProcess:
        return self._run(["ssh", self._host, command], check=check)

    def _run(self, args: list[str], check: bool = True) -> subprocess.CompletedProcess:
        try:
            result = subprocess.run(args, capture_output=True, text=True)
        except OSError as error:
            raise PublishError(f"cannot run {args[0]}: {error}") from error
        if check and result.returncode != 0:
            raise PublishError(f"{args[0]} to {self._host} failed: {result.stderr.strip()}")
        return result


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument(
        "scenario",
        help="Scenario name (ADR-0041), which also names its server in the cluster: its packs default to "
        "<assets-root>/packs/<scenario>/{client,server}.pack, where augusta-pack writes them.",
    )
    parser.add_argument("--host", required=True, help="The k3s node, as ssh names it (e.g. an alias in ~/.ssh/config).")
    parser.add_argument(
        "--assets-root",
        type=Path,
        default=default_assets_root(),
        help="Hermetic environment root, for the defaults below (default: inferred from this interpreter's own venv).",
    )
    parser.add_argument(
        "--client-pack", type=Path, default=None, help="Default: <assets-root>/packs/<scenario>/client.pack"
    )
    parser.add_argument(
        "--server-pack", type=Path, default=None, help="Default: <assets-root>/packs/<scenario>/server.pack"
    )
    parser.add_argument(
        "--public-key",
        type=Path,
        default=None,
        help="The key both packs are signed with, published as augusta.pub. Default: <assets-root>/keys/augusta.pub",
    )
    args = parser.parse_args(argv)

    packs = args.assets_root / "packs" / args.scenario
    try:
        release = prepare(
            args.scenario,
            args.client_pack or packs / "client.pack",
            args.server_pack or packs / "server.pack",
            args.public_key or args.assets_root / "keys" / "augusta.pub",
        )
        copied = publish(release, SshNode(args.host))
    except PublishError as error:
        print(error, file=sys.stderr)
        return 1

    print(f"{'Published' if copied else 'Already published'}: {args.host}:{release.directory}")
    # Quoted: a version of digits alone would read as a number in YAML.
    print(f'  serve it with servers.{release.scenario}.packVersion: "{release.version}"')
    return 0


if __name__ == "__main__":
    sys.exit(main())
