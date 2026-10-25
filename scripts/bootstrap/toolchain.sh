#!/bin/bash
# Makes and checks the checkout's toolchain (ADR-0077): every tool at the one
# version scripts/bootstrap/versions.env pins, run from the checkout's
# .toolchain/bin by the editor, the terminal, the hooks and CI.
#
# Usage: scripts/bootstrap/toolchain.sh install|check|store|store-dir
#
# store fills the shared store,
# ${XDG_CACHE_HOME:-~/.cache}/augusta/toolchain/<tool>/<version>/, with each
# pinned version it lacks, once per machine and outside any checkout: what the
# bootstrap's platform step runs, and the dev container's image. store-dir
# prints the store's path, which the bootstrap's uninstall removes.
#
# install fills the store, then links the checkout's .toolchain/bin into it
# (hardlinks or copies on Windows, where a symlink may be refused), with the
# system LLVM's clangd, clang-format and clang-tidy of LLVM_VERSION's major,
# installs Prettier, which package-lock.json pins, into
# .toolchain/node_modules, and writes .toolchain/activate. It is what
# `make toolchain` runs, and quick when nothing changed.
#
# check reads and changes nothing: it fails, naming each culprit, when a tool
# .toolchain/bin links or Prettier is not the pinned version (LLVM's: major) or
# does not run, or when a path .vscode/settings.json names under .toolchain/
# does not exist. It is what `make toolchain-check` and the pre-commit hook
# run.
#
# Beside the bootstrap scripts, not in the Makefile, as the dev container's
# image build copies only scripts/.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
readonly SCRIPT_DIR
ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
readonly ROOT
readonly VERSIONS="${SCRIPT_DIR}/versions.env"
readonly TOOLCHAIN="${ROOT}/.toolchain"
readonly STORE="${XDG_CACHE_HOME:-${HOME}/.cache}/augusta/toolchain"

# shellcheck source=/dev/null
. "${VERSIONS}"
# The LLVM major whose tools .toolchain/bin links.
readonly LLVM_MAJOR="${LLVM_VERSION%%.*}"

# The platform: linux, macos or windows (Git Bash).
case "$(uname -s)" in
  Darwin) PLATFORM=macos ;;
  MINGW* | MSYS* | CYGWIN*) PLATFORM=windows ;;
  *) PLATFORM=linux ;;
esac
readonly PLATFORM

# Prettier, a Node package that package.json and package-lock.json beside this
# script pin, which npm installs into .toolchain/node_modules.
readonly PACKAGE="${SCRIPT_DIR}/package.json"
readonly LOCKFILE="${SCRIPT_DIR}/package-lock.json"
readonly NODE_MODULES="${TOOLCHAIN}/node_modules"
readonly PRETTIER="${NODE_MODULES}/prettier/bin/prettier.cjs"
# The lockfile npm last installed .toolchain/node_modules from, which npm ci
# deletes as it starts.
readonly INSTALLED_LOCKFILE="${NODE_MODULES}/.augusta-package-lock.json"

# Returns:
#   0 on Windows.
windows() {
  [[ "${PLATFORM}" = windows ]]
}

# Prints a path as the native programs take it: uv and npm on Windows read no
# Git Bash path.
# Arguments:
#   The path.
native_path() {
  if windows; then
    cygpath -m "$1"
  else
    echo "$1"
  fi
}

# Git Bash takes a name without .exe for the .exe beside it, in test, ln, cp
# and cygpath alike: only a directory listing tells the two apart.
# Arguments:
#   The file.
# Returns:
#   0 when its directory lists that exact name.
exists_exactly() {
  local file
  for file in "${1%/*}/"*; do
    [[ "${file}" == "$1" ]] && return 0
  done
  return 1
}

# The tools.

# The toolchain's tools, by the names the store files them under.
readonly TOOLS=(ruff shfmt shellcheck actionlint gersemi pymarkdown yamllint
  ctcache node yamlfmt stylua luacheck taplo buf trivy kubeconform kyverno
  promtool amtool hadolint oras syft grype yq flux cosign)

# LLVM's tools, which .toolchain/bin links to the system's LLVM: a package
# manager installs it, pinned to its major (ADR-0008), not the store.
readonly LLVM_TOOLS=(clangd clang-format clang-tidy)

