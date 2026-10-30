#!/bin/bash
# Adds or removes a worktree beside the main checkout, under augusta-wt/<name>,
# with the submodules a build needs: third_party/nvtx linked to the main
# checkout's (a junction on Windows), and third_party/vcpkg as a worktree of the
# main checkout's vcpkg at the commit the branch pins, with its tool copied in.
# add then runs `make toolchain`, which links the worktree's .toolchain/ into
# the shared store at the versions its branch pins (ADR-0077); remove takes
# .toolchain/ with the worktree, links and copies only.
#
# Usage:
#   scripts/worktree.sh add <name> <branch> [<base>]
#   scripts/worktree.sh remove <name>
#
# add checks out <branch>, creating it off <base> (origin/develop by default)
# when it does not exist. remove refuses a worktree with uncommitted changes,
# and unlinks nvtx before anything else: a recursive delete that follows the
# link wipes the main checkout's nvtx.

set -euo pipefail

# Prints the main checkout's path, from any of its worktrees.
main_checkout() {
  dirname "$(git rev-parse --path-format=absolute --git-common-dir)"
}

# Whether this shell runs on Windows (Git Bash).
on_windows() {
  case "$(uname -s)" in
    MINGW* | MSYS* | CYGWIN*) return 0 ;;
    *) return 1 ;;
  esac
}

# Links a directory to another, as a junction on Windows.
# Arguments:
#   The link, then its target.
link_dir() {
  if on_windows; then
    # cmd parses its own switches: Git Bash must not rewrite /J into a path.
    MSYS_NO_PATHCONV=1 cmd /c mklink /J "$(cygpath -w "$1")" \
      "$(cygpath -w "$2")" >/dev/null
  else
    ln -s "$2" "$1"
  fi
}

# Removes a link made by link_dir, or the empty directory it replaces, never
# what a link points at: neither command recurses.
# Arguments:
#   The link.
unlink_dir() {
  if on_windows; then
    MSYS_NO_PATHCONV=1 cmd /c rmdir "$(cygpath -w "$1")"
  elif [[ -L "$1" ]]; then
    rm "$1"
  else
    rmdir "$1"
  fi
}

# Arguments:
#   The worktree's name, its branch, and the base a new branch starts from.
add() {
  local name="$1" branch="$2" base="${3:-origin/develop}"
  local main wt
  main=$(main_checkout)
  wt="$(dirname "${main}")/augusta-wt/${name}"

  if git -C "${main}" show-ref --verify --quiet "refs/heads/${branch}"; then
    git -C "${main}" worktree add "${wt}" "${branch}"
  else
    git -C "${main}" worktree add -b "${branch}" "${wt}" "${base}"
  fi

  rmdir "${wt}/third_party/nvtx"
  link_dir "${wt}/third_party/nvtx" "${main}/third_party/nvtx"
  # Through the link, nvtx's .git names its gitdir relative to the main
  # checkout, which from here resolves to nothing: git add -A and -u would
  # fail on it. The worktree never bumps nvtx, so git leaves the entry alone.
  git -C "${wt}" update-index --skip-worktree third_party/nvtx

  local vcpkg sha tool
  vcpkg="${main}/third_party/vcpkg"
  sha=$(git -C "${wt}" ls-tree HEAD third_party/vcpkg | awk '{print $3}')
  git -C "${vcpkg}" cat-file -e "${sha}^{commit}" 2>/dev/null \
    || git -C "${vcpkg}" fetch origin "${sha}"
  rmdir "${wt}/third_party/vcpkg"
  git -C "${vcpkg}" worktree add --detach "${wt}/third_party/vcpkg" "${sha}"
  # Only this platform's tool: a main checkout also built elsewhere (WSL, the
  # dev container) holds both, and Git Bash's cp writes vcpkg onto a vcpkg.exe
  # already there, leaving a Linux binary in its place.
  tool=vcpkg
  if on_windows; then
    tool=vcpkg.exe
  fi
  if [[ -f "${vcpkg}/${tool}" ]]; then
    cp "${vcpkg}/${tool}" "${wt}/third_party/vcpkg/"
  fi

  if [[ -e "${main}/.toolchain/bin" ]]; then
    make -C "${wt}" toolchain
  else
    "${wt}/scripts/dev-container.sh" true
  fi

  echo "worktree: ${wt} on ${branch}"
}

# Arguments:
#   The worktree's name.
remove() {
  local name="$1"
  local main wt
  main=$(main_checkout)
  wt="$(dirname "${main}")/augusta-wt/${name}"

  if [[ -n "$(git -C "${wt}" status --porcelain)" ]]; then
    echo "worktree: ${wt} has uncommitted changes; commit or discard them" \
      "first." >&2
    exit 1
  fi

  # A step already undone is skipped, so a worktree add left half set up is
  # removed too.
  if [[ -e "${wt}/third_party/nvtx" || -L "${wt}/third_party/nvtx" ]]; then
    unlink_dir "${wt}/third_party/nvtx"
  fi
  if [[ -f "${wt}/third_party/vcpkg/.git" ]]; then
    git -C "${main}/third_party/vcpkg" worktree remove --force \
      "${wt}/third_party/vcpkg"
  fi
  # A worktree with a submodule's checkout in it is removed only by force.
  git -C "${main}" worktree remove --force "${wt}"

  echo "worktree: removed ${wt}"
}

usage() {
  echo "usage: scripts/worktree.sh add <name> <branch> [<base>]" >&2
  echo "       scripts/worktree.sh remove <name>" >&2
  exit 2
}

main() {
  case "${1:-}" in
    add)
      if [[ $# -lt 3 || $# -gt 4 ]]; then usage; fi
      add "${@:2}"
      ;;
    remove)
      if [[ $# -ne 2 ]]; then usage; fi
      remove "$2"
      ;;
    *) usage ;;
  esac
}

main "$@"
