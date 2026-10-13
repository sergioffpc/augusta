#!/bin/bash
# Stages the client's whole runtime folder from a Windows build, with
# `cmake --install --component client` (src/client/CMakeLists.txt), and checks
# that augustac runs from it alone. Given a zip, it also zips the folder, unzips
# it into an empty one, and checks that augustac runs from there and, given a
# version, prints it: what a release attaches (release.yml).
#
# The check runs `augustac --version` with only Windows' own folders on PATH,
# so a DLL augustac imports that is missing from the folder fails it rather
# than being found elsewhere. --version exits before the Renderer starts, so a
# DLL loaded only once it runs is not checked. The folder must also hold none
# of the development-only libraries the client never ships (ADR-0059): a
# Vulkan loader, which is the system's, the Vulkan validation layer, lavapipe,
# DXC or Slang.
#
#   scripts/stage-client.sh build/x64-windows dist/augustac
#   scripts/stage-client.sh build/x64-windows dist/augustac \
#     dist/augustac-windows-x64.zip 3.0.0
set -euo pipefail

# The folder the zip is unzipped into, removed on exit.
unzipped=""

usage() {
  echo "usage: $0 <build-dir> <staging-dir> [<zip> [<version>]]" >&2
  exit 2
}

# Runs augustac --version from a folder, with no other folder to load DLLs
# from, and exits 1 if it fails or prints another version.
# Arguments:
#   The folder augustac.exe is in.
#   The version it must print (optional; any version without it).
# Outputs:
#   The folder and what augustac printed, to stdout.
check_runs() {
  local folder="$1"
  local version="${2:-}"
  local system
  system="$(cygpath -u "${SYSTEMROOT}")"
  local printed
  if ! printed="$(cd "${folder}" \
    && PATH="${system}/System32:${system}" ./augustac.exe --version)"; then
    echo "$0: augustac does not run from ${folder}" >&2
    exit 1
  fi
  if [[ -n "${version}" ]] && [[ "${printed}" != "augustac ${version}" ]]; then
    echo "$0: augustac from ${folder} prints '${printed}'," \
      "not 'augustac ${version}'" >&2
    exit 1
  fi
  echo "${folder}: ${printed}"
}

# Exits 1 if a folder holds a library the client never ships.
# Arguments:
#   The folder augustac.exe is in.
check_no_dev_libraries() {
  local folder="$1"
  local found
  found="$(find "${folder}" -type f \( -iname 'vulkan-1*.dll' \
    -o -iname 'VkLayer_*' -o -iname 'vulkan_lvp.dll' \
    -o -iname 'dxcompiler.dll' -o -iname 'dxil.dll' -o -iname 'slang*.dll' \))"
  if [[ -n "${found}" ]]; then
    echo "$0: ${folder} holds libraries the client never ships:" >&2
    echo "${found}" >&2
    exit 1
  fi
}

# Stages and checks the folder, then, given a zip, zips and checks it.
# Globals:
#   unzipped: set to the folder the zip is unzipped into, removed on exit.
# Arguments:
#   The build directory, the staging directory, and optionally the zip and
#   the version it must print.
main() {
  if (($# < 2 || $# > 4)); then
    usage
  fi
  local build_dir="$1"
  local staging_dir="$2"
  local zip="${3:-}"
  local version="${4:-}"

  rm -rf "${staging_dir}"
  cmake --install "${build_dir}" --component client --prefix "${staging_dir}"
  local folder="${staging_dir}/bin"
  check_no_dev_libraries "${folder}"
  check_runs "${folder}"
  [[ -n "${zip}" ]] || return 0

  zip="$(realpath -m "${zip}")"
  mkdir -p "$(dirname "${zip}")"
  rm -f "${zip}"
  (cd "${folder}" && cmake -E tar cf "${zip}" --format=zip .)
  unzipped="$(mktemp -d)"
  trap 'rm -rf "${unzipped}"' EXIT
  (cd "${unzipped}" && cmake -E tar xf "${zip}")
  check_no_dev_libraries "${unzipped}"
  check_runs "${unzipped}" "${version}"
}

main "$@"