# Describes a tool, without a command substitution, as each forks, slowly on
# Windows.
# Globals:
#   version_name: the versions.env variable that pins it.
#   version: that variable's value.
#   source: where it comes from: uv, which installs package from PyPI, or
#     release, its platform's release download (toolchain_release in
#     scripts/bootstrap/<platform>.sh).
#   package: the PyPI requirement uv installs.
#   rock: the LuaRocks rock that builds it where its release has no build for
#     the platform, or nothing.
#   command: the command it puts in .toolchain/bin.
#   without_exe: set when Windows' .toolchain/bin also needs that command
#     without .exe.
#   probe: the arguments that make it print its version.
#   expected: the start of the line of that output the pinned version prints,
#     up to a space or the line's end, or nothing when its output names no
#     version the pin gives. A wheel that packages another project's binary
#     (shellcheck-py, actionlint-py) adds a fourth number of its own to the
#     binary's version, which the binary does not report.
# Arguments:
#   The tool.
describe_tool() {
  version_args=(--version) source=uv without_exe='' rock=''
  case "$1" in
    ruff)
      version_name=RUFF_VERSION command=ruff package="ruff==${RUFF_VERSION}"
      expected="ruff ${RUFF_VERSION}"
      ;;
    shfmt)
      # shfmt-py's version is the wheel's, not the shfmt it packages.
      version_name=SHFMT_VERSION command=shfmt
      package="shfmt-py==${SHFMT_VERSION}" expected=
      ;;
    shellcheck)
      version_name=SHELLCHECK_VERSION command=shellcheck
      package="shellcheck-py==${SHELLCHECK_VERSION}"
      expected="version: ${SHELLCHECK_VERSION%.*}"
      ;;
    actionlint)
      version_name=ACTIONLINT_VERSION command=actionlint
      package="actionlint-py==${ACTIONLINT_VERSION}"
      expected="${ACTIONLINT_VERSION%.*}"
      ;;
    gersemi)
      version_name=GERSEMI_VERSION command=gersemi
      package="gersemi==${GERSEMI_VERSION}"
      expected="gersemi ${GERSEMI_VERSION}"
      ;;
    pymarkdown)
      version_name=PYMARKDOWN_VERSION command=pymarkdown
      package="pymarkdownlnt==${PYMARKDOWN_VERSION}"
      version_args=(version) expected="${PYMARKDOWN_VERSION}"
      ;;
    yamllint)
      version_name=YAMLLINT_VERSION command=yamllint
      package="yamllint==${YAMLLINT_VERSION}"
      expected="yamllint ${YAMLLINT_VERSION}"
      ;;
    ctcache)
      # A commit, which clang-tidy-cache does not report: --help shows it runs.
      version_name=CTCACHE_COMMIT command=clang-tidy-cache
      package="ctcache @ git+https://github.com/matus-chochlik/ctcache@${CTCACHE_COMMIT}"
      version_args=(--help) expected=
      ;;
    node)
      # Node from its PyPI wheel, which runs Prettier: uv's launcher of it.
      version_name=NODEJS_WHEEL_VERSION command=node
      package="nodejs-wheel==${NODEJS_WHEEL_VERSION}"
      expected="v${NODEJS_WHEEL_VERSION}"
      ;;
    yamlfmt)
      version_name=YAMLFMT_VERSION command=yamlfmt source=release
      expected="yamlfmt ${YAMLFMT_VERSION}"
      ;;
    stylua)
      version_name=STYLUA_VERSION command=stylua source=release
      expected="stylua ${STYLUA_VERSION}"
      ;;
    luacheck)
      # No release for macOS or Linux arm64, where its rock builds it.
      version_name=LUACHECK_VERSION command=luacheck source=release
      rock=luacheck
      expected="Luacheck: ${LUACHECK_VERSION}"
      ;;
    taplo)
      version_name=TAPLO_VERSION command=taplo source=release
      expected="taplo ${TAPLO_VERSION}"
      ;;
    buf)
      # buf's extension runs the exact path its setting names, which has no
      # .exe on Windows.
      version_name=BUF_VERSION command=buf source=release without_exe=1
      expected="${BUF_VERSION}"
      ;;
    # The tools CI's own jobs run, and the operators' flux and cosign.
    trivy)
      version_name=TRIVY_VERSION command=trivy source=release
      expected="Version: ${TRIVY_VERSION}"
      ;;
    kubeconform)
      version_name=KUBECONFORM_VERSION command=kubeconform source=release
      version_args=(-v) expected="v${KUBECONFORM_VERSION}"
      ;;
    kyverno)
      version_name=KYVERNO_VERSION command=kyverno source=release
      version_args=(version) expected="Version: ${KYVERNO_VERSION}"
      ;;
    promtool)
      version_name=PROMETHEUS_VERSION command=promtool source=release
      expected="promtool, version ${PROMETHEUS_VERSION}"
      ;;
    amtool)
      version_name=ALERTMANAGER_VERSION command=amtool source=release
      expected="amtool, version ${ALERTMANAGER_VERSION}"
      ;;
    hadolint)
      version_name=HADOLINT_VERSION command=hadolint source=release
      expected="Haskell Dockerfile Linter ${HADOLINT_VERSION}"
      ;;
    oras)
      version_name=ORAS_VERSION command=oras source=release
      version_args=(version) expected="Version:        ${ORAS_VERSION}"
      ;;
    syft)
      version_name=SYFT_VERSION command=syft source=release
      expected="syft ${SYFT_VERSION}"
      ;;
    grype)
      version_name=GRYPE_VERSION command=grype source=release
      expected="grype ${GRYPE_VERSION}"
      ;;
    yq)
      version_name=YQ_VERSION command=yq source=release
      expected="yq (https://github.com/mikefarah/yq/) version v${YQ_VERSION}"
      ;;
    flux)
      version_name=FLUX_VERSION command=flux source=release
      expected="flux version ${FLUX_VERSION}"
      ;;
    cosign)
      version_name=COSIGN_VERSION command=cosign source=release
      version_args=(version) expected="GitVersion:    v${COSIGN_VERSION}"
      ;;
    *)
      echo "toolchain: unknown tool $1" >&2
      exit 1
      ;;
  esac
  version="${!version_name}"
}

