#!/bin/bash
# Stages the client's whole runtime folder from a build, with
# `cmake --install --component client` (src/client/CMakeLists.txt), and checks
# that augustac runs from it alone. Given an archive, it also archives the
# folder, extracts it into an empty one, and checks that augustac runs from
# there and, given a version, prints it: what a release attaches (release.yml,
# ADR-0058). On Windows the archive is a zip of the folder's contents; on Linux
# a gzipped tarball of one augustac/ folder.
#
# The check runs `augustac --version` with only the system's libraries besides
# the folder's: on Windows only Windows' own folders on PATH, on Linux no
# LD_LIBRARY_PATH or LD_PRELOAD. So a library augustac links that is missing
# from the folder fails it rather than being found elsewhere. --version exits
# before the Renderer starts, so a library loaded only once it runs is not
# checked. The folder must also hold none of the development-only libraries the
# client never ships (ADR-0059): a Vulkan loader, which is the system's, the
# Vulkan validation layer, lavapipe, DXC or Slang.
#
# On Linux it also checks that every library each file in the folder links
# (its ELF NEEDED entries) is in the folder or on ALLOWED_SYSTEM_LIBRARIES, and
# that the build's augustac.debug, which a release publishes beside the
# tarball, is the debug info of the augustac in the folder: the same build ID,
# and the name its .gnu_debuglink gives (ADR-0055).
#
#   scripts/stage-client.sh build/x64-windows dist/augustac
#   scripts/stage-client.sh build/x64-windows dist/augustac \
#     dist/augustac-windows-x64.zip 3.0.0
#   scripts/stage-client.sh build/x64-linux dist/augustac \
#     dist/augustac-linux-x64.tar.gz 3.0.0
set -euo pipefail

# The system libraries the Linux client may link and does not ship (ADR-0058):
# glibc's, the Vulkan loader, and the X11, Wayland, xkbcommon and audio
# libraries. Anything else it links must be in its folder, or added here as a
# conscious decision. libstdc++ and libgcc_s are not here: the C++ runtime is
# linked statically (cmake/LlvmToolchain.cmake). glibc's loader is named for
# arm64 too, the dev container's on an Apple silicon Mac, though no release
# packages that client.
readonly ALLOWED_SYSTEM_LIBRARIES=(
  ld-linux-x86-64.so.2 ld-linux-aarch64.so.1 libc.so.6 libm.so.6 libmvec.so.1 libpthread.so.0
  libdl.so.2 librt.so.1
  libvulkan.so.1
  libX11.so.6 libX11-xcb.so.1 libxcb.so.1 libXcursor.so.1 libXext.so.6
  libXi.so.6 libXinerama.so.1 libXrandr.so.2 libXrender.so.1
  libwayland-client.so.0 libwayland-cursor.so.0 libwayland-egl.so.1
  libdecor-0.so.0
  libxkbcommon.so.0 libxkbcommon-x11.so.0
  libasound.so.2 libpulse.so.0 libpulse-simple.so.0 libpipewire-0.3.so.0
  libjack.so.0
)

# The folder the archive is extracted into, removed on exit.
extracted=""

usage() {
  echo "usage: $0 <build-dir> <staging-dir> [<archive> [<version>]]" >&2
  exit 2
}

# Prints the platform the script runs on: windows (Git Bash) or linux.
platform() {
  case "$(uname -s)" in
    MINGW* | MSYS* | CYGWIN*) echo windows ;;
    Linux) echo linux ;;
    *)
      echo "$0: the client is staged on Windows or Linux only" >&2
      exit 1
      ;;
  esac
}

# Runs the current folder's augustac, with no other folder to load libraries
# from but the system's.
# Arguments:
#   augustac's arguments.
run_alone() {
  if [[ "$(platform)" = windows ]]; then
    local system
    system="$(cygpath -u "${SYSTEMROOT}")"
    PATH="${system}/System32:${system}" ./augustac.exe "$@"
  else
    env -u LD_LIBRARY_PATH -u LD_PRELOAD ./augustac "$@"
  fi
}

# Runs augustac --version from a folder alone, and exits 1 if it fails or
# prints another version.
# Arguments:
#   The folder augustac is in.
#   The version it must print (optional; any version without it).
# Outputs:
#   The folder and what augustac printed, to stdout.
check_runs() {
  local folder="$1"
  local version="${2:-}"
  local printed
  if ! printed="$(cd "${folder}" && run_alone --version)"; then
    echo "$0: augustac does not run from ${folder}" >&2
    exit 1
  fi
  if [[ -n "${version}" ]] && [[ "${printed}" != "augustac ${version}" ]]; then
    echo "$0: augustac from ${folder} prints '${printed}'," \
      "not 'augustac ${version}'" >&2
    exit 1
  fi
  echo "${folder}: ${printed}"
}

# Exits 1 if a folder holds a library the client never ships.
# Arguments:
#   The folder augustac is in.
check_no_dev_libraries() {
  local folder="$1"
  local found
  found="$(find "${folder}" \( -type f -o -type l \) \( \
    -iname 'vulkan-1*.dll' -o -name 'libvulkan.so*' \
    -o -iname 'VkLayer_*' -o -name 'libVkLayer_*' \
    -o -iname 'vulkan_lvp.dll' -o -name 'libvulkan_lvp.so*' \
    -o -iname 'dxcompiler.dll' -o -iname 'dxil.dll' -o -name 'libdxcompiler.so*' \
    -o -iname 'slang*.dll' -o -name 'libslang*.so*' \))"
  if [[ -n "${found}" ]]; then
    echo "$0: ${folder} holds libraries the client never ships:" >&2
    echo "${found}" >&2
    exit 1
  fi
}

