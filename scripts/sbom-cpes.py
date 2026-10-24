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
}
_BOOST_CPE = "boost:boost"

# The packages with no CPE, and why each one is left unscanned.
_NO_CPE = {
    "augustac": "the executable itself",
    "augustad": "the executable itself",
    "blake3": "not in the NVD",
    "flecs": "not in the NVD",
    "glm": "not in the NVD",
    "libsodium": "not in the NVD: its CVEs have no CPE assigned",
    "nvtx": "not in the NVD",
    "ozz-animation": "not in the NVD",
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
    "vcpkg-get-python-packages": "a build tool",
    "shader-slang": "a build tool: compiles the shaders the client embeds",
    # The renderer's (ADR-0059), and the ports NVRHI pulls in.
    "glfw3": "not in the NVD",
    "imgui": "not in the NVD",
    "nvrhi": "not in the NVD",
    "directx-headers": "not in the NVD",
    "vulkan-headers": "not in the NVD",
    # GLFW's X11 and Wayland ports on Linux, and what they are built with:
    # GLFW loads the system's X11 and Wayland libraries at runtime, so their
    # code is in no executable shipped, only their headers (ADR-0058).
    "libx11": "headers only: the client loads the system's at runtime",
    "libxcb": "headers only: the client loads the system's at runtime",
    "libxau": "libxcb's, linked into no executable shipped",
    "libxdmcp": "libxcb's, linked into no executable shipped",
    "wayland": "headers and wayland-scanner: the client loads the "
    "system's library at runtime",
    "libxml2": "a build tool's: wayland-scanner's and libxcb's",
    "libxslt": "a build tool's: libxcb's",
    "libiconv": "a build tool's: libxml2's",
    "xcb-proto": "a build tool: libxcb's",
    "xcb-util-m4": "a build tool: libxcb's",
    "xorg-macros": "a build tool: the X11 ports'",
    "xproto": "headers only: the X11 ports'",
    "xtrans": "headers only: libx11's",
    "pthread": "vcpkg's stand-in for the system's pthreads",
    "pthreads": "vcpkg's stand-in for the system's pthreads",
    "pthread-stubs": "libxcb's, linked into no executable shipped",
    # Vulkan's validation layer, which Debug builds and the renderer smoke test
    # load, and the ports it pulls in (ADR-0059): never in the client's zip.
    "vulkan-validationlayers": "a development layer, shipped in no package",
    "vulkan-utility-libraries": "the validation layer's, shipped in no package",
    "spirv-cross": "the validation layer's, shipped in no package",
    "spirv-headers": "the validation layer's, shipped in no package",
    "spirv-reflect": "the validation layer's, shipped in no package",
    "spirv-tools": "the validation layer's, shipped in no package",
    "mimalloc": "the validation layer's, shipped in no package",
    "parallel-hashmap": "the validation layer's, shipped in no package",
    # The manifest's lavapipe feature: Mesa's software Vulkan driver, which the
    # renderer smoke test draws on, the ports it pulls in, and a Vulkan loader
    # for a test machine without one.
    "mesa": "a test port, linked into no executable shipped",
    "llvm": "mesa's, linked into no executable shipped",
    "glslang": "a build tool: mesa's",
    "atl": "a build tool: llvm's, from Visual Studio",
    "vulkan-loader": "a test port: the client uses the system's loader",
    # The manifest's agent feature, which CI turns on for the Agents' Python
    # module (tools/agent) and a release doesn't: pybind11 and the python3
    # port it pulls in, with python3's own dependencies.
    "pybind11": "the Agents' module's, linked into no executable shipped",
    "python3": "pybind11's, linked into no executable shipped",
    "bzip2": "python3's and libxml2's, linked into no executable shipped",
    "expat": "python3's, linked into no executable shipped",
    "libffi": "python3's, linked into no executable shipped",
    "liblzma": "python3's and libxml2's, linked into no executable shipped",
    "mpdecimal": "python3's, linked into no executable shipped",
    "sqlite3": "python3's, linked into no executable shipped",
    "zstd": "python3's and ktx's, linked into no executable shipped",
    # The manifest's pack feature, which CI turns on beside the agent feature
    # for the cooker's Python module (tools/pack) and a release doesn't: its
    # mesh and texture libraries, with ktx's own dependencies.
    "meshoptimizer": "the cooker's module's, linked into no executable shipped",
    "ktx": "the cooker's module's, linked into no executable shipped",
    "stb": "the cooker's module's, linked into no executable shipped",
    "opengl-registry": "ktx's, linked into no executable shipped",
    "egl-registry": "ktx's, linked into no executable shipped",
    "cxxopts": "ktx's tools', linked into no executable shipped",
    "fmt": "ktx's tools', linked into no executable shipped",
    # What the client's zip ships beside it (ADR-0058): the MSVC runtime.
    "msvc-runtime": "its CPE, microsoft:visual_c++, names Visual Studio's "
    "releases, not the runtime's file versions",
}


def nvd_version(name, version):
    """The NVD's version for the package at its SBOM version."""
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