# Sets the global major to the LLVM major a tool's --version output names
# ("clang-format version 21.1.8", "LLVM version 21.1.8", "Apple clangd version
# 17.0.0"), or to nothing.
# Arguments:
#   The output.
read_major() {
  local pattern='version ([0-9]+)\.'
  major=
  if [[ "$1" =~ ${pattern} ]]; then
    major="${BASH_REMATCH[1]}"
  fi
}

# The store.

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
# store) downloads nothing.
# Arguments:
#   The directory.
npm_ci() {
  cp "${PACKAGE}" "${LOCKFILE}" "$1/"
  "${STORE}/node/${NODEJS_WHEEL_VERSION}/bin/npm" ci \
    --prefix "$(native_path "$1")" \
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

# The checkout's links.

# Removes a file of .toolchain/bin, if there is one. A Windows executable that
# is running (the editor's ruff server) cannot be deleted, but it can be
# renamed out of the way, which install removes once nothing runs it.
# Arguments:
#   The file.
remove_file() {
  if [[ -e "$1" || -L "$1" ]]; then
    rm -f "$1" 2>/dev/null || mv -f "$1" "$1.$$.old"
  fi
}

# Links a file of the store into .toolchain/bin, unless it is linked already.
# Arguments:
#   The store's file, the link.
link_file() {
  [[ -e "$2" && "$2" -ef "$1" ]] && return
  remove_file "$2"
  if windows; then
    ln -f "$1" "$2" 2>/dev/null || cp -f "$1" "$2"
  else
    ln -sf "$1" "$2"
  fi
}

# Gives a Windows executable of .toolchain/bin a second name without .exe,
# unless it has it already: buf's extension checks that the exact path its
# setting names exists before it runs it, and takes one name for every
# platform. A hardlink, or a copy on another volume than the store, made by
# Windows' own commands, as Git Bash's would write the .exe again.
# Arguments:
#   The command.
link_without_exe() {
  local file="${TOOLCHAIN}/bin/$1" dir
  dir="$(cygpath -w "${TOOLCHAIN}/bin")"
  if exists_exactly "${file}"; then
    [[ "${file}" -ef "${file}.exe" ]] && return
    remove_file "${file}"
  fi
  cmd //c mklink //H "${dir}\\$1" "${dir}\\$1.exe" >/dev/null 2>&1 \
    || cmd //c copy //y "${dir}\\$1.exe" "${dir}\\$1" >/dev/null
}

# Prints the system's binary of an LLVM tool at the pinned major, or nothing:
# the platform's LLVM of that major (apt's, Homebrew's keg), then PATH's (where
# CI's Windows jobs put theirs, ahead of the runner image's), then the Windows
# installer's. Never .toolchain/bin's own, which PATH may hold.
# Arguments:
#   The tool.
find_llvm_tool() {
  local candidate output program_files candidates=(
    "/usr/lib/llvm-${LLVM_MAJOR}/bin/$1"
    "${HOMEBREW_PREFIX:-/opt/homebrew}/opt/llvm@${LLVM_MAJOR}/bin/$1"
    "$(command -v "$1-${LLVM_MAJOR}" || true)"
    "$(command -v "$1" || true)"
  )
  if windows; then
    program_files="$(cygpath -u "${PROGRAMFILES:-C:\\Program Files}")"
    candidates+=("${program_files}/LLVM/bin/$1")
  fi
  for candidate in "${candidates[@]}"; do
    [[ -n "${candidate}" ]] || continue
    if [[ -f "${candidate}.exe" ]]; then
      candidate+=.exe
    fi
    [[ -f "${candidate}" && -x "${candidate}" ]] || continue
    [[ "${candidate%/*}" -ef "${TOOLCHAIN}/bin" ]] && continue
    output="$("${candidate}" --version 2>/dev/null)" || continue
    read_major "${output}"
    if [[ "${major}" == "${LLVM_MAJOR}" ]]; then
      echo "${candidate}"
      return
    fi
  done
}

# Arguments:
#   A binary, a file.
# Returns:
#   0 when the file is the binary, or a copy of it with its time.
same_binary() {
  [[ -e "$2" ]] || return 1
  [[ "$2" -ef "$1" ]] || [[ ! "$2" -nt "$1" && ! "$2" -ot "$1" ]]
}

# Links an LLVM tool into .toolchain/bin, unless it is linked already.
# clangd and clang-tidy take clang's own headers from beside the binary they
# run as (../lib/clang/<major>/include), which a symlink resolves to the
# system's LLVM. So Windows takes a native symlink where it may (Developer
# Mode, an administrator), else a hardlink or a copy, kept while its time is
# the system binary's, with those headers copied beside .toolchain/bin.
# Arguments:
#   The system's binary, the link.
link_llvm_tool() {
  local headers="${TOOLCHAIN}/lib/clang/${LLVM_MAJOR}/include"
  if ! windows; then
    link_file "$1" "$2"
    return
  fi
  if ! same_binary "$1" "$2"; then
    remove_file "$2"
    MSYS=winsymlinks:nativestrict ln -s "$1" "$2" 2>/dev/null \
      || ln "$1" "$2" 2>/dev/null || cp -p "$1" "$2"
  fi
  if [[ ! -L "$2" && ! -d "${headers}" ]]; then
    mkdir -p "${headers%/*}"
    cp -R "${1%/*/*}/lib/clang/${LLVM_MAJOR}/include" "${headers}"
  fi
}

# Installs Prettier into .toolchain/node_modules with npm ci, unless it is
# installed from this lockfile already.
install_prettier() {
  cmp -s "${LOCKFILE}" "${INSTALLED_LOCKFILE}" && return
  echo "toolchain: installing Prettier from $(native_path "${LOCKFILE}")"
  npm_ci "${TOOLCHAIN}"
  # Last, so an install cut short is redone.
  cp "${LOCKFILE}" "${INSTALLED_LOCKFILE}"
}

# Writes .toolchain/activate, which puts .toolchain/bin first on a shell's
# PATH.
write_activate() {
  cat >"${TOOLCHAIN}/activate" <<EOF
# Puts this checkout's toolchain first on PATH (ADR-0077): source it.
case ":\${PATH}:" in
  *":${TOOLCHAIN}/bin:"*) ;;
  *) export PATH="${TOOLCHAIN}/bin:\${PATH}" ;;
