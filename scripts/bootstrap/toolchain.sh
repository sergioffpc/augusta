#!/bin/bash
# Makes and checks the checkout's toolchain (ADR-0077): every tool at the one
# version scripts/bootstrap/versions.env pins, run from the checkout's
# .toolchain/bin by the editor, the terminal, the hooks and CI.
#
# Usage: scripts/bootstrap/toolchain.sh install|check
#
# install puts each pinned version that is missing into the shared store,
# ${XDG_CACHE_HOME:-~/.cache}/augusta/toolchain/<tool>/<version>/, once per
# machine, then links .toolchain/bin into it (hardlinks or copies on Windows,
# where a symlink may be refused) and writes .toolchain/activate. Prettier, which
# package-lock.json pins, goes into .toolchain/node_modules with npm ci, the
# toolchain's Node running it. It links the system LLVM's clangd, clang-format
# and clang-tidy there too, of the major .github/actions/setup-windows-llvm
# pins. It is what `make toolchain` runs, and quick when nothing changed.
#
# check reads and changes nothing: it fails, naming each culprit, when a tool
# .toolchain/bin links or Prettier is not the pinned version (LLVM's: major) or
# does not run, or when a path .vscode/settings.json names under .toolchain/
# does not exist. It is what `make toolchain-check` runs.
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

# The toolchain's tools, by the names the store files them under.
readonly TOOLS=(ruff shfmt shellcheck actionlint gersemi pymarkdown yamllint
  ctcache node yamlfmt stylua luacheck taplo buf trivy kubeconform kyverno
  promtool amtool hadolint oras syft grype yq flux cosign)
# Prettier, a Node package that package.json and package-lock.json beside this
# script pin, which npm installs into .toolchain/node_modules.
readonly PACKAGE="${SCRIPT_DIR}/package.json"
readonly LOCKFILE="${SCRIPT_DIR}/package-lock.json"
readonly PRETTIER="${TOOLCHAIN}/node_modules/prettier/bin/prettier.cjs"
# The lockfile npm last installed .toolchain/node_modules from, which npm ci
# deletes as it starts.
readonly INSTALLED_LOCKFILE="${TOOLCHAIN}/node_modules/.augusta-package-lock.json"

# LLVM's tools, which .toolchain/bin links to the system's LLVM: a package
# manager installs it, pinned to its major (ADR-0008), not the store.
readonly LLVM_TOOLS=(clangd clang-format clang-tidy)

# Describes a tool, in globals: version_name, the versions.env variable that
# pins it, and version, its value; source, where it comes from: uv, which
# installs package, or release, its platform's release download
# (toolchain_release in scripts/bootstrap/<platform>.sh); command, the command
# it puts in .toolchain/bin; bare, set when Windows' bin also needs that command
# without .exe; probe, the arguments that make it print its version; expected,
# the start of the line of that output the pinned version prints, up to a space
# or the line's end, or nothing when its output names no version the pin gives.
# A wheel that packages another project's binary (shellcheck-py, actionlint-py)
# adds a fourth number of its own to the binary's version, which the binary does
# not report.
# Without a command substitution, as each forks, slowly on Windows.
# Arguments:
#   The tool.
describe_tool() {
  probe=(--version) source=uv bare=
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
      probe=(version) expected="${PYMARKDOWN_VERSION}"
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
      probe=(--help) expected=
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
      version_name=LUACHECK_VERSION command=luacheck source=release
      expected="Luacheck: ${LUACHECK_VERSION}"
      ;;
    taplo)
      version_name=TAPLO_VERSION command=taplo source=release
      expected="taplo ${TAPLO_VERSION}"
      ;;
    buf)
      # buf's extension runs the exact path its setting names, which has no
      # .exe on Windows.
      version_name=BUF_VERSION command=buf source=release bare=1
      expected="${BUF_VERSION}"
      ;;
    # The tools CI's own jobs run, and the operators' flux and cosign.
    trivy)
      version_name=TRIVY_VERSION command=trivy source=release
      expected="Version: ${TRIVY_VERSION}"
      ;;
    kubeconform)
      version_name=KUBECONFORM_VERSION command=kubeconform source=release
      probe=(-v) expected="v${KUBECONFORM_VERSION}"
      ;;
    kyverno)
      version_name=KYVERNO_VERSION command=kyverno source=release
      probe=(version) expected="Version: ${KYVERNO_VERSION}"
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
      probe=(version) expected="Version:        ${ORAS_VERSION}"
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
      probe=(version) expected="GitVersion:    v${COSIGN_VERSION}"
      ;;
    *)
      echo "toolchain: unknown tool $1" >&2
      exit 1
      ;;
  esac
  version="${!version_name}"
}

