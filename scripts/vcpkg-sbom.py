#!/usr/bin/env python3
"""An executable's SBOM (SPDX 2.3 JSON) from the vcpkg ports it was built with.

syft finds nothing in a C++ executable: vcpkg links most of it statically, and
what it leaves are DLLs with no package metadata. vcpkg itself writes an SPDX
document for every port it installs (share/<port>/vcpkg.spdx.json); this joins
their port packages, each with its vcpkg purl, under one package for the
executable. It lists every port the manifest installed for the triplet, test
and build-tool ports included - the manifest has one dependency list for
every target. A dependency vcpkg doesn't provide (a submodule: Falcor, NVTX)
is named with --git-package, at its checked-out commit.

  scripts/vcpkg-sbom.py --name augustad --version v1.0.0 \\
      --repo https://github.com/owner/augusta --commit <commit> \\
      build/x64-linux/vcpkg_installed/x64-linux > augustad.spdx.json
"""

import argparse
import datetime
import json
import pathlib
import subprocess
import sys

_NOASSERTION = "NOASSERTION"
# The license and copyright fields of a package nothing here makes a claim about.
_UNASSERTED = {
    "licenseConcluded": _NOASSERTION,
    "licenseDeclared": _NOASSERTION,
    "copyrightText": _NOASSERTION,
}
# vcpkg writes this for a port with no license, but never declares it, which
# makes the document invalid.
_VCPKG_NO_LICENSE = "LicenseRef-vcpkg-null"


def port_packages(installed):
    """The port package of each vcpkg.spdx.json under installed/share."""
    packages = []
    for path in sorted(installed.glob("share/*/vcpkg.spdx.json")):
        document = json.loads(path.read_text(encoding="utf-8"))
        port = next(p for p in document["packages"] if p["SPDXID"] == "SPDXRef-port")
        port = {**port, "SPDXID": f"SPDXRef-vcpkg-{port['name']}"}
        for field in ("licenseConcluded", "licenseDeclared"):
            if port[field] == _VCPKG_NO_LICENSE:
                port[field] = _NOASSERTION
        packages.append(port)
    if not packages:
        sys.exit(f"no vcpkg.spdx.json under {installed}/share - not a vcpkg installed tree")
    return packages


def git_package(path):
    """A package for the git checkout at path, at its HEAD commit."""

    def git(*args):
        return subprocess.run(
            ["git", "-C", str(path), *args], check=True, capture_output=True, text=True
        ).stdout.strip()

    commit = git("rev-parse", "HEAD")
    return {
        "name": path.name,
        "SPDXID": f"SPDXRef-git-{path.name}",
        "versionInfo": commit,
        "downloadLocation": f"git+{git('remote', 'get-url', 'origin')}@{commit}",
        **_UNASSERTED,
    }


def spdx_document(name, version, repo, commit, dependencies):
    root = {
        "name": name,
        "SPDXID": "SPDXRef-root",
        "versionInfo": version,
        "downloadLocation": f"git+{repo}@{commit}",
        **_UNASSERTED,
        "primaryPackagePurpose": "APPLICATION",
    }
    relationships = [
        {
            "spdxElementId": "SPDXRef-DOCUMENT",
            "relationshipType": "DESCRIBES",
            "relatedSpdxElement": "SPDXRef-root",
        }
    ] + [
        {
            "spdxElementId": "SPDXRef-root",
            "relationshipType": "DEPENDS_ON",
            "relatedSpdxElement": dependency["SPDXID"],
        }
        for dependency in dependencies
    ]
    created = datetime.datetime.now(datetime.UTC).strftime("%Y-%m-%dT%H:%M:%SZ")
    return {
        "spdxVersion": "SPDX-2.3",
        "dataLicense": "CC0-1.0",
        "SPDXID": "SPDXRef-DOCUMENT",
        "name": f"{name}@{version}",
        # Unique per document, as SPDX requires, under the repository's own URI.
        "documentNamespace": f"{repo}/spdx/{name}-{version}-{commit}",
        "creationInfo": {"creators": ["Tool: augusta-vcpkg-sbom"], "created": created},
        "packages": [root, *dependencies],
        "relationships": relationships,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--name", required=True, help="the executable")
    parser.add_argument("--version", required=True)
    parser.add_argument("--repo", required=True, help="the source repository's URL")
    parser.add_argument("--commit", required=True, help="the commit built")
    parser.add_argument(
        "--git-package",
        action="append",
        default=[],
        type=pathlib.Path,
        help="a dependency checked out with git rather than installed by vcpkg",
    )
    parser.add_argument("installed", type=pathlib.Path, help="vcpkg_installed/<triplet>")
    args = parser.parse_args()
    dependencies = port_packages(args.installed) + [git_package(p) for p in args.git_package]
    json.dump(
        spdx_document(args.name, args.version, args.repo, args.commit, dependencies),
        sys.stdout,
        indent=2,
    )
    sys.stdout.write("\n")


if __name__ == "__main__":
    main()
