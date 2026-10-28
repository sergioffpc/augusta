# shellcheck shell=bash
# shellcheck disable=SC2034 # command: install_release (store.sh) reads it
# shellcheck disable=SC2154 # release_*: windows.sh's toolchain_release sets
# Windows' MSVC sysroot (ADR-0079), for scripts/bootstrap/toolchain.sh, which
# sources this: the STL, the CRT, ATL and the Windows SDK, which xwin downloads
# from Microsoft at the versions scripts/bootstrap/versions.env pins, in the
# layout clang-cl's /winsysroot reads. On Windows alone: elsewhere each of
# these does nothing. Microsoft's, so it lives only in the store, which CI
# caches, and never in the repository, an image or a release.

# The sysroot's name, of every pin that changes what xwin makes, short as
# Windows' paths are: its longest file is 119 characters below it. The last
# part names what fill_sysroot asks xwin for beyond its default, the debug
# CRT's libraries and DLLs: a sysroot without them keeps another name.
SYSROOT_VERSION="${MSVC_CRT_VERSION}-${WINDOWS_SDK_VERSION}"
readonly SYSROOT_VERSION+="-${VS_MANIFEST_VERSION}-${XWIN_VERSION}-d"
readonly SYSROOT_STORE="${STORE}/sysroot/${SYSROOT_VERSION}"
# Where the checkout's build finds it.
readonly SYSROOT_LINK="${TOOLCHAIN}/sysroot"

# Installs xwin's release into the store, unless it is there.
# Globals:
#   command: set to xwin, for install_release.
install_xwin() {
  local dir="${STORE}/xwin/${XWIN_VERSION}"
  [[ -f "${dir}/.installed" ]] && return
  echo "toolchain: installing xwin ${XWIN_VERSION}"
  rm -rf "${dir}"
  mkdir -p "${dir}"
  command=xwin
  toolchain_release xwin
  install_release "${dir}"
  touch "${dir}/.installed"
}

# Makes the pinned sysroot in the store with xwin, unless it is there: a
# .installed file, written last with the sysroot's name, marks it complete.
# xwin reads the pinned package manifest, downloaded and checked here, from its
# download cache, through a channel manifest written to name it: it never asks
# Microsoft which manifest is current. The download cache goes once the
# sysroot is made.
fill_sysroot() {
  local dir="${SYSROOT_STORE}" cache="${SYSROOT_STORE}.download"
  local manifest="${cache}/dl/pkg_manifest_${VS_MANIFEST_SHA256}.vsman"
  windows || return 0
  [[ -f "${dir}/.installed" ]] && return
  install_xwin
  echo "toolchain: installing the MSVC sysroot ${SYSROOT_VERSION}"
  rm -rf "${dir}" "${cache}"
  mkdir -p "${cache}/dl"
  curl --proto '=https' --tlsv1.2 -fsSLo "${manifest}" "${VS_MANIFEST_URL}"
  if ! sha256_matches "${VS_MANIFEST_SHA256}" "${manifest}"; then
    echo "toolchain: SHA-256 mismatch for ${VS_MANIFEST_URL}" >&2
    rm -rf "${cache}"
    exit 1
  fi
  cat >"${cache}/channel.json" <<EOF
{"channelItems": [{"id": "Microsoft.VisualStudio.Manifests.VisualStudio",
  "version": "${VS_MANIFEST_VERSION}", "type": "Manifest",
  "payloads": [{"fileName": "VisualStudio.vsman",
    "sha256": "${VS_MANIFEST_SHA256}", "size": 0,
    "url": "${VS_MANIFEST_URL}"}]}]}
EOF
  # By accepting Microsoft's license, as each machine's download does. ATL is
  # vcpkg's llvm port's. The debug CRT's libraries link every Debug build,
  # vcpkg's debug ports among them, and its DLLs run the windows-debug
  # preset's tests; neither may be redistributed, and neither leaves the store.
  "${STORE}/xwin/${XWIN_VERSION}/bin/xwin.exe" --accept-license \
    --log-level warn --cache-dir "$(native_path "${cache}")" \
    --manifest "$(native_path "${cache}/channel.json")" \
    --crt-version "${MSVC_CRT_VERSION}" --sdk-version "${WINDOWS_SDK_VERSION}" \
    --include-atl --include-debug-runtime splat --include-debug-libs \
    --use-winsysroot-style --preserve-ms-arch-notation \
    --disable-symlinks --output "$(native_path "${dir}")"
  rm -rf "${cache}"
  echo "${SYSROOT_VERSION}" >"${dir}/.installed"
}

# Links .toolchain/sysroot to the pinned sysroot of the store, unless it is
# linked already: a directory junction, which Windows makes without Developer
# Mode or an administrator, and removes without touching its target.
link_sysroot() {
  windows || return 0
  read_file "${SYSROOT_LINK}/.installed"
  [[ "${contents%$'\n'}" == "${SYSROOT_VERSION}" ]] && return
  if [[ -e "${SYSROOT_LINK}" || -L "${SYSROOT_LINK}" ]]; then
    cmd //c rmdir "$(cygpath -w "${SYSROOT_LINK}")"
  fi
  cmd //c mklink //J "$(cygpath -w "${SYSROOT_LINK}")" \
    "$(cygpath -w "${SYSROOT_STORE}")" >/dev/null
}

# Checks that .toolchain/sysroot links the pinned sysroot.
check_sysroot() {
  local linked
  windows || return 0
  read_file "${SYSROOT_LINK}/.installed"
  linked="${contents%$'\n'}"
  if [[ -z "${linked}" ]]; then
    fail "sysroot: .toolchain/sysroot is missing: run make toolchain"
  elif [[ "${linked}" != "${SYSROOT_VERSION}" ]]; then
    fail "sysroot: .toolchain/sysroot is ${linked}, not the MSVC sysroot" \
      "${SYSROOT_VERSION} versions.env pins: run make toolchain"
  fi
}
