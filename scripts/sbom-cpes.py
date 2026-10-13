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
import re
import sys

# Each package's NVD vendor:product, looked up in the NVD's CPE dictionary.
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
    # Falcor's own, which the client ships beside it (ADR-0058): its vcpkg
    # tree's ports and its packman packages.
    "assimp": "assimp:assimp",
    "bzip2": "bzip:bzip2",
    "freeimage": "freeimage_project:freeimage",
    "hdf5": "hdfgroup:hdf5",
    "jasper": "jasper_project:jasper",
    "lcms": "littlecms:little_cms_color_engine",
    "libjpeg-turbo": "libjpeg-turbo:libjpeg-turbo",
    "liblzma": "tukaani:xz",
    "libpng": "libpng:libpng",
    "libwebp": "webmproject:libwebp",
    "lz4": "lz4_project:lz4",
    "nv-usd": "pixar:openusd",
    "openexr": "openexr:openexr",
    "openjpeg": "uclouvain:openjpeg",
    "pugixml": "pugixml_project:pugixml",
    "python": "python:python",
    "snappy": "google:snappy",
    "tbb": "intel:threading_building_blocks",
    "tiff": "libtiff:libtiff",
    "zstd": "facebook:zstandard",
}
_BOOST_CPE = "boost:boost"
# Packman's packages whose version carries NVIDIA's build and platform after
# the library's own: python's 3.10.11+nv1-windows-x86_64, nv-usd's
# 22.11.nv.0.2.424.27f22922-win64_...
_PACKMAN_VERSIONED = {"python", "nv-usd"}

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
    "vcpkg-get-python": "a build tool",
    "shader-slang": "a build tool: compiles the shaders the client embeds",
    "directx-dxc": "a build tool: compiles the shaders the client embeds",
    # The NVRHI renderer's (ADR-0059), and the ports NVRHI pulls in.
    "glfw3": "not in the NVD",
    "imgui": "not in the NVD",
    "nvrhi": "not in the NVD",
    "directx-headers": "not in the NVD",
    "vulkan-headers": "not in the NVD",
    # The manifest's agent feature, which CI turns on for the Agents' Python
    # module (tools/agent) and a release doesn't: pybind11 and the python3
    # port it pulls in, with python3's own dependencies.
    "pybind11": "the Agents' module's, linked into no executable shipped",
    "python3": "pybind11's, linked into no executable shipped",
    "expat": "python3's, linked into no executable shipped",
    "libffi": "python3's, linked into no executable shipped",
    "mpdecimal": "python3's, linked into no executable shipped",
    "sqlite3": "python3's, linked into no executable shipped",
    # What the client's zip ships beside it (ADR-0058): the MSVC runtime, and
    # Falcor's own vcpkg ports, packman packages and fetched SDKs.
    "msvc-runtime": "its CPE, microsoft:visual_c++, names Visual Studio's "
    "releases, not the runtime's file versions",
    "blosc": "not in the NVD (only c-blosc2 is)",
    "minizip": "zlib's contrib/minizip, versioned and scanned as zlib "
    "(minizip_project:minizip is another library)",
    "draco": "not in the NVD",
    "freeglut": "not in the NVD",
    "imath": "not in the NVD",
    "jxrlib": "not in the NVD",
    "kubazip": "not in the NVD",
    "libraw": "versioned by date (201903), which the NVD's versions are not",
    "opengl": "the system's OpenGL, which vcpkg's port only finds",
    "opensubdiv": "not in the NVD",
    "openvdb": "not in the NVD",
    "poly2tri": "not in the NVD",
    "rapidjson": "versioned by date, which the NVD's versions are not",
    "stb": "versioned by date, which the NVD's versions are not",
    "szip": "not in the NVD",
    "utfcpp": "not in the NVD",
    "WinPixEventRuntime": "not in the NVD",
    "nanovdb": "not in the NVD",
    "nrd": "not in the NVD",
    "nvtt": "not in the NVD",
    "rtxdi": "not in the NVD",
    "slang": "not in the NVD",
    "agility_sdk": "not in the NVD",
    "dlss": "not in the NVD",
    "dxc": "not in the NVD",
    "nvapi": "not in the NVD",
}


def nvd_version(name, version):
    """The NVD's version for the package at its SBOM version."""
    # vcpkg's port revision ("#2") is the recipe's, not the library's.
    version = version.split("#")[0]
    # protobuf's C++ library is 6.33.4 where its release, as the NVD names
    # it, is 33.4.
    if name == "protobuf":
        version = version.split(".", 1)[1]
    if name in _PACKMAN_VERSIONED:
        library = re.match(r"\d+(?:\.\d+)*", version)
        if not library:
            sys.exit(f"{name}: no library version in packman's {version}")
        version = library[0]
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
