#!/bin/bash
# Stages the client's whole runtime folder from a Windows build, with
# `cmake --install --component client` (src/client/CMakeLists.txt), and checks
# that augustac runs from it alone. Given a zip and a version, it also zips the
# folder, unzips it into an empty one, and checks that augustac runs from there
# and prints that version: what a release attaches (release.yml).
#
# The check runs `augustac --version` with only Windows' own folders on PATH,
# so a DLL augustac imports that is missing from the folder fails it rather
# than being found elsewhere. --version exits before Falcor starts: what Falcor
# loads at runtime (its shaders, data, plugins and the DLLs it loads itself) is
# not checked.
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

# Stages and checks the folder, then, given a zip, zips and checks it.
# Globals:
#   unzipped: set to the folder the zip is unzipped into, removed on exit.
# Arguments:
#   The build directory, the staging directory, and optionally the zip and
#   the version it must print.
main() {
  [[ $# -eq 2 ]] || [[ $# -eq 4 ]] || usage
  local build_dir="$1"
  local staging_dir="$2"
  local zip="${3:-}"
  local version="${4:-}"

  rm -rf "${staging_dir}"
  cmake --install "${build_dir}" --component client --prefix "${staging_dir}"
  local folder="${staging_dir}/bin"
  check_runs "${folder}"
  [[ -n "${zip}" ]] || return 0

  zip="$(realpath -m "${zip}")"
  mkdir -p "$(dirname "${zip}")"
  rm -f "${zip}"
  (cd "${folder}" && cmake -E tar cf "${zip}" --format=zip .)
  unzipped="$(mktemp -d)"
  trap 'rm -rf "${unzipped}"' EXIT
  (cd "${unzipped}" && cmake -E tar xf "${zip}")
  check_runs "${unzipped}" "${version}"
}

main "$@"
