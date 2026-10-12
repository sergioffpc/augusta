#!/usr/bin/env python3
r"""An executable's SBOM (SPDX 2.3 JSON) from the vcpkg ports it was built with.

syft finds nothing in a C++ executable: vcpkg links most of it statically, and
what it leaves are DLLs with no package metadata. vcpkg itself writes an SPDX
document for every port it installs (share/<port>/vcpkg.spdx.json); this joins
their port packages, each with its vcpkg purl, under one package for the
executable. It lists every port the manifest installed for the triplet, test
and build-tool ports included - the manifest has one dependency list for
every target. A dependency vcpkg doesn't provide (a submodule: Falcor, NVTX)
is named with --git-package, at its checked-out commit.

The client ships Falcor's own dependencies beside it (ADR-0058), which
--falcor names from the Falcor checkout they were fetched into: the ports of
the vcpkg tree its packman bundle is (external/packman/deps), every packman
package of dependencies.xml, and every FetchPackage URL of
external/CMakeLists.txt. --msvc-runtime names the MSVC runtime the client
bundles, at the version of its vcruntime140.dll.

  scripts/vcpkg-sbom.py --name augustad --version v1.0.0 \
      --repo https://github.com/owner/augusta --commit <commit> \
      build/x64-linux/vcpkg_installed/x64-linux > augustad.spdx.json
  scripts/vcpkg-sbom.py --name augustac ... --falcor third_party/falcor \
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
import xml.etree.ElementTree as ET

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
# The packman platform of the client, the one executable Falcor is in.
_PACKMAN_PLATFORM = "windows-x86_64"
# A FetchPackage URL's version: a release's number (v1.8.2505, 1.717.0-preview,
# v310.1.0.zip), or the commit an archive is of.
_URL_VERSION = re.compile(
    r"/v?(\d+(?:\.\d+)+(?:-[A-Za-z][\w.]*)?)(?:\.zip)?(?=/|$)"
    r"|/([0-9a-f]{40})\.zip$"
)


def port_packages(installed, prefix="vcpkg"):
    """The port package of each vcpkg.spdx.json under installed/share.

    Each is SPDXRef-<prefix>-<port>, so two vcpkg trees' ports stay apart.
    """
    packages = []
    for path in sorted(installed.glob("share/*/vcpkg.spdx.json")):
        document = json.loads(path.read_text(encoding="utf-8"))
        port = next(
            p for p in document["packages"] if p["SPDXID"] == "SPDXRef-port"
        )
        port = {**port, "SPDXID": f"SPDXRef-{prefix}-{port['name']}"}
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


def packman_packages(manifest):
    """A package for each of manifest's packman packages for the client.

    A package named for other platforms only is left out. Its download
    location is its remote's URL when it names one; packman's own servers
    have no public one.
    """
    root = ET.parse(manifest).getroot()
    remotes = {
        remote.get("name"): remote.get("packageLocation")
        for remote in root.iter("remote")
    }
    packages = []
    for dependency in root.iter("dependency"):
        for package in dependency.iter("package"):
            platforms = package.get("platforms")
            if platforms and _PACKMAN_PLATFORM not in platforms.split():
                continue
            version = package.get("version").replace(
                "${platform}", _PACKMAN_PLATFORM
            )
            location = remotes.get(package.get("remotes"))
            if location:
                location = "https://" + location.replace(
                    "${version}", version
                ).replace("${platform}", _PACKMAN_PLATFORM)
            packages.append(
                {
                    "name": package.get("name"),
                    "SPDXID": f"SPDXRef-packman-{dependency.get('name')}",
                    "versionInfo": version,
                    "downloadLocation": location or _NOASSERTION,
                    **_UNASSERTED,
                }
            )
    return packages


def fetched_packages(cmake_lists):
    """A package for each FetchPackage(<name> URL <url>) of cmake_lists.

    A ${VAR} in a URL is what set(VAR "<value>") in the same file gives. A URL
    with no version in it fails, so none is ever listed without one.
    """
    text = cmake_lists.read_text(encoding="utf-8")
    variables = dict(re.findall(r'set\((\w+)\s+"([^"]*)"\)', text))

    def expanded(value):
        while "${" in value:
            value = re.sub(
                r"\$\{(\w+)\}", lambda match: variables[match[1]], value
            )
        return value

    packages = []
    for name, written in re.findall(
        r"FetchPackage\((\w+)\s+URL\s+([^)\s]+)\)", text
    ):
        url = expanded(written.strip('"'))
        match = _URL_VERSION.search(url)
        if not match:
            sys.exit(f"{cmake_lists}: no version in {name}'s URL {url}")
        packages.append(
            {
                "name": name,
                "SPDXID": f"SPDXRef-fetch-{name}",
                "versionInfo": match[1] or match[2],
                "downloadLocation": url,
                **_UNASSERTED,
            }
        )
    return packages


def falcor_packages(checkout):
    """Falcor's own dependencies, from its checkout (the module's doc)."""
    return (
        port_packages(
            checkout / "external" / "packman" / "deps", "falcor-vcpkg"
        )
        + packman_packages(checkout / "dependencies.xml")
        + fetched_packages(checkout / "external" / "CMakeLists.txt")
    )


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
    version.GetFileVersionInfoW(str(dll), 0, size, data)
    info = ctypes.c_void_p()
    length = ctypes.c_uint()
    version.VerQueryValueW(data, "\\", ctypes.byref(info), ctypes.byref(length))
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
        "--falcor",
        type=pathlib.Path,
        help="the Falcor checkout whose dependencies ship beside it",
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
    if args.falcor:
        dependencies += falcor_packages(args.falcor)
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
