#!/bin/bash
# Turns the nightly's ctest results (JUnit XML, one file per run) and its
# llvm-cov report (scripts/coverage-report.sh) into one browsable test report
# (ADR-0013), which the documentation site publishes under tests/ (ADR-0046):
#
#   index.html    each run's test counts and the coverage totals, linking to
#   results.html  a row per test, a column per run, each result linked to its
#                 output (junit2html)
#   coverage/     the llvm-cov report as it is
#
# A report, never a gate: nothing here fails on a failed test or a percentage.
# Either input may be missing (a job that never got to its tests); the report
# then says so.
#
#   scripts/test-report.sh <junit-dir> <coverage-dir> <output-dir>
set -euo pipefail

readonly JUNIT2HTML="junit2html==31.1.4"

# Prints the value of a JUnit XML file's first attribute of that name.
# Arguments:
#   The attribute's name.
#   The file.
attribute() {
  grep -m1 -o "$1=\"[0-9]*\"" "$2" | grep -o '[0-9]*' || echo 0
}

# Prints the table rows of each run's test counts.
# Arguments:
#   The JUnit XML files.
results_rows() {
  local result
  for result in "$@"; do
    printf '<tr><td>%s</td><td>%s</td><td>%s</td><td>%s</td></tr>\n' \
      "$(basename "${result}" .xml)" "$(attribute tests "${result}")" \
      "$(attribute failures "${result}")" "$(attribute disabled "${result}")"
  done
}

# Prints the table row of the coverage totals: llvm-cov report's TOTAL line,
# whose percentages are regions, functions, lines and branches, in that order.
# Arguments:
#   The llvm-cov summary.
coverage_row() {
  awk '$1 == "TOTAL" {
    printf "<tr><td>%s</td><td>%s</td><td>%s</td><td>%s</td></tr>\n", $10, $7, $13, $4
  }' "$1"
}

main() {
  local junit coverage="$2" output="$3"
  mkdir -p "${output}"
  # Absolute, since junit2html runs in the output directory.
  junit="$(realpath -m "$1")"

  shopt -s nullglob
  local results=("${junit}"/*.xml)
  local results_section coverage_section
  if [[ "${#results[@]}" -gt 0 ]]; then
    # junit2html writes each run's own page, <run>.xml.html, into the
    # current directory, beside the matrix that links to them.
    (cd "${output}" && uv tool run --from "${JUNIT2HTML}" junit2html \
      --report-matrix results.html "${results[@]}")
    results_section="<table><tr><th>Run</th><th>Tests</th><th>Failures</th><th>Disabled</th></tr>
$(results_rows "${results[@]}")
</table>
<p><a href=\"results.html\">Every test's result</a></p>"
  else
    results_section="<p>No test results: no run got as far as its tests.</p>"
  fi

  if [[ -f "${coverage}/index.html" && -f "${coverage}/summary.txt" ]]; then
    cp -r "${coverage}" "${output}/coverage"
    coverage_section="<table><tr><th>Lines</th><th>Functions</th><th>Branches</th><th>Regions</th></tr>
$(coverage_row "${coverage}/summary.txt")
</table>
<p><a href=\"coverage/index.html\">Coverage by file and line</a></p>"
  else
    coverage_section="<p>No coverage report: the coverage run did not produce one.</p>"
  fi

  cat >"${output}/index.html" <<EOF
<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Test report</title>
<style>
body { font-family: system-ui, sans-serif; margin: 2rem auto; max-width: 48rem; padding: 0 1rem; }
table { border-collapse: collapse; }
th, td { border: 1px solid #ccc; padding: 0.25rem 0.75rem; text-align: right; }
th:first-child, td:first-child { text-align: left; }
</style>
</head>
<body>
<h1>Test report</h1>
<p>The nightly's tests, generated $(date -u +"%Y-%m-%d %H:%M UTC").</p>
<h2>Tests</h2>
${results_section}
<h2>Coverage</h2>
${coverage_section}
</body>
</html>
EOF
}

main "$@"
