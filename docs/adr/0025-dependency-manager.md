# Dependency Manager: vcpkg

Dependencies are managed via vcpkg (MIT, manifest mode via `vcpkg.json`), integrated in CI via `lukka/run-vcpkg`.

## Considered Options

Chosen over Conan for broader, more current coverage of this project's specific dependencies (PhysX 5.x, GameNetworkingSockets, meshoptimizer, DirectXTex — all stale or entirely absent on Conan Center) and simpler GitHub Actions integration. Also covers Steam Audio and OpenUSD via maintained vcpkg ports, replacing manual SDK download / `build_usd.py`.

## Consequences

NVIDIA Falcor remains outside any package manager — vendored and built from source (it fetches its own sub-dependencies, e.g. Slang, via NVIDIA's internal Packman tool).
