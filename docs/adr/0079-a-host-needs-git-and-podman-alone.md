# A Host Needs Git and Podman Alone

The bootstrap installed the project's system packages and pinned tools on each
host, and CI's Ubuntu release fixed their versions. A host on any other release
then drifted: on Ubuntu 26.10, clang 22 replaced the pinned LLVM 21 and PhysX's
`-Weverything -Werror` stopped the build. So development now happens in one
place, the dev container, whose image already was CI's Linux environment.

**`scripts/bootstrap.sh`, given no step, readies the container.** A host needs
git and Podman, in WSL on Windows and with a Podman machine on macOS. The
`platform` and `checkout` steps stay: they are the recipe the image and CI's
native jobs run, so this ADR changes where they run, not what they do.

**Hooks follow the toolchain.** A checkout with no host `.toolchain/bin` runs
its `pre-commit` and `pre-push` hooks in the container, and a checkout that has
one keeps running them on its host.

**Git stays on the host, with its identity, signing and credentials.** Git LFS
is the seam: a host without git-lfs gets repository-local filters that run the
container's, and uploading a new LFS object takes git-lfs on the host, because
the container holds no credentials.

**What the container gives up.** The native Windows client (MSVC and clang-cl)
and the native macOS client build in CI alone, so a failure only they reproduce
is debugged through CI or on a host that ran the two steps by hand. The
container does not open a window on the host's display or reach its GPU: it
compiles the client and tests it on Mesa's software Vulkan.
