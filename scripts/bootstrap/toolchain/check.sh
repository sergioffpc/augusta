# shellcheck shell=bash
# shellcheck disable=SC2154 # describe_tool (tools.sh) sets the tool's globals
# Checks the checkout's .toolchain/ against the pins, for
# scripts/bootstrap/toolchain.sh, which sources this: `make toolchain-check`
# and the pre-commit hook.

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

# Records a probe that failed to run, with its exit status and the first line
# it printed, which says why.
# Globals:
#   status, contents: the probe's, from read_probe.
# Arguments:
#   What failed to run.
fail_to_run() {
  local line
  line="$(head -n 1 <<<"${contents}")"
  fail "$1 does not run (exit ${status})${line:+, it prints: ${line}}"
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
      fail_to_run "${tool}: .toolchain/bin/${command}"
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
      fail_to_run "${tool}: .toolchain/bin/${tool}"
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
  local pinned pin
  read_file "${LOCKFILE}"
  pinned="${contents}"
  pin="${contents#*\"node_modules/prettier\": \{}"
  pin="${pin#*\"version\": \"}"
  pin="${pin%%\"*}"
  read_file "${INSTALLED_LOCKFILE}"
  if [[ "${contents}" != "${pinned}" ]]; then
    fail "prettier: .toolchain/node_modules is not installed from" \
      "package-lock.json's prettier ${pin}"
    return
  fi
  read_probe prettier
  if [[ "${status}" != 0 ]]; then
    fail_to_run "prettier: ${PRETTIER#"${ROOT}/"}"
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
  check_sysroot
  if ((${#failures[@]} > 0)); then
    printf 'toolchain: %s\n' "${failures[@]}" >&2
    return 1
  fi
}
