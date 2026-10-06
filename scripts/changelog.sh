#!/bin/bash
# A release's CHANGELOG.md section. Cutting it, on the release/* or hotfix/*
# branch before it is merged to main, has git-cliff (cliff.toml) generate it
# from the commits since the previous release; release.yml prints it with
# --notes as the GitHub Release's notes.
#
#   scripts/changelog.sh v1.1.0           # cut v1.1.0's section
#   scripts/changelog.sh v1.1.0 --notes   # print it, failing if never cut
set -euo pipefail

readonly RELEASE='v[0-9]+\.[0-9]+\.[0-9]+'

usage() {
  echo "usage: $0 vX.Y.Z [--notes]" >&2
  exit 2
}

# Prints CHANGELOG.md's lines inside (1) or outside (0) a version's section,
# which runs from its `## X.Y.Z` heading up to the next release's.
# Arguments:
#   The version, vX.Y.Z.
#   1 for the lines inside, 0 for the lines outside.
section() {
  awk -v heading="## ${1#v} " -v keep="$2" '
  index($0, "## ") == 1 { inside = index($0, heading) == 1 }
  (inside + 0) == keep' CHANGELOG.md
}

main() {
  local version="${1:-}"
  local mode="${2:-}"
  echo "${version}" | grep -qxE "${RELEASE}" || usage
  [[ -z "${mode}" ]] || [[ "${mode}" = "--notes" ]] || usage

  if [[ "${mode}" = "--notes" ]]; then
    local notes
    notes="$(section "${version}" 1 | tail -n +2)"
    if [[ -z "${notes}" ]]; then
      echo "$0: CHANGELOG.md has no ${version} section;" \
        "cut it on the release branch." >&2
      exit 1
    fi
    echo "${notes}"
    exit 0
  fi

  # The previous release is the tag sorting just below this version, not the
  # nearest one in the history: a tag sits on main's merge commit, which
  # develop, and so a release/* branch, never contains.
  local previous
  previous="$({
    git tag --list 'v*' | grep -xE "${RELEASE}" || true
    echo "${version}"
  } \
    | sort -uV | grep -B1 -xF "${version}" | head -n 1)"
  local range=()
  if [[ "${previous}" != "${version}" ]]; then
    range=("${previous}..HEAD")
  fi

  local cliff=(uvx git-cliff@2.14.2 "${range[@]}" --tag "${version}")
  if [[ -f CHANGELOG.md ]]; then
    # A re-cut replaces the earlier one.
    local rest
    rest="$(section "${version}" 0)"
    echo "${rest}" >CHANGELOG.md
    "${cliff[@]}" --prepend CHANGELOG.md
  else
    "${cliff[@]}" --output CHANGELOG.md
  fi
}

main "$@"
