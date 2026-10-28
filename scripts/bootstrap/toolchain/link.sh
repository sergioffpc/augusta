# shellcheck shell=bash
# shellcheck disable=SC2154 # describe_tool (tools.sh) sets the tool's globals
# Links the checkout's .toolchain/ into the store, for
# scripts/bootstrap/toolchain.sh, which sources this: `make toolchain`.

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
# system's, removing whatever else it holds; links Windows' .toolchain/sysroot
# (sysroot.sh); installs Prettier and writes
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
  link_sysroot
  install_prettier
  write_activate
  cp "${VERSIONS}" "${TOOLCHAIN}/versions.env"
}