esac
EOF
}

# Fills the store and links .toolchain/bin into it, and LLVM's tools to the
# system's, removing whatever else it holds; installs Prettier and writes
# .toolchain/activate. Last, it copies the pins it linked to
# .toolchain/versions.env, which check compares with the pins, so an install
# cut short leaves the old ones.
install() {
  local tool file name kept=" "
  mkdir -p "${TOOLCHAIN}/bin"
  fill_store
  for tool in "${TOOLS[@]}"; do
    describe_tool "${tool}"
    for file in "${STORE}/${tool}/${version}/bin/${command}"{,.exe}; do
      [[ -e "${file}" ]] || continue
      name="${file##*/}"
      link_file "${file}" "${TOOLCHAIN}/bin/${name}"
      kept+="${name} "
    done
    if [[ -n "${without_exe}" ]] && windows; then
      link_without_exe "${command}"
      kept+="${command} "
    fi
  done
  # LLVM's tools when the system has them at the pinned major, else said and
  # left out, which check then fails on.
  for tool in "${LLVM_TOOLS[@]}"; do
    file="$(find_llvm_tool "${tool}")"
    if [[ -z "${file}" ]]; then
      echo "toolchain: no LLVM ${LLVM_MAJOR} ${tool} found: install it" \
        "(scripts/bootstrap.sh)" >&2
      continue
    fi
    name="${file##*/}"
    link_llvm_tool "${file}" "${TOOLCHAIN}/bin/${name}"
    kept+="${name} "
  done
  # What a previous version linked and this one does not, or renamed away
  # while it ran: removed when nothing runs it any more.
  for file in "${TOOLCHAIN}/bin/"*; do
    name="${file##*/}"
    if [[ "${kept}" != *" ${name} "* ]]; then
      rm -f "${file}" 2>/dev/null || true
    fi
  done
  install_prettier
  write_activate
  cp "${VERSIONS}" "${TOOLCHAIN}/versions.env"
}