windows() {
  case "$(uname -s)" in
    MINGW* | MSYS* | CYGWIN*) return 0 ;;
    *) return 1 ;;
  esac
}

# Prints a path as the native programs take it: uv on Windows reads no Git
# Bash path.
native_path() {
  if windows; then
    cygpath -m "$1"
  else
    echo "$1"
  fi
}

# Installs the described tool's pinned version into the store, unless it is
# there already. Its tool environment and the Python that runs it both live
# in the store, so a store restored on another machine (CI's cache) runs.
install_tool() {
  local dir="${STORE}/$1/${version}"
  [[ -f "${dir}/.installed" ]] && return
  echo "toolchain: installing $1 ${version}"
  rm -rf "${dir}"
  mkdir -p "${dir}"
  UV_TOOL_DIR="$(native_path "${dir}/tools")" \
  UV_TOOL_BIN_DIR="$(native_path "${dir}/bin")" \
  UV_PYTHON_INSTALL_DIR="$(native_path "${STORE}/python")" \
  UV_PYTHON_PREFERENCE=only-managed \
    uv tool install --quiet "${package}"
  # Last, so an install cut short is redone.
  touch "${dir}/.installed"
}

# Links a file of the store into .toolchain/bin, unless it is linked already.
# A Windows executable that is running (the editor's ruff server) cannot be
# deleted, but it can be renamed out of the way.
# Arguments:
#   The store's file, the link.
link_file() {
  [[ -e "$2" && "$2" -ef "$1" ]] && return
  if [[ -e "$2" || -L "$2" ]]; then
    rm -f "$2" 2>/dev/null || mv -f "$2" "$2.$$.old"
  fi
  if windows; then
    ln -f "$1" "$2" 2>/dev/null || cp -f "$1" "$2"
  else
    ln -sf "$1" "$2"
  fi
}

# Git Bash takes a name without .exe for the .exe beside it, in test, ln, cp
# and cygpath alike: only a directory listing tells the two apart.
# Arguments:
#   The file.
# Returns:
#   0 when its directory lists that exact name.
listed() {
  local file
  for file in "${1%/*}/"*; do
    [[ "${file}" == "$1" ]] && return 0
  done
  return 1
}

# Gives a Windows executable of .toolchain/bin a second name without .exe,
# unless it has it already: buf's extension checks that the exact path its
# setting names exists before it runs it, and takes one name for every
# platform. A hardlink, or a copy on another volume than the store, made by
# Windows' own commands, as Git Bash's would write the .exe again.
# Arguments:
#   The command.
link_bare() {
  local bare="${TOOLCHAIN}/bin/$1" dir
  dir="$(cygpath -w "${TOOLCHAIN}/bin")"
  if listed "${bare}"; then
    [[ "${bare}" -ef "${bare}.exe" ]] && return
    rm -f "${bare}" 2>/dev/null || mv -f "${bare}" "${bare}.$$.old"
  fi
  cmd //c mklink //H "${dir}\\$1" "${dir}\\$1.exe" >/dev/null 2>&1 \
    || cmd //c copy //y "${dir}\\$1.exe" "${dir}\\$1" >/dev/null
}

