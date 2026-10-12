#!/bin/bash
# Stages the client's whole runtime folder from a Windows build, with
# `cmake --install --component client` (src/client/CMakeLists.txt), and checks
# that augustac starts from it alone. Given a zip and a version, it also zips the
# folder, unzips it into an empty one, and checks that augustac starts from there
# and prints that version: what a release attaches (release.yml).
#
# The check runs `augustac --version` with only Windows' own folders on PATH, so
# a DLL missing from the folder fails it rather than being found elsewhere.
#
#   scripts/stage-client.sh build/x64-windows dist/augustac
#   scripts/stage-client.sh build/x64-windows dist/augustac \
#     dist/augustac-windows-x64.zip 3.0.0
set -euo pipefail

# The folder the zip is unzipped into, removed on exit.
unzipped=""

usage() {
  echo "usage: $0 <build-dir> <staging-dir> [<zip> <version>]" >&2
  exit 2
}

# Runs augustac --version from a folder, with no other folder to load DLLs from.
# Arguments:
#   The folder augustac.exe is in.
#   The version it must print, or empty for any.
check_starts() {
  local folder="$1"
  local version="$2"
  local system
  system="$(cygpath -u "${SYSTEMROOT}")"
  local printed
  if ! printed="$(cd "${folder}" \
    && PATH="${system}/System32:${system}" ./augustac.exe --version)"; then
    echo "$0: augustac does not start from ${folder}" >&2
    exit 1
  fi
  if [[ -n "${version}" ]] && [[ "${printed}" != "augustac ${version}" ]]; then
    echo "$0: augustac from ${folder} prints '${printed}'," \
      "not 'augustac ${version}'" >&2
    exit 1
  fi
  echo "${folder}: ${printed}"
}

main() {
  [[ $# -eq 2 ]] || [[ $# -eq 4 ]] || usage
  local build_dir="$1"
  local staging_dir="$2"
  local zip="${3:-}"
  local version="${4:-}"

  rm -rf "${staging_dir}"
  cmake --install "${build_dir}" --component client --prefix "${staging_dir}"
  local folder="${staging_dir}/bin"
  check_starts "${folder}" ""
  [[ -n "${zip}" ]] || return 0

  zip="$(realpath -m "${zip}")"
  mkdir -p "$(dirname "${zip}")"
  rm -f "${zip}"
  (cd "${folder}" && cmake -E tar cf "${zip}" --format=zip .)
  unzipped="$(mktemp -d)"
  trap 'rm -rf "${unzipped}"' EXIT
  (cd "${unzipped}" && cmake -E tar xf "${zip}")
  check_starts "${unzipped}" "${version}"
}

main "$@"