# Exits 1 if a file in a folder links a library that is neither in the folder
# nor on ALLOWED_SYSTEM_LIBRARIES.
# Arguments:
#   The folder augustac is in.
check_needed() {
  local folder="$1"
  local file needed unlisted=""
  while IFS= read -r -d '' file; do
    [[ "$(head -c 4 "${file}")" = $'\x7fELF' ]] || continue
    while IFS= read -r needed; do
      [[ -e "${folder}/${needed}" ]] && continue
      [[ " ${ALLOWED_SYSTEM_LIBRARIES[*]} " = *" ${needed} "* ]] && continue
      unlisted+="  ${file#"${folder}"/} links ${needed}"$'\n'
    done < <(llvm-readelf --dynamic-table "${file}" \
      | sed -n 's/.*(NEEDED).*\[\(.*\)\]$/\1/p')
  done < <(find "${folder}" -type f -print0)
  if [[ -n "${unlisted}" ]]; then
    echo "$0: ${folder} links libraries neither in it nor on" \
      "ALLOWED_SYSTEM_LIBRARIES: bundle them, or allow them there" >&2
    printf '%s' "${unlisted}" >&2
    exit 1
  fi
}

# Prints an ELF file's build ID.
# Arguments:
#   The file.
build_id() {
  # A .debug file's dynamic table is empty, which llvm-readelf warns about.
  llvm-readelf --notes "$1" 2>/dev/null | sed -n 's/^ *Build ID: *//p'
}

# Exits 1 unless debug info is that of the augustac in a folder: the same
# build ID, under the name augustac's .gnu_debuglink gives.
# Arguments:
#   The folder augustac is in.
#   The debug info.
check_debug_info() {
  local folder="$1"
  local debug="$2"
  if [[ ! -f "${debug}" ]]; then
    echo "$0: the build has no ${debug}" >&2
    exit 1
  fi
  local binary_id debug_id
  binary_id="$(build_id "${folder}/augustac")"
  debug_id="$(build_id "${debug}")"
  if [[ -z "${binary_id}" ]] || [[ "${binary_id}" != "${debug_id}" ]]; then
    echo "$0: ${debug} has build ID '${debug_id}', not the build ID" \
      "'${binary_id}' of ${folder}/augustac" >&2
    exit 1
  fi
  local link
  link="$(llvm-readelf --string-dump=.gnu_debuglink "${folder}/augustac" \
    | sed -n 's/^ *\[ *0\] *//p')"
  if [[ "${link}" != "$(basename "${debug}")" ]]; then
    echo "$0: ${folder}/augustac's .gnu_debuglink names '${link}'," \
      "not '$(basename "${debug}")'" >&2
    exit 1
  fi
}

# Runs every check on a folder.
# Arguments:
#   The build directory, the folder augustac is in, and optionally the version
#   it must print.
check_folder() {
  local build_dir="$1"
  local folder="$2"
  local version="${3:-}"
  check_no_dev_libraries "${folder}"
  if [[ "$(platform)" = linux ]]; then
    check_needed "${folder}"
    check_debug_info "${folder}" "${build_dir}/src/client/augustac.debug"
  fi
  check_runs "${folder}" "${version}"
}

# Archives a folder, and extracts it into an empty one.
# Globals:
#   extracted: set to the folder it is extracted into, removed on exit.
# Arguments:
#   The folder and the archive.
archive() {
  local folder="$1"
  local archive="$2"
  archive="$(realpath -m "${archive}")"
  mkdir -p "$(dirname "${archive}")"
  rm -f "${archive}"
  extracted="$(mktemp -d)"
  trap 'rm -rf "${extracted}"' EXIT
  if [[ "$(platform)" = windows ]]; then
    (cd "${folder}" && cmake -E tar cf "${archive}" --format=zip .)
    (cd "${extracted}" && cmake -E tar xf "${archive}")
    return
  fi
  tar -C "$(dirname "${folder}")" --transform "s,^$(basename "${folder}"),augustac," \
    -czf "${archive}" "$(basename "${folder}")"
  tar -C "${extracted}" -xzf "${archive}"
}

# Stages and checks the folder, then, given an archive, archives and checks it.
# Arguments:
#   The build directory, the staging directory, and optionally the archive and
#   the version it must print.
main() {
  if (($# < 2 || $# > 4)); then
    usage
  fi
  local build_dir="$1"
  local staging_dir="$2"
  local archive="${3:-}"
  local version="${4:-}"
  platform >/dev/null

  rm -rf "${staging_dir}"
  cmake --install "${build_dir}" --component client --prefix "${staging_dir}"
  local folder="${staging_dir}/bin"
  check_folder "${build_dir}" "${folder}"
  [[ -n "${archive}" ]] || return 0

  archive "${folder}" "${archive}"
  local copy="${extracted}"
  [[ "$(platform)" = windows ]] || copy+="/augustac"
  check_folder "${build_dir}" "${copy}" "${version}"
}

main "$@"