# The check.

# Reads a whole file into the global contents, carriage returns dropped,
# without forking: nothing when there is no file.
# Arguments:
#   The file.
read_file() {
  contents=
  [[ -f "$1" ]] || return 0
  IFS= read -r -d '' contents <"$1" || true
  contents="${contents//$'\r'/}"
}

# Runs a command in the background, its output and then its exit status into
# files of the probes directory named for it, so that every tool prints its
# version at once.
# Globals:
#   probes: the directory.
# Arguments:
#   The probe's name, then the command.
start_probe() {
  local name="$1"
  shift
  {
    local status=0
    "$@" >"${probes}/${name}" 2>&1 || status=$?
    echo "${status}" >"${probes}/${name}.status"
  } &
}

# Reads a probe's results into the globals status, its exit status, and
# contents, its output.
# Globals:
#   probes: the directory.
# Arguments:
#   The probe's name.
read_probe() {
  read_file "${probes}/$1.status"
  status="${contents%$'\n'}"
  read_file "${probes}/$1"
}

# Records a failure.
# Globals:
#   failures: the failures, each a line check prints.
# Arguments:
#   The failure's words.
fail() {
  failures+=("$*")
}

# Checks each tool .toolchain/bin links: that install linked its pinned
# version, and that the tool runs and prints it. Each a line between newlines,
# matched whole, without forking: a fork per tool would cost the hook seconds
# on Windows.
check_tools() {
  local tool installed output
  read_file "${TOOLCHAIN}/versions.env"
  installed=$'\n'"${contents}"$'\n'
  for tool in "${TOOLS[@]}"; do
    describe_tool "${tool}"
    read_probe "${tool}"
    output=$'\n'"${contents}"$'\n'
    if [[ "${installed}" != *$'\n'"${version_name}=${version}"$'\n'* ]]; then
      fail "${tool}: .toolchain/bin links another version than" \
        "${version_name}=${version}"
    elif [[ "${status}" != 0 ]]; then
      fail "${tool}: .toolchain/bin/${command} does not run"
    elif [[ -n "${expected}" &&
      "${output}" != *$'\n'"${expected}"[$'\n'\ ]* ]]; then
      fail "${tool}: .toolchain/bin/${command} is not ${version}, it prints:" \
        "$(head -n 1 <<<"${contents}")"
    elif [[ -n "${without_exe}" ]] && windows \
      && ! exists_exactly "${TOOLCHAIN}/bin/${command}"; then
      fail "${tool}: .toolchain/bin/${command} is missing, the name without" \
        ".exe its extension runs"
    fi
  done
}

# Checks LLVM's tools: of the major it is pinned to, as linked from the
# system's now.
check_llvm_tools() {
  local tool
  for tool in "${LLVM_TOOLS[@]}"; do
    read_probe "${tool}"
    read_major "${contents}"
    if [[ ! -e "${TOOLCHAIN}/bin/${tool}" ]]; then
      fail "${tool}: .toolchain/bin/${tool} does not exist: install LLVM" \
        "${LLVM_MAJOR}'s, then run make toolchain"
    elif [[ "${status}" != 0 ]]; then
      fail "${tool}: .toolchain/bin/${tool} does not run"
    elif [[ "${major}" != "${LLVM_MAJOR}" ]]; then
      fail "${tool}: .toolchain/bin/${tool}" \
        "($(readlink -f "${TOOLCHAIN}/bin/${tool}")) is not LLVM" \
        "${LLVM_MAJOR}, it prints: $(head -n 1 <<<"${contents}")"
    fi
  done
}

