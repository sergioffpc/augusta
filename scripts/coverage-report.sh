#!/bin/bash
# Turns a `ctest --preset linux-coverage` run into a browsable llvm-cov HTML
# report (ADR-0013): a report for finding untested deterministic logic, never a
# gate, so nothing here fails on a percentage. Beside it, the same coverage as
# LCOV (coverage.lcov), which an editor shows in its gutters: VS Code's CMake
# Tools runs this script through the coverage-report target after "Run Tests
# with Coverage" (.vscode/settings.json).
#
#   cmake --preset linux-coverage && cmake --build --preset linux-coverage
#   ctest --preset linux-coverage
#   scripts/coverage-report.sh [output-dir]
#
# output-dir defaults to build/x64-linux-coverage/report.
set -euo pipefail

readonly BUILD="build/x64-linux-coverage"
# Only the code under test: not the tests themselves, nor third-party code.
readonly IGNORE='(/tests/|/third_party/|/vcpkg_installed/)'

# Prints the llvm tool's name, versioned as the clang that compiled the build
# if that is on PATH: a profile's format is tied to the compiler's version,
# and Ubuntu only puts the versioned names on PATH for some installs.
# Arguments:
#   The tool's unversioned name.
tool() {
  local version
  version="$(clang -dumpversion | cut -d. -f1)"
  if command -v "$1-${version}" >/dev/null; then
    echo "$1-${version}"
  else
    echo "$1"
  fi
}

main() {
  local output="${1:-${BUILD}/report}"
  local profdata cov
  profdata="$(tool llvm-profdata)"
  cov="$(tool llvm-cov)"

  shopt -s nullglob
  local profiles=("${BUILD}"/profiles/*.profraw)
  if [[ "${#profiles[@]}" -eq 0 ]]; then
    echo "no profiles in ${BUILD}/profiles:" \
      "run ctest --preset linux-coverage first" >&2
    exit 1
  fi
  "${profdata}" merge -sparse "${profiles[@]}" \
    -o "${BUILD}/coverage.profdata"

  # Every executable ctest ran: the test binaries and the fuzz targets'
  # replays.
  local binaries
  mapfile -t binaries < <(find "${BUILD}/tests" -type f -executable \
    -name 'augusta_*' ! -name '*.*' | sort)
  local objects=("${binaries[0]}")
  local binary
  for binary in "${binaries[@]:1}"; do
    objects+=(-object "${binary}")
  done

  local common=("${objects[@]}"
    -instr-profile="${BUILD}/coverage.profdata"
    -ignore-filename-regex="${IGNORE}")
  "${cov}" show "${common[@]}" -format=html -output-dir="${output}" \
    -show-line-counts-or-regions
  # Absolute source paths, as the editor matches them against its open files.
  "${cov}" export "${common[@]}" -format=lcov >"${output}/coverage.lcov"
  "${cov}" report "${common[@]}" | tee "${output}/summary.txt"
}

main "$@"