# Checks a file's SHA-256: GNU's sha256sum, or on macOS, whose own reads no
# list from stdin, shasum.
# Arguments:
#   The SHA-256, the file.
sha256_matches() {
  if [[ "$(uname -s)" = Darwin ]]; then
    printf '%s  %s\n' "$1" "$2" | shasum -a 256 --check --status
  else
    printf '%s  %s\n' "$1" "$2" | sha256sum --check --status
  fi
}

# Installs the described tool's pinned release into the store, unless it is
# there already: the asset toolchain_release names, checked against its
# SHA-256, and the binary taken out of it.
# Arguments:
#   The tool.
# shellcheck disable=SC2154 # release_*: toolchain_release sets them
install_release() {
  local dir="${STORE}/$1/${version}" asset binary exe=
  [[ -f "${dir}/.installed" ]] && return
  echo "toolchain: installing $1 ${version}"
  windows && exe=.exe
  rm -rf "${dir}"
  mkdir -p "${dir}/bin"
  asset="${dir}/${release_url##*/}"
  binary="${dir}/bin/${command}${exe}"
  curl --proto '=https' --tlsv1.2 -fsSLo "${asset}" "${release_url}"
  if ! sha256_matches "${release_sha}" "${asset}"; then
    echo "toolchain: SHA-256 mismatch for ${release_url}" >&2
    rm -rf "${dir}"
    exit 1
  fi
  case "${release_url}" in
    *.tar.gz)
      tar -xzf "${asset}" -C "${dir}" "${release_member}"
      mv "${dir}/${release_member}" "${binary}"
      ;;
    *.zip)
      if windows; then
        extract_zip "${asset}" "${dir}"
      else
        unzip -qo "${asset}" "${release_member}" -d "${dir}"
      fi
      mv "${dir}/${release_member}" "${binary}"
      ;;
    *.gz) gunzip -c "${asset}" >"${binary}" ;;
    *) mv "${asset}" "${binary}" ;;
  esac
  chmod +x "${binary}"
  rm -f "${asset}"
  # Last, so an install cut short is redone.
  touch "${dir}/.installed"
}

# Links the system's command of a tool that has no release for this platform
# (luacheck on macOS and Linux arm64, which Homebrew and apt install), the
# first on PATH outside any .toolchain/bin: check then holds it to the pin.
# Arguments:
#   The tool.
link_system() {
  local dir dirs
  IFS=: read -r -a dirs <<<"${PATH}"
  for dir in "${dirs[@]}"; do
    [[ "${dir}" == */.toolchain/bin || ! -x "${dir}/${command}" ]] && continue
    link_file "${dir}/${command}" "${TOOLCHAIN}/bin/${command}"
    return
  done
  echo "toolchain: $1 has no release for this platform, and no ${command}" \
    "is on PATH to link: run scripts/bootstrap.sh, which installs it." >&2
  exit 1
}