# Checks Prettier, at the version the lockfile pins: its
# "node_modules/prettier" entry's first "version", matched without forking.
check_prettier() {
  local lockfile pin
  read_file "${LOCKFILE}"
  lockfile="${contents}"
  pin="${contents#*\"node_modules/prettier\": \{}"
  pin="${pin#*\"version\": \"}"
  pin="${pin%%\"*}"
  read_file "${INSTALLED_LOCKFILE}"
  if [[ "${contents}" != "${lockfile}" ]]; then
    fail "prettier: .toolchain/node_modules is not installed from" \
      "package-lock.json's prettier ${pin}"
    return
  fi
  read_probe prettier
  if [[ "${status}" != 0 ]]; then
    fail "prettier: ${PRETTIER#"${ROOT}/"} does not run"
  elif [[ $'\n'"${contents}" != *$'\n'"${pin}"$'\n'* ]]; then
    fail "prettier: ${PRETTIER#"${ROOT}/"} is not ${pin}, it prints:" \
      "$(head -n 1 <<<"${contents}")"
  fi
}

# Checks the editor's settings. Its binaries: an extension whose path names
# nothing falls back to its bundled copy without a word. A binary passes with
# or without .exe, as Windows' bin has only the one and the others' only the
# other. And StyLua's release: its extension can't run .toolchain/bin's, as it
# expands no variable, and looks for a relative path from the file system's
# root. It downloads the release the settings name instead, which must be the
# pinned one.
check_editor() {
  local settings="${ROOT}/.vscode/settings.json" path stylua
  while IFS= read -r path; do
    path="${path#\$\{workspaceFolder\}/}"
    path="${path#\"}"
    if [[ ! -e "${ROOT}/${path}" && ! -e "${ROOT}/${path%.exe}" &&
      ! -e "${ROOT}/${path}.exe" ]]; then
      fail ".vscode/settings.json names ${path}, which does not exist"
    fi
  done < <(grep -oE '(\$\{workspaceFolder\}/|")\.toolchain/[^"$:;]*' \
    "${settings}" | sort -u)
  read_file "${settings}"
  stylua="\"stylua.targetReleaseVersion\": \"v${STYLUA_VERSION}\""
  if [[ "${contents}" != *"${stylua}"* ]]; then
    fail ".vscode/settings.json's stylua.targetReleaseVersion is not" \
      "v${STYLUA_VERSION}"
  fi
}

# Reads .toolchain/ and changes nothing, failing with each culprit when it is
# not what install makes of the pins.
check() {
  local tool
  failures=()
  if [[ ! -d "${TOOLCHAIN}/bin" ]]; then
    echo "toolchain: ${TOOLCHAIN}/bin does not exist." >&2
    return 1
  fi
  # Every tool prints its version at once, as the pre-commit hook waits on
  # them.
  probes="$(mktemp -d)"
  # shellcheck disable=SC2064 # the directory now, not at exit
  trap "rm -rf '${probes}'" EXIT
  for tool in "${TOOLS[@]}"; do
    describe_tool "${tool}"
    start_probe "${tool}" "${TOOLCHAIN}/bin/${command}" "${version_args[@]}"
  done
  for tool in "${LLVM_TOOLS[@]}"; do
    start_probe "${tool}" "${TOOLCHAIN}/bin/${tool}" --version
  done
  start_probe prettier "${TOOLCHAIN}/bin/node" "${PRETTIER}" --version
  wait
  check_tools
  check_llvm_tools
  check_prettier
  check_editor
  if ((${#failures[@]} > 0)); then
    printf 'toolchain: %s\n' "${failures[@]}" >&2
    return 1
  fi
}

main() {
  case "${1:-}" in
    install) install ;;
    check) check ;;
    store) store ;;
    store-dir) echo "${STORE}" ;;
    *)
      echo "usage: $0 install|check|store|store-dir" >&2
      exit 2
      ;;
  esac
}

main "$@"
