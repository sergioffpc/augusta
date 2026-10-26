# One Pinned Version per Tool, Editor Included

The hooks, the Makefile and CI ran the formatters and linters at pinned
versions, but the editor did not: each VS Code extension ran the binary it
bundled, downloaded or found first on PATH. The Ruff extension formatted with
its bundled 0.17.2 against CI's 0.16.10, clangd was Apple's rather than LLVM
21's, and shfmt was whatever Homebrew had. A save could then write what CI
rejects. So every tool the repository runs now has one version, written in one
place, and the editor, the terminal, the hooks and CI all run that same binary,
on macOS, Linux and Windows.

**The toolchain is every pinned tool.** "Toolchain" names the whole set of tools
the repository builds, formats, lints, scans and operates with: the formatters
and linters, the tools CI's jobs run (trivy, kubeconform, the Kyverno CLI,
promtool, amtool, hadolint, oras, syft, grype, yq, buf) and the operators'
(flux, cosign). What a package manager installs stays on the system, because
Homebrew, apt and winget pin no exact version: LLVM (pinned to its major,
ADR-0008), CMake, Ninja, uv, git, Podman, Mesa, the Vulkan loader, kubectl, helm
and the rest. LLVM's `clangd`, `clang-format` and `clang-tidy` belong to the
toolchain as links, so the editor's clangd is the hooks' LLVM.

**One file of versions.** `scripts/bootstrap/versions.env` (`NAME=VALUE`) holds
every version, which the Makefile includes and bash sources, LLVM's
(`LLVM_VERSION`) among them: CI's Windows jobs and the Windows bootstrap install
that release, and `.toolchain/bin` links its major's tools. Each
`scripts/bootstrap/<platform>.sh` holds its downloads' SHA-256s beside it.
Prettier is the exception: `scripts/bootstrap/package.json` and its
`package-lock.json` pin it, with the lockfile's integrity hashes, installed by
`npm ci` under the Node of the pinned `nodejs-wheel`, so no Node is installed on
the system. PyPI tools (ruff, shfmt, shellcheck, gersemi, pymarkdown, yamllint,
actionlint) are installed with `uv tool install`, as is ctcache from its pinned
commit, with the Python that runs them kept in the store too, so a store
restored on another machine (CI's cache) runs; release binaries, buf among them,
are downloaded and verified. luacheck has no release for macOS or Linux arm64:
there the system's LuaRocks (Homebrew's, apt's) builds its pinned rock into the
store, whose dependencies (argparse, LuaFileSystem) are LuaRocks' latest. The
rock is built for, and runs on, a pinned Lua 5.4 built from lua.org's source
into the same store directory, never the system's Lua: luacheck loads on Lua 5.1
to 5.4, and Homebrew's Lua is 5.5.

**A shared store, linked per checkout.** Each version of each tool is installed
once per machine, in
`${XDG_CACHE_HOME:-~/.cache}/augusta/toolchain/<tool>/<version>/`, and every
checkout and worktree has a `.toolchain/` (ignored by git) whose `bin/` links
into it. Two branches at different versions keep both side by side, and the
toolchain's gigabyte is not copied per worktree. On Windows, where a symlink may
be refused, `bin/` holds hardlinks or copies.

**`make toolchain` makes `.toolchain/`.** It installs into the store whatever
version is missing and relinks `.toolchain/bin`, idempotently: the command to
run after a pull that bumps a version. The bootstrap's `checkout` step,
`scripts/worktree.sh add`, the dev container's post-create and every CI job that
runs a tool call it, CI with the store cached on the hash of `versions.env` and
the lockfile. The bootstrap's `platform` step (once `toolchain`) installs the
system's packages and fills the store, once per machine; the dev container's
image runs it, so a container only links.

**Everything runs `.toolchain/bin`.** The Makefile, the hooks and CI run its
binaries, not `uv tool run` nor PATH. `pre-commit` compares their versions with
`versions.env` and fails with "run `make toolchain`" on a mismatch. VS Code's
integrated terminals put `${workspaceFolder}/.toolchain/bin` first on PATH, and
`make toolchain` writes `.toolchain/activate` for any other shell, which the
runbooks source.

**The editor points at `.toolchain/bin`.** The workspace settings name each
extension's binary under `${workspaceFolder}/.toolchain/` (ruff, shfmt,
shellcheck, buf and clangd) and Prettier's module by `prettier.prettierPath`.
buf's extension checks that the exact path exists, so on Windows `bin/` also
holds `buf` beside `buf.exe`. Three extensions can be given neither
`${workspaceFolder}` nor a workspace-relative path. StyLua's downloads the
release `stylua.targetReleaseVersion` names instead, which
`make toolchain-check` holds to the pin. luacheck's would run whichever luacheck
is on VS Code's own PATH, so it is not recommended, and `pre-push` lints Lua.
Even Better TOML's bundled Taplo keeps syntax and schema checks while TOML is
not formatted on save; `pre-commit` formats it with the pinned Taplo. The
extensions' own versions stay unpinned: the binary they run decides the output.
The settings apply in a trusted workspace only; in Restricted Mode VS Code runs
Ruff, Prettier and ShellCheck bundled and turns clangd and shfmt off.

## Considered Options

- **The editor not formatting**: format-on-save and extension linting off where
  the versions differ, left to `pre-commit`. No drift in what is committed, but
  no feedback while editing; kept only for Taplo and luacheck, whose extensions
  can't be pointed at the toolchain.
- **Wrapper scripts over `uv tool run`**: one version in one place with no
  install, but VS Code's extensions on Windows cannot run a bash wrapper and
  most run no `.cmd`.
- **One version per machine on PATH** (`~/.local/bin`, `/usr/local/bin`): bare
  names in the settings, but `/usr/bin/clangd` and Homebrew's `shfmt` come first
  unless the bootstrap prepends it to every shell, and a branch bumping a
  version would make every other worktree wrong.
- **A `package.json` at the root**: Prettier's extension finds `node_modules`
  unaided, but the root of a C++ repository gains a Node project, and buf's
  native binary sits in a platform-specific package no single setting names.

## Consequences

- An extension whose binary is missing falls back to its bundled one (Ruff,
  Prettier) or downloads the latest (buf) without saying so; `pre-commit`
  catches the drift before it is committed, and the Makefile fails outright.
- Bumping a tool is one line in `versions.env` plus its SHA-256s, or one
  `npm install` for Prettier; every developer then runs `make toolchain`.
- A fresh worktree has no `.toolchain/` until `scripts/worktree.sh add` or
  `make toolchain` makes it.
- Uninstalling the `platform` step removes the store; each checkout's
  `.toolchain/` goes with the checkout.
- On Windows the store's path must stay short: past about 130 characters (an
  `XDG_CACHE_HOME` deep in a temporary directory), the files of pymarkdown's and
  ctcache's environments pass Windows' 260-character path limit and those tools
  fail. The default, `%USERPROFILE%\.cache`, is short enough.