# Sets the global llvm_major to the LLVM major the repository pins: that of the
# exact version CI's Windows jobs install, which the bootstrap's packages and
# the Linux runner's give too. Read there, not written again, without forking.
read_llvm_major() {
  local action=.github/actions/setup-windows-llvm/action.yml
  local pattern='LLVM_VERSION: *([0-9]+)\.'
  read_file "${ROOT}/${action}"
  if [[ ! "${contents}" =~ ${pattern} ]]; then
    echo "toolchain: ${action} names no LLVM_VERSION" >&2
    exit 1
  fi
  llvm_major="${BASH_REMATCH[1]}"
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

# Prints the system's binary of an LLVM tool at the pinned major, or nothing:
# the platform's LLVM of that major (apt's, Homebrew's keg), then PATH's (where
# CI's Windows jobs put theirs, ahead of the runner image's), then the Windows
# installer's. Never .toolchain/bin's own, which PATH may hold.
# Arguments:
#   The tool.
find_llvm_tool() {
  local candidate output candidates=(
    "/usr/lib/llvm-${llvm_major}/bin/$1"
    "${HOMEBREW_PREFIX:-/opt/homebrew}/opt/llvm@${llvm_major}/bin/$1"
    "$(command -v "$1-${llvm_major}" || true)"
    "$(command -v "$1" || true)"
  )
  if windows; then
    candidates+=("$(cygpath -u "${PROGRAMFILES:-C:\\Program Files}")/LLVM/bin/$1")
  fi
  for candidate in "${candidates[@]}"; do
    if [[ -f "${candidate}.exe" ]]; then
      candidate+=.exe
    fi
    [[ -n "${candidate}" && -f "${candidate}" && -x "${candidate}" ]] || continue
    if [[ "${candidate%/*}" -ef "${TOOLCHAIN}/bin" ]]; then
      continue
    fi
    output="$("${candidate}" --version 2>/dev/null)" || continue
    read_major "${output}"
    if [[ "${major}" == "${llvm_major}" ]]; then
      echo "${candidate}"
      return
    fi
  done
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
  if ! windows; then
    link_file "$1" "$2"
    return
  fi
  if [[ ! -e "$2" || ! ("$2" -ef "$1" || (! "$2" -nt "$1" && ! "$2" -ot "$1")) ]]; then
    if [[ -e "$2" || -L "$2" ]]; then
      rm -f "$2" 2>/dev/null || mv -f "$2" "$2.$$.old"
    fi
    MSYS=winsymlinks:nativestrict ln -s "$1" "$2" 2>/dev/null \
      || ln "$1" "$2" 2>/dev/null || cp -p "$1" "$2"
  fi
  local headers="${TOOLCHAIN}/lib/clang/${llvm_major}/include"
  if [[ ! -L "$2" && ! -d "${headers}" ]]; then
    mkdir -p "$(dirname "${headers}")"
    cp -R "$(dirname "$(dirname "$1")")/lib/clang/${llvm_major}/include" \
      "${headers}"
  fi
}

# Installs Prettier into .toolchain/node_modules with npm ci, from the lockfile,
# unless it is installed from this lockfile already. npm's download cache is in
# the store, so a reinstall (another worktree, CI's cached store) downloads
# nothing.
install_prettier() {
  local npm="${STORE}/node/${NODEJS_WHEEL_VERSION}/bin/npm"
  cmp -s "${LOCKFILE}" "${INSTALLED_LOCKFILE}" && return
  echo "toolchain: installing Prettier from $(native_path "${LOCKFILE}")"
  cp "${PACKAGE}" "${LOCKFILE}" "${TOOLCHAIN}/"
  "${npm}" ci --prefix "$(native_path "${TOOLCHAIN}")" \
    --cache "$(native_path "${STORE}/npm-cache")" --prefer-offline \
    --ignore-scripts --no-audit --no-fund --no-update-notifier --loglevel=error
  # Last, so an install cut short is redone.
  cp "${LOCKFILE}" "${INSTALLED_LOCKFILE}"
}

install() {
  local tool file name linked=() platform=linux
  mkdir -p "${TOOLCHAIN}/bin"
  # The platform's release downloads, toolchain_release.
  case "$(uname -s)" in
    Darwin) platform=macos ;;
    MINGW* | MSYS* | CYGWIN*) platform=windows ;;
  esac
  # shellcheck source=/dev/null
  . "${SCRIPT_DIR}/${platform}.sh"
  for tool in "${TOOLS[@]}"; do
    describe_tool "${tool}"
    if [[ "${source}" = release ]]; then
      if ! toolchain_release "${tool}"; then
        link_system "${tool}"
        linked+=("${command}")
        continue
      fi
      install_release "${tool}"
    else
      install_tool "${tool}"
    fi
    for file in "${STORE}/${tool}/${version}/bin/${command}"{,.exe}; do
      [[ -e "${file}" ]] || continue
      name="$(basename "${file}")"
      link_file "${file}" "${TOOLCHAIN}/bin/${name}"
      linked+=("${name}")
    done
    if [[ -n "${bare}" && "${platform}" = windows ]]; then
      link_bare "${command}"
      linked+=("${command}")
    fi
  done
  # LLVM's tools when the system has them at the pinned major, else said and
  # left out, which check then fails on.
  read_llvm_major
  for tool in "${LLVM_TOOLS[@]}"; do
    file="$(find_llvm_tool "${tool}")"
    if [[ -z "${file}" ]]; then
      echo "toolchain: no LLVM ${llvm_major} ${tool} found: install it (scripts/bootstrap.sh)" >&2
      continue
    fi
    name="$(basename "${file}")"
    link_llvm_tool "${file}" "${TOOLCHAIN}/bin/${name}"
    linked+=("${name}")
  done
  # What a previous version linked and this one does not, or renamed away
  # while it ran: removed when nothing runs it any more.
  for file in "${TOOLCHAIN}/bin/"*; do
    name="$(basename "${file}")"
    if [[ " ${linked[*]} " != *" ${name} "* ]]; then
      rm -f "${file}" 2>/dev/null || true
    fi
  done
  install_prettier
  cat >"${TOOLCHAIN}/activate" <<EOF
