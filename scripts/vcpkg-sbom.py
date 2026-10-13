#!/usr/bin/env python3
r"""An executable's SBOM (SPDX 2.3 JSON) from the vcpkg ports it was built with.

syft finds nothing in a C++ executable: vcpkg links most of it statically, and
what it leaves are DLLs with no package metadata. vcpkg itself writes an SPDX
document for every port it installs (share/<port>/vcpkg.spdx.json); this joins
their port packages, each with its vcpkg purl, under one package for the
executable. It lists every port the manifest installed for the triplet, test
and build-tool ports included - the manifest has one dependency list for
every target. A dependency vcpkg doesn't provide (a submodule: NVTX) is named
with --git-package, at its checked-out commit. --msvc-runtime names the MSVC
runtime the client bundles (ADR-0058), at the version of its vcruntime140.dll.

  scripts/vcpkg-sbom.py --name augustad --version v1.0.0 \
      --repo https://github.com/owner/augusta --commit <commit> \
      build/x64-linux/vcpkg_installed/x64-linux > augustad.spdx.json
  scripts/vcpkg-sbom.py --name augustac ... --git-package third_party/nvtx \
      --msvc-runtime <staged>/bin/vcruntime140.dll \
      build/x64-windows/vcpkg_installed/x64-windows > augustac.spdx.json
"""

import argparse
import datetime
import json
import pathlib
import re
import subprocess
import sys

_NOASSERTION = "NOASSERTION"
# The license and copyright fields of a package nothing here makes a claim
# about.
_UNASSERTED = {
    "licenseConcluded": _NOASSERTION,
    "licenseDeclared": _NOASSERTION,
    "copyrightText": _NOASSERTION,
}
# vcpkg writes this for a port with no license, but never declares it, which
# makes the document invalid.
_VCPKG_NO_LICENSE = "LicenseRef-vcpkg-null"


def spdx_id(*parts):
    """SPDXRef-<parts>, with what an SPDX ID can't hold (as _) made -."""
    return "SPDXRef-" + re.sub(r"[^A-Za-z0-9.-]", "-", "-".join(parts))


def port_packages(installed):
    """The port package of each vcpkg.spdx.json under installed/share."""
    packages = []
    for path in sorted(installed.glob("share/*/vcpkg.spdx.json")):
        document = json.loads(path.read_text(encoding="utf-8"))
        port = next(
            p for p in document["packages"] if p["SPDXID"] == "SPDXRef-port"
        )
        port = {**port, "SPDXID": spdx_id("vcpkg", port["name"])}
        for field in ("licenseConcluded", "licenseDeclared"):
            if port[field] == _VCPKG_NO_LICENSE:
                port[field] = _NOASSERTION
        packages.append(port)
    if not packages:
        sys.exit(
            f"no vcpkg.spdx.json under {installed}/share - not a vcpkg "
            f"installed tree"
        )
    return packages


def git_package(path):
    """A package for the git checkout at path, at its HEAD commit."""

    def git(*args):
        return subprocess.run(
            ["git", "-C", str(path), *args],
            check=True,
            capture_output=True,
            text=True,
        ).stdout.strip()

    commit = git("rev-parse", "HEAD")
    origin = git("remote", "get-url", "origin")
    return {
        "name": path.name,
        "SPDXID": f"SPDXRef-git-{path.name}",
        "versionInfo": commit,
        "downloadLocation": f"git+{origin}@{commit}",
        **_UNASSERTED,
    }


def file_version(dll):
    """The file version in a Windows DLL's version resource."""
    # Windows' own version.dll reads it: only the client's SBOM, made on
    # Windows, asks for it.
    import ctypes  # noqa: PLC0415

    version = ctypes.windll.version
    size = version.GetFileVersionInfoSizeW(str(dll), None)
    if not size:
        sys.exit(f"{dll}: no version resource")
    data = ctypes.create_string_buffer(size)
    info = ctypes.c_void_p()
    length = ctypes.c_uint()
    if not version.GetFileVersionInfoW(
        str(dll), 0, size, data
    ) or not version.VerQueryValueW(
        data, "\\", ctypes.byref(info), ctypes.byref(length)
    ):
        sys.exit(f"{dll}: its version resource can't be read")
    # VS_FIXEDFILEINFO's dwFileVersionMS and dwFileVersionLS, after its
    # dwSignature and dwStrucVersion.
    most, least = ctypes.cast(info, ctypes.POINTER(ctypes.c_uint32))[2:4]
    return f"{most >> 16}.{most & 0xFFFF}.{least >> 16}.{least & 0xFFFF}"


def msvc_runtime_package(vcruntime):
    """The MSVC runtime, at the version of its vcruntime140.dll."""
    return {
        "name": "msvc-runtime",
        "SPDXID": "SPDXRef-msvc-runtime",
        "versionInfo": file_version(vcruntime),
        "downloadLocation": _NOASSERTION,
        **_UNASSERTED,
    }


def spdx_document(name, version, repo, commit, dependencies):
    """The SPDX document for executable name, depending on dependencies."""
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
        "creationInfo": {
            "creators": ["Tool: scripts/vcpkg-sbom.py"],
            "created": created,
        },
        "packages": [root, *dependencies],
        "relationships": relationships,
    }


def main():
    """Writes the SBOM the command line describes to stdout."""
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--name", required=True, help="the executable")
    parser.add_argument("--version", required=True)
    parser.add_argument(
        "--repo", required=True, help="the source repository's URL"
    )
    parser.add_argument("--commit", required=True, help="the commit built")
    parser.add_argument(
        "--git-package",
        action="append",
        default=[],
        type=pathlib.Path,
        help="a dependency checked out with git rather than installed by vcpkg",
    )
    parser.add_argument(
        "--msvc-runtime",
        type=pathlib.Path,
        help="the vcruntime140.dll the executable ships with",
    )
    parser.add_argument(
        "installed", type=pathlib.Path, help="vcpkg_installed/<triplet>"
    )
    args = parser.parse_args()
    dependencies = port_packages(args.installed) + [
        git_package(p) for p in args.git_package
    ]
    if args.msvc_runtime:
        dependencies.append(msvc_runtime_package(args.msvc_runtime))
    json.dump(
        spdx_document(
            args.name, args.version, args.repo, args.commit, dependencies
        ),
        sys.stdout,
        indent=2,
    )
    sys.stdout.write("\n")


if __name__ == "__main__":
    main()
