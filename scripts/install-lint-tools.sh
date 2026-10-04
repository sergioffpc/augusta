#!/usr/bin/env bash
# Installs the formatters and linters the git hooks and CI's format job run
# that Ubuntu doesn't package at CI's versions - uv (for yamllint), yamlfmt,
# StyLua, luacheck and taplo - into the directory given (default
# ~/.local/bin). CI's format job pins the same versions and checksums of the
# last four; bump them together. uv is its latest release: yamllint, which it
# runs, is the pinned one.
# Shared by bootstrap-wsl.sh and the dev container's image.
set -euo pipefail

bin_dir="${1:-$HOME/.local/bin}"
mkdir -p "$bin_dir"

if ! command -v uv >/dev/null 2>&1; then
  curl -LsSf https://astral.sh/uv/install.sh | env UV_UNMANAGED_INSTALL="$bin_dir" sh
fi

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

yamlfmt_asset="yamlfmt_0.21.0_Linux_x86_64.tar.gz"
curl -fsSLo "$tmp/$yamlfmt_asset" \
  "https://github.com/google/yamlfmt/releases/download/v0.21.0/$yamlfmt_asset"
curl -fsSLo "$tmp/stylua.zip" \
  https://github.com/JohnnyMorganz/StyLua/releases/download/v2.5.2/stylua-linux-x86_64.zip
curl -fsSLo "$tmp/luacheck" \
  https://github.com/lunarmodules/luacheck/releases/download/v1.2.0/luacheck
curl -fsSLo "$tmp/taplo.gz" \
  https://github.com/tamasfe/taplo/releases/download/0.10.0/taplo-linux-x86_64.gz
printf '%s  %s\n' \
  '1f300d9257b232bb3b541d7fb1b0e6b3c121bcbab381c86cd38cb8722be8a566' "$tmp/$yamlfmt_asset" \
  'bcb0d855e91f102f28a370e850f8566b3b44b79e6274d806ea5246837c0fd5ab' "$tmp/stylua.zip" \
  'd68da17fca0697d9e2fb04201f3884abd259fa558b3a449bccaed47f1390defc' "$tmp/luacheck" \
  '8fe196b894ccf9072f98d4e1013a180306e17d244830b03986ee5e8eabeb6156' "$tmp/taplo.gz" \
  | sha256sum --check --status

tar -xzf "$tmp/$yamlfmt_asset" -C "$tmp"
unzip -q "$tmp/stylua.zip" -d "$tmp"
gunzip "$tmp/taplo.gz"
install -m 0755 "$tmp/yamlfmt" "$tmp/stylua" "$tmp/luacheck" "$tmp/taplo" "$bin_dir"