# Puts this checkout's toolchain first on PATH (ADR-0077): source it.
case ":\${PATH}:" in
  *":${TOOLCHAIN}/bin:"*) ;;
  *) export PATH="${TOOLCHAIN}/bin:\${PATH}" ;;
esac
EOF
  # The versions .toolchain/bin now links, which check compares with the pins.
  # Last, so an install cut short leaves the old ones.
  cp "${VERSIONS}" "${TOOLCHAIN}/versions.env"
}

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

check() {
  local failures=() tool linked output path probes status lockfile pin
  if [[ ! -d "${TOOLCHAIN}/bin" ]]; then
    echo "toolchain: ${TOOLCHAIN}/bin does not exist." >&2
    return 1
  fi
  # Every tool prints its version at once, as the pre-commit hook waits on
  # them: each into a file of its own, with its exit status beside it.
  probes="$(mktemp -d)"
  # shellcheck disable=SC2064 # the directory now, not at exit
  trap "rm -rf '${probes}'" EXIT
  for tool in "${TOOLS[@]}"; do
    describe_tool "${tool}"
    {
      status=0
      "${TOOLCHAIN}/bin/${command}" "${probe[@]}" >"${probes}/${tool}" 2>&1 \
        || status=$?
      echo "${status}" >"${probes}/${tool}.status"
    } &
  done
  for tool in "${LLVM_TOOLS[@]}"; do
    {
      status=0
      "${TOOLCHAIN}/bin/${tool}" --version >"${probes}/${tool}" 2>&1 || status=$?
      echo "${status}" >"${probes}/${tool}.status"
    } &
  done
  {
    status=0
    "${TOOLCHAIN}/bin/node" "${PRETTIER}" --version >"${probes}/prettier" 2>&1 \
      || status=$?
    echo "${status}" >"${probes}/prettier.status"
  } &
  wait
  # Each a line between newlines, matched whole, without forking: a fork per
  # tool would cost the hook seconds on Windows.
  read_file "${TOOLCHAIN}/versions.env"
  linked=$'\n'"${contents}"$'\n'
  for tool in "${TOOLS[@]}"; do
    describe_tool "${tool}"
    read_file "${probes}/${tool}.status"
    status="${contents%$'\n'}"
    read_file "${probes}/${tool}"
    output=$'\n'"${contents}"$'\n'
    if [[ "${linked}" != *$'\n'"${version_name}=${version}"$'\n'* ]]; then
      failures+=("${tool}: .toolchain/bin links another version than ${version_name}=${version}")
    elif [[ "${status}" != 0 ]]; then
      failures+=("${tool}: .toolchain/bin/${command} does not run")
    elif [[ -n "${expected}" && "${output}" != *$'\n'"${expected}"[$'\n'\ ]* ]]; then
      failures+=("${tool}: .toolchain/bin/${command} is not ${version}, it prints: $(head -n 1 <<<"${contents}")")
    elif [[ -n "${bare}" ]] && windows && ! listed "${TOOLCHAIN}/bin/${command}"; then
      failures+=("${tool}: .toolchain/bin/${command} is missing, the name without .exe its extension runs")
    fi
  done
  # LLVM's, of the major it is pinned to, as linked from the system's now.
  read_llvm_major
  for tool in "${LLVM_TOOLS[@]}"; do
    read_file "${probes}/${tool}.status"
    status="${contents%$'\n'}"
    read_file "${probes}/${tool}"
    read_major "${contents}"
    if [[ ! -e "${TOOLCHAIN}/bin/${tool}" ]]; then
      failures+=("${tool}: .toolchain/bin/${tool} does not exist: install LLVM ${llvm_major}'s, then run make toolchain")
    elif [[ "${status}" != 0 ]]; then
      failures+=("${tool}: .toolchain/bin/${tool} does not run")
    elif [[ "${major}" != "${llvm_major}" ]]; then
      failures+=("${tool}: .toolchain/bin/${tool} ($(readlink -f "${TOOLCHAIN}/bin/${tool}")) is not LLVM ${llvm_major}, it prints: $(head -n 1 <<<"${contents}")")
    fi
  done
  # Prettier, at the version the lockfile pins: its "node_modules/prettier"
  # entry's first "version", matched without forking.
  read_file "${LOCKFILE}"
  lockfile="${contents}"
  pin="${contents#*\"node_modules/prettier\": \{}"
  pin="${pin#*\"version\": \"}"
  pin="${pin%%\"*}"
  read_file "${INSTALLED_LOCKFILE}"
  if [[ "${contents}" != "${lockfile}" ]]; then
    failures+=("prettier: .toolchain/node_modules is not installed from package-lock.json's prettier ${pin}")
  else
    read_file "${probes}/prettier.status"
    status="${contents%$'\n'}"
    read_file "${probes}/prettier"
    if [[ "${status}" != 0 ]]; then
      failures+=("prettier: ${PRETTIER#"${ROOT}/"} does not run")
    elif [[ $'\n'"${contents}" != *$'\n'"${pin}"$'\n'* ]]; then
      failures+=("prettier: ${PRETTIER#"${ROOT}/"} is not ${pin}, it prints: $(head -n 1 <<<"${contents}")")
    fi
  fi
  # The editor's binaries: an extension whose path names nothing falls back to
  # its bundled copy without a word. A binary passes with or without .exe, as
  # Windows' bin has only the one and the others' only the other.
  while IFS= read -r path; do
    path="${path#\$\{workspaceFolder\}/}"
    path="${path#\"}"
    if [[ ! -e "${ROOT}/${path}" && ! -e "${ROOT}/${path%.exe}" &&
      ! -e "${ROOT}/${path}.exe" ]]; then
      failures+=(".vscode/settings.json names ${path}, which does not exist")
    fi
  done < <(grep -oE '(\$\{workspaceFolder\}/|")\.toolchain/[^"$:;]*' \
    "${ROOT}/.vscode/settings.json" | sort -u)
  # StyLua's extension can't run .toolchain/bin's: it expands no variable, and
  # looks for a relative path from the file system's root. It downloads the
  # release the settings name instead, which must be the pinned one.
  read_file "${ROOT}/.vscode/settings.json"
  if [[ "${contents}" != *"\"stylua.targetReleaseVersion\": \"v${STYLUA_VERSION}\""* ]]; then
    failures+=(".vscode/settings.json's stylua.targetReleaseVersion is not v${STYLUA_VERSION}")
  fi
  if ((${#failures[@]} > 0)); then
    printf 'toolchain: %s\n' "${failures[@]}" >&2
    return 1
  fi
}

main() {
  case "${1:-}" in
    install) install ;;
    check) check ;;
    *)
      echo "usage: $0 install|check" >&2
      exit 2
      ;;
  esac
}

main "$@"
