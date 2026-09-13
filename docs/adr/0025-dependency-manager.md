# Dependency Manager: vcpkg

Dependencies are managed via vcpkg (MIT, manifest mode via `vcpkg.json`), vendored as a pinned git submodule (`third_party/vcpkg`) rather than installed via a CI action like `lukka/run-vcpkg` — a fixed submodule commit gives reproducible dependency resolution per-clone without depending on a third-party action's own version pinning. See `docs/ENGINEERING.md`'s CI/CD section for the binary-cache mechanism (a GitHub Packages NuGet feed, not vcpkg's native GitHub-Actions-cache backend, which was removed upstream).

## Considered Options

Chosen over Conan for broader, more current coverage of this project's specific dependencies (PhysX 5.x, GameNetworkingSockets, meshoptimizer, DirectXTex — all stale or entirely absent on Conan Center) and simpler GitHub Actions integration. Also covers Steam Audio and OpenUSD via maintained vcpkg ports, replacing manual SDK download / `build_usd.py`.

## Consequences

NVIDIA Falcor remains outside any package manager — vendored and built from source (it fetches its own sub-dependencies, e.g. Slang, via NVIDIA's internal Packman tool).
