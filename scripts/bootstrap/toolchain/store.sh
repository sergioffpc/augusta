# shellcheck shell=bash
# shellcheck disable=SC2154 # describe_tool (tools.sh) sets the tool's globals
# Fills the toolchain's shared store, for scripts/bootstrap/toolchain.sh, which
# sources this: each pinned version it lacks, from PyPI with uv, from its
# release download or from its LuaRocks rock, and npm's download cache with
# Prettier's packages.

# Checks a file's SHA-256: GNU's sha256sum, or on macOS, whose own reads no
# list from stdin, shasum.
# Arguments:
#   The SHA-256, the file.
sha256_matches() {
  if [[ "${PLATFORM}" = macos ]]; then
    printf '%s  %s\n' "$1" "$2" | shasum -a 256 --check --status
  else
    printf '%s  %s\n' "$1" "$2" | sha256sum --check --status
  fi
}

# Installs the described tool's PyPI package into a directory of the store,
# with uv. Its tool environment and the Python that runs it both live in the
# store, so a store restored on another machine (CI's cache) runs.
# Globals:
#   package: the requirement (describe_tool's).
# Arguments:
#   The directory.
install_package() {
  UV_TOOL_DIR="$(native_path "$1/tools")" \
  UV_TOOL_BIN_DIR="$(native_path "$1/bin")" \
  UV_PYTHON_INSTALL_DIR="$(native_path "${STORE}/python")" \
  UV_PYTHON_PREFERENCE=only-managed \
    uv tool install --quiet --python "${PYTHON_VERSION}" "${package}"
}

# Installs the described tool's release download into a directory of the
# store: the asset toolchain_release names, checked against its SHA-256, and
# the binary taken out of it into bin/.
# Globals:
#   command: the binary's name (describe_tool's).
#   release_url, release_sha, release_member: the asset (toolchain_release's).
# Arguments:
#   The directory.
# shellcheck disable=SC2154 # release_*: toolchain_release sets them
install_release() {
  local asset="$1/${release_url##*/}" binary="$1/bin/${command}"
  if windows; then
    binary+=.exe
  fi
  mkdir -p "$1/bin"
  curl --proto '=https' --tlsv1.2 -fsSLo "${asset}" "${release_url}"
  if ! sha256_matches "${release_sha}" "${asset}"; then
    echo "toolchain: SHA-256 mismatch for ${release_url}" >&2
    rm -rf "$1"
    exit 1
  fi
  case "${release_url}" in
    *.tar.gz)
      # From stdin: GNU tar reads a file named C:/... as on a host C.
      tar -xzf - -C "$1" "${release_member}" <"${asset}"
      mv "$1/${release_member}" "${binary}"
      ;;
    *.zip)
      if windows; then
        extract_zip "${asset}" "$1" "${release_member}"
      else
        unzip -qo "${asset}" "${release_member}" -d "$1"
      fi
      mv "$1/${release_member}" "${binary}"
      ;;
    *.gz) gunzip -c "${asset}" >"${binary}" ;;
    *) mv "${asset}" "${binary}" ;;
  esac
  # The directory the binary was in, empty now.
  if [[ "${release_member}" == */* ]]; then
    rm -rf "${1:?}/${release_member%%/*}"
  fi
  chmod +x "${binary}"
  rm -f "${asset}"
}

# Builds the described tool from its LuaRocks rock into a directory of the
# store: luacheck, whose release has no build for macOS or Linux arm64. With
# the system's LuaRocks and its Lua, which the bootstrap installs, and clang
# for the C modules of the rock's dependencies.
# Globals:
#   rock, command, version (describe_tool's).
# Arguments:
#   The directory.
install_rock() {
  if [[ -z "${rock}" ]]; then
    echo "toolchain: ${command} has no release for this platform." >&2
    exit 1
  fi
  if ! command -v luarocks >/dev/null; then
    echo "toolchain: ${command} has no release for this platform, and" \
      "LuaRocks, which builds it, is not on PATH: run" \
      "scripts/bootstrap.sh platform." >&2
    exit 1
  fi
  luarocks --tree "$1" install "${rock}" "${version}" CC=clang LD=clang \
    >/dev/null
}

# Installs each pinned version the store lacks into its <tool>/<version>/,
# which a .installed file, written last, marks complete: an install cut short
# is redone. From the platform's bootstrap script, its release downloads.
fill_store() {
  local tool dir
  # shellcheck source=/dev/null
  . "${SCRIPT_DIR}/${PLATFORM}.sh"
  for tool in "${TOOLS[@]}"; do
    describe_tool "${tool}"
    dir="${STORE}/${tool}/${version}"
    [[ -f "${dir}/.installed" ]] && continue
    echo "toolchain: installing ${tool} ${version}"
    rm -rf "${dir}"
    mkdir -p "${dir}"
    if [[ "${source}" = uv ]]; then
      install_package "${dir}"
    elif toolchain_release "${tool}"; then
      install_release "${dir}"
    else
      install_rock "${dir}"
    fi
    touch "${dir}/.installed"
  done
}

# Installs Prettier into a directory with npm ci, from package.json and its
# lockfile, which it copies there: with the toolchain's Node, and npm's
# download cache in the store, so a reinstall (another worktree, CI's cached
# store) downloads nothing. The directory goes to npm by its physical path:
# npm resolves a path through a symlink (macOS's /var, where mktemp puts its
# directories, is /private/var's) and then takes the directory for a linked
# package the lockfile lacks.
# Arguments:
#   The directory.
npm_ci() {
  local dir
  dir="$(cd "$1" && pwd -P)"
  cp "${PACKAGE}" "${LOCKFILE}" "${dir}/"
  "${STORE}/node/${NODEJS_WHEEL_VERSION}/bin/npm" ci \
    --prefix "$(native_path "${dir}")" \
    --cache "$(native_path "${STORE}/npm-cache")" --prefer-offline \
    --ignore-scripts --no-audit --no-fund --no-update-notifier --loglevel=error
}

# Fills the store alone, outside any checkout, so that a checkout's
# `make toolchain` then only links: npm's download cache takes Prettier's
# packages, which npm ci then installs into each checkout without a download.
store() {
  local scratch
  fill_store
  scratch="$(mktemp -d)"
  npm_ci "${scratch}"
  rm -rf "${scratch}"
}
