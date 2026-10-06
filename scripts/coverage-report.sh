#!/usr/bin/env bash
# Turns a `ctest --preset linux-coverage` run into a browsable llvm-cov HTML
# report (ADR-0013): a report for finding untested deterministic logic, never a
# gate, so nothing here fails on a percentage. Beside it, the same coverage as
# LCOV (coverage.lcov), which an editor shows in its gutters: VS Code's CMake
# Tools runs this script through the coverage-report target after "Run Tests
# with Coverage" (.vscode/settings.json).
#
#   cmake --preset linux-coverage && cmake --build --preset linux-coverage
#   ctest --preset linux-coverage
#   scripts/coverage-report.sh [output-dir]   # default: build/x64-linux-coverage/report
set -euo pipefail

build="build/x64-linux-coverage"
output="${1:-$build/report}"

# The llvm tools of the clang that compiled the build: a profile's format is
# tied to the compiler's version, and Ubuntu only puts the versioned names on PATH
# for some installs.
version="$(clang -dumpversion | cut -d. -f1)"
tool() {
  if command -v "$1-$version" >/dev/null; then echo "$1-$version"; else echo "$1"; fi
}
profdata="$(tool llvm-profdata)"
cov="$(tool llvm-cov)"

shopt -s nullglob
profiles=("$build"/profiles/*.profraw)
if [ "${#profiles[@]}" -eq 0 ]; then
  echo "no profiles in $build/profiles: run ctest --preset linux-coverage first" >&2
  exit 1
fi
"$profdata" merge -sparse "${profiles[@]}" -o "$build/coverage.profdata"

# Every executable ctest ran: the test binaries and the fuzz targets' replays.
mapfile -t binaries < <(find "$build/tests" -type f -executable -name 'augusta_*' ! -name '*.*' | sort)
objects=("${binaries[0]}")
for binary in "${binaries[@]:1}"; do
  objects+=(-object "$binary")
done

# Only the code under test: not the tests themselves, nor third-party code.
ignore='(/tests/|/third_party/|/vcpkg_installed/)'
"$cov" show "${objects[@]}" -instr-profile="$build/coverage.profdata" \
  -format=html -output-dir="$output" -ignore-filename-regex="$ignore" -show-line-counts-or-regions
# Absolute source paths, as the editor matches them against its open files.
"$cov" export "${objects[@]}" -instr-profile="$build/coverage.profdata" \
  -format=lcov -ignore-filename-regex="$ignore" >"$output/coverage.lcov"
"$cov" report "${objects[@]}" -instr-profile="$build/coverage.profdata" \
  -ignore-filename-regex="$ignore" | tee "$output/summary.txt"
