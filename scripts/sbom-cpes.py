#!/usr/bin/env python3
r"""An executable's SBOM with each dependency's NVD CPE, for grype to match.

No vulnerability database has advisories for vcpkg purls (pkg:vcpkg), so a
scanner given scripts/vcpkg-sbom.py's SBOM as it is matches nothing and
reports it clean. grype also matches a package's CPE against the NVD, so this
adds a cpe23Type reference to every package that has a CPE there.

Every package must be in _CPES or _NO_CPE: one in neither fails, so a new
dependency is never left out of the scan without a reason saying why.

  scripts/sbom-cpes.py augustad-linux-x64.spdx.json > augustad.cpes.spdx.json
"""

import argparse
import json
import pathlib
import sys

# Each port's NVD vendor:product, looked up in the NVD's CPE dictionary.
# boost's ports, one per library, all release under one CPE.
_CPES = {
    "abseil": "abseil:common_libraries",
    "flatbuffers": "google:flatbuffers",
    "gamenetworkingsockets": "valvesoftware:game_networking_sockets",
    "libmysofa": "symonics:libmysofa",
    "lua": "lua:lua",
    "miniaudio": "mackron:miniaudio",
    "openssl": "openssl:openssl",
    "protobuf": "google:protobuf",
    "yaml-cpp": "yaml-cpp_project:yaml-cpp",
    "zlib": "zlib:zlib",
}
_BOOST_CPE = "boost:boost"

# The packages with no CPE, and why each one is left unscanned.
_NO_CPE = {
    "augustac": "the executable itself",
    "augustad": "the executable itself",
    "blake3": "not in the NVD",
    "falcor": "not in the NVD",
    "flecs": "not in the NVD",
    "glm": "not in the NVD",
    "libsodium": "not in the NVD: its CVEs have no CPE assigned",
    "nvtx": "not in the NVD",
    "pffft": "not in the NVD",
    "physx": "not in the NVD (nvidia:physx_system_software is the driver)",
    "prometheus-cpp": "not in the NVD",
    "sol2": "not in the NVD",
    "steam-audio": "not in the NVD",
    "utf8-range": "built from protobuf's sources, scanned as protobuf",
    "benchmark": "a test port, linked into no executable shipped",
    "gtest": "a test port, linked into no executable shipped",
    "rapidcheck": "a test port, linked into no executable shipped",
    "pkgconf": "a build tool",
    "vcpkg-boost": "a build tool",
    "vcpkg-cmake": "a build tool",
    "vcpkg-cmake-config": "a build tool",
    "vcpkg-cmake-get-vars": "a build tool",
    "vcpkg-make": "a build tool",
    "vcpkg-msbuild": "a build tool",
    "vcpkg-pkgconfig-get-modules": "a build tool",
    "vcpkg-tool-meson": "a build tool",
}


def nvd_version(name, version):
    """The NVD's version for the port at vcpkg's version."""
    # vcpkg's port revision ("#2") is the recipe's, not the library's.
    version = version.split("#")[0]
    # protobuf's C++ library is 6.33.4 where its release, as the NVD names
    # it, is 33.4.
    if name == "protobuf":
        version = version.split(".", 1)[1]
    return version


def cpe(name, version):
    """The CPE 2.3 name of the port at version, or None if it has none."""
    if name in _CPES:
        vendor_product = _CPES[name]
    elif name.startswith("boost-"):
        vendor_product = _BOOST_CPE
    elif name in _NO_CPE:
        return None
    else:
        sys.exit(
            f"{name}: neither a CPE nor a reason for none - add it to "
            f"_CPES or _NO_CPE in {pathlib.Path(__file__).name}"
        )
    return (
        f"cpe:2.3:a:{vendor_product}:{nvd_version(name, version)}:*:*:*:*:*:*:*"
    )


def with_cpe(package):
    """package, with a cpe23Type reference if it has a CPE."""
    name = cpe(package["name"], package["versionInfo"])
    if not name:
        return package
    reference = {
        "referenceCategory": "SECURITY",
        "referenceType": "cpe23Type",
        "referenceLocator": name,
    }
    return {
        **package,
        "externalRefs": [*package.get("externalRefs", []), reference],
    }


def with_cpes(document):
    """document, with a cpe23Type reference on every package that has one."""
    return {
        **document,
        "packages": [with_cpe(p) for p in document["packages"]],
    }


def main():
    """Writes the SBOM on the command line, with its CPEs, to stdout."""
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("sbom", type=pathlib.Path, help="an SPDX 2.3 JSON SBOM")
    args = parser.parse_args()
    document = json.loads(args.sbom.read_text(encoding="utf-8"))
    json.dump(with_cpes(document), sys.stdout, indent=2)
    sys.stdout.write("\n")


if __name__ == "__main__":
    main()
