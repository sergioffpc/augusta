#!/bin/bash
# Sets up the optional NVIDIA Omniverse USD Composer authoring tools under a
# caller-chosen assets root, on Windows (in Git Bash) or Linux. Run after the
# repository's scripts/bootstrap.sh has installed Git, CMake and Ninja.
# Composer is an authoring-time tool only; it is never linked into the shipped
# client/server binaries.
#
# Usage: tools/composer/scripts/bootstrap.sh [--accept-omniverse-eula] <assets-root>
#   --accept-omniverse-eula accepts NVIDIA's Omniverse license terms without
#   the interactive prompt (for unattended runs by someone who has already
#   read them).
set -euo pipefail

usage() {
  echo "usage: $0 [--accept-omniverse-eula] <assets-root>" >&2
  exit 2
}

# Prints kit-app-template's repo tool for this platform: repo.bat on Windows,
# repo.sh elsewhere. Git Bash runs a .bat through cmd by itself.
repo_tool() {
  case "$(uname -s)" in
    MINGW* | MSYS* | CYGWIN*) echo ./repo.bat ;;
    *) echo ./repo.sh ;;
  esac
}

# Clones a repository, or fetches it if it is already there.
# Arguments:
#   The URL, the path.
sync_git_repo() {
  if [[ -d "$2" ]]; then
    echo "Updating $2..."
    git -C "$2" fetch --tags --quiet
  else
    echo "Cloning $1..."
    git clone --quiet "$1" "$2"
  fi
}

# Asks for NVIDIA's Omniverse terms once per checkout, leaving a breadcrumb.
# Arguments:
#   Whether they were accepted on the command line (true or false).
accept_omniverse_eula() {
  local breadcrumb=.omniverse_eula_accepted.txt
  [[ -f "${breadcrumb}" ]] && return
  echo "The Omniverse Kit App Template is governed by the NVIDIA Software" \
    "License Agreement and the Product-Specific Terms for NVIDIA Omniverse:"
  echo "  https://www.nvidia.com/en-us/agreements/enterprise-software/nvidia-software-license-agreement/"
  echo "  https://www.nvidia.com/en-us/agreements/enterprise-software/product-specific-terms-for-omniverse/"
  if [[ "$1" != true ]]; then
    local answer
    read -r -p "Do you accept the governing terms? (yes/no) " answer
    if [[ ! "${answer}" =~ ^(y|yes)$ ]]; then
      echo "bootstrap: Omniverse terms not accepted - re-run with" \
        "--accept-omniverse-eula after accepting them." >&2
      exit 1
    fi
  fi
  touch "${breadcrumb}"
}

main() {
  local accept_eula=false
  if [[ "${1:-}" = --accept-omniverse-eula ]]; then
    accept_eula=true
    shift
  fi
  (($# == 1)) || usage
  local assets_root="$1"
  local composer_project tools_dir bin_dir kit_app_template_dir adobe_plugins_dir
  composer_project="$(cd "$(dirname "$0")/.." && pwd)"
  tools_dir="${assets_root}/tools"
  bin_dir="${assets_root}/bin"
  kit_app_template_dir="${tools_dir}/kit-app-template"
  adobe_plugins_dir="${tools_dir}/USD-Fileformat-plugins"

  local missing=() command
  for command in git cmake ninja; do
    command -v "${command}" >/dev/null 2>&1 || missing+=("${command}")
  done
  if ((${#missing[@]} > 0)); then
    echo "bootstrap: these commands are required but aren't on PATH:" \
      "${missing[*]}. Run scripts/bootstrap.sh first, then re-run this" \
      "script." >&2
    exit 1
  fi

  mkdir -p "${tools_dir}" "${bin_dir}"

  # The old Omniverse Launcher was deprecated (Oct 2025); Composer is now
  # built from the Kit App Template repo.
  sync_git_repo https://github.com/NVIDIA-Omniverse/kit-app-template.git \
    "${kit_app_template_dir}"

  local repo
  repo="$(repo_tool)"
  (
    cd "${kit_app_template_dir}"
    # Replay the checked-in app definition instead of the interactive
    # template wizard, so its name/version/setup extension stay fixed.
    # Existing apps may have been customized, so only replay when the app is
    # absent.
    if [[ ! -e source/apps/augusta.kit ]]; then
      accept_omniverse_eula "${accept_eula}"
      echo "Scaffolding the Augusta USD Composer app (repo template replay)..."
      "${repo}" template replay "${composer_project}/augusta.playback.toml"
    fi
    # `repo build` fetches the Kit SDK on its first run and is not
    # interactive.
    echo "Building USD Composer (augusta.kit)..."
    "${repo}" build
  )

  install -m 0755 "${composer_project}/augusta-composer.sh" "${bin_dir}"

  # glTF/FBX/OBJ ingestion (ADR-0016) is kept as a separate checkout; the
  # cooker does not consume it yet.
  sync_git_repo https://github.com/adobe/USD-Fileformat-plugins.git \
    "${adobe_plugins_dir}"

  echo
  echo "USD Composer ready under ${assets_root}:"
  echo "  - ${kit_app_template_dir}: Composer app source and build"
  echo "  - ${adobe_plugins_dir}: Adobe USD-Fileformat-plugins"
  echo "  - ${bin_dir}/augusta-composer.sh: launch Composer"
}

main "$@"
