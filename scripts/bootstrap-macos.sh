#!/bin/bash
# Bootstraps a macOS checkout (docs/ENGINEERING.md, Developer Environment) for
# what runs on macOS: the git hooks and `make format-check`, with the
# formatters and linters at the versions CI pins, so a formatting or lint
# failure shows up before the push rather than in CI. Nothing builds natively
# on macOS: the server builds in the dev container, the client on Windows.
#
# Homebrew installs what it versions (LLVM's major, luacheck, uv, Git LFS,
# Doxygen, gh); the formatters CI pins to an exact release are downloaded,
# checksummed, to ~/.local/bin.
#
# Usage: scripts/bootstrap-macos.sh
set -euo pipefail

# CI's format job pins the same versions; bump them together.
readonly YAMLFMT_VERSION=0.21.0
readonly STYLUA_VERSION=2.5.2
readonly TAPLO_VERSION=0.10.0
readonly LUACHECK_VERSION=1.2.0
# The clang CI runs (the ubuntu-26.04 runner's distro package), as
# bootstrap-windows.ps1 pins it: another major formats and lints differently.
readonly LLVM_FORMULA=llvm@21
readonly BIN_DIR="${HOME}/.local/bin"

command -v brew >/dev/null 2>&1 || {
  echo "bootstrap-macos: Homebrew not found - install it from https://brew.sh" \
    "first." >&2
  exit 1
}

brew install "${LLVM_FORMULA}" luacheck uv git-lfs doxygen gh

# luacheck has no versioned formula: say so if Homebrew's has moved on.
luacheck_version="$(luacheck --version | sed -n 's/^Luacheck: //p')"
if [[ "${luacheck_version}" != "${LUACHECK_VERSION}" ]]; then
  echo "bootstrap-macos: luacheck is ${luacheck_version}, CI runs" \
    "${LUACHECK_VERSION} - its findings may differ from CI's." >&2
fi

case "$(uname -m)" in
  arm64)
    yamlfmt_arch=arm64
    yamlfmt_sha=4b417ecb94339d57e4c122ecc948c1a00fe328b5853266de9806e652a92858fa
    stylua_arch=aarch64
    stylua_sha=92ff0889e16324801bc072692974bb67f8161e62010fc90f96c62a17f81f32c7
    taplo_arch=aarch64
    taplo_sha=713734314c3e71894b9e77513c5349835eefbd52908445a0d73b0c7dc469347d
    ;;
  x86_64)
    yamlfmt_arch=x86_64
    yamlfmt_sha=060e943bcb8583c456810eb1ff4721b4f46c4a0c1a4432449d5dc3bbfe29a22b
    stylua_arch=x86_64
    stylua_sha=53c50a1605d0a6345d160a1a5a21db40bcf2bf9cd23c17f7c277a63a1bff3a7f
    taplo_arch=x86_64
    taplo_sha=898122cde3a0b1cd1cbc2d52d3624f23338218c91b5ddb71518236a4c2c10ef2
    ;;
  *)
    echo "bootstrap-macos: unsupported architecture: $(uname -m)" >&2
    exit 1
    ;;
esac

tmp="$(mktemp -d)"
trap 'rm -rf "${tmp}"' EXIT
curl -fsSLo "${tmp}/yamlfmt.tar.gz" \
  "https://github.com/google/yamlfmt/releases/download/v${YAMLFMT_VERSION}/yamlfmt_${YAMLFMT_VERSION}_Darwin_${yamlfmt_arch}.tar.gz"
curl -fsSLo "${tmp}/stylua.zip" \
  "https://github.com/JohnnyMorganz/StyLua/releases/download/v${STYLUA_VERSION}/stylua-macos-${stylua_arch}.zip"
curl -fsSLo "${tmp}/taplo.gz" \
  "https://github.com/tamasfe/taplo/releases/download/${TAPLO_VERSION}/taplo-darwin-${taplo_arch}.gz"
printf '%s  %s\n' \
  "${yamlfmt_sha}" "${tmp}/yamlfmt.tar.gz" \
  "${stylua_sha}" "${tmp}/stylua.zip" \
  "${taplo_sha}" "${tmp}/taplo.gz" \
  | shasum -a 256 --check --status
tar -xzf "${tmp}/yamlfmt.tar.gz" -C "${tmp}" yamlfmt
unzip -q "${tmp}/stylua.zip" -d "${tmp}"
gunzip "${tmp}/taplo.gz"
mkdir -p "${BIN_DIR}"
install -m 0755 "${tmp}/yamlfmt" "${tmp}/stylua" "${tmp}/taplo" "${BIN_DIR}"

# Only clang-format and clang-tidy from the keg-only LLVM, so the rest of it
# doesn't shadow Apple's clang.
llvm_bin="$(brew --prefix "${LLVM_FORMULA}")/bin"
ln -sf "${llvm_bin}/clang-format" "${llvm_bin}/clang-tidy" "${BIN_DIR}"

# A tool earlier on PATH (an older StyLua from cargo, say) would run instead.
for tool in yamlfmt stylua taplo clang-format clang-tidy; do
  resolved="$(command -v "${tool}" || echo nothing)"
  if [[ "${resolved}" != "${BIN_DIR}/${tool}" ]]; then
    echo "bootstrap-macos: ${tool} resolves to ${resolved}, not" \
      "${BIN_DIR}/${tool} - put ${BIN_DIR} first on PATH." >&2
  fi
done

cd "$(git -C "$(dirname "$0")" rev-parse --show-toplevel)"
# LFS's filters only: .githooks' pre-push already runs git lfs pre-push.
git lfs install --skip-repo
git config core.hooksPath .githooks
echo "bootstrap-macos: done. \`make format-check\` runs CI's format checks."
