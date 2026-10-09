# Tools

This directory contains every tool, apart from the runtime in `src/`: `pack`
cooks authored content into signed runtime packs, `composer` sets up the
optional USD authoring application and its launcher (both Windows), `swarm`
builds `augusta-swarm`, which fills a server with Scripted players for load and
end-to-end tests, and `replay` builds `augusta-replay`, which replays a match
recording `augustad` wrote. Each tool's tests live beside it. `docs` is not a
tool of its own: it holds how the documentation site is built (ADR-0046).

| Tool                     | Purpose                                                                             |
| ------------------------ | ----------------------------------------------------------------------------------- |
| [`pack/`](pack/)         | Python asset cooker, signing utilities, and native cooking modules.                 |
| [`composer/`](composer/) | Optional NVIDIA USD Composer setup, playback definition, and launcher.              |
| [`swarm/`](swarm/)       | `augusta-swarm`: the Scripted players, a server's worth of headless clients.        |
| [`replay/`](replay/)     | `augusta-replay`: replays a match recording against the server pack it was made on. |
| [`docs/`](docs/)         | The documentation site's MkDocs hooks and Doxyfile, built by `make docs`.           |

## Python environment

The Python tools share one environment: [`pyproject.toml`](pyproject.toml) here
is a uv workspace with `pack` as its member and one lock, `uv.lock`. `uv sync`,
run from this directory, creates `tools/.venv` with the cooker installed
editable, pytest, and MkDocs (the `docs` group, which `make docs` runs alone).
The editor uses it as the workspace's interpreter. Run `uv sync` here, not in a
member, where it would sync that member alone and drop the rest.

## Composer

Composer is an optional authoring tool; it is not needed to cook stages someone
else authored. It runs on Windows (in Git Bash) and Linux. After the
repository's `scripts/bootstrap.sh`, run the separate Composer bootstrap from
the repository root:

```bash
tools/composer/scripts/bootstrap.sh <assets-root>
```

It builds the Augusta app with NVIDIA's Kit App Template (`repo.bat` on Windows,
`repo.sh` on Linux), fetches Adobe's USD-Fileformat-plugins, and copies
`augusta-composer.sh` to `<assets-root>/bin`. The bootstrap checks NVIDIA's
license interactively; for unattended setup, pass `--accept-omniverse-eula` only
after accepting the terms. Launch with `<assets-root>/bin/augusta-composer.sh`,
or `augusta-composer.sh` when `bin` is on `PATH`.

## Pack

The offline asset cooker (ADR-0030): it turns a raw authored OpenUSD stage into
the signed client and server packs the game loads (ADR-0018, ADR-0019). It is a
tooling-time project only - nothing here is linked into the shipped client or
server.

```text
scenarios/<scenario>.yaml -> the map's and characters' stages -> usd-optimize -> usd-validation-nvidia -> cook -> packs/<scenario>/client.pack
                          -> the cue sounds and the scripts                                             -> packs/<scenario>/server.pack
```

1. **usd-optimize** cleans the stage (triangulate, dedupe, flatten, drop small
   geometry). ADR-0015.
2. **usd-validation-nvidia** validates the cleaned stage. Any issue, warnings
   included, aborts the cook and no pack is written. ADR-0015.
3. **cook** walks the stage with `pxr`, converts meshes, colliders, hitboxes,
   spawn points and textures, then packs, BLAKE3-hashes and Ed25519-signs the
   result. ADR-0031, ADR-0032.

The whole pipeline is pure Python except for two small pybind11 modules in
[pack/cpp/](pack/cpp/): `_meshoptimizer` (ADR-0016) and `_textconv` (ADR-0017).
They have no USD dependency on purpose; see ADR-0030 for why.

### Setup

The pack bootstrap creates the cooker's **assets root** (a uv-managed Python
environment, native modules, a signing key and content directories). The cooker
runs on Windows and Linux, the platforms vcpkg's DirectXTex port builds for, and
cooks the same bytes on both. From Git Bash on Windows, or a Linux shell (the
dev container included), after the repository's own `scripts/bootstrap.sh`:

```bash
tools/pack/scripts/bootstrap.sh <assets-root>
```

This bootstraps only the cooker. It is safe to re-run, never regenerates an
existing signing key (which would invalidate every pack already signed with it),
and never overwrites a seeded piece below once it exists at its path.

The script also seeds a small worked example from
[composer/examples/authoring/](composer/examples/authoring/), piece by piece:
`authoring/maps/firebase.usda` (a floor, a prop, a spawn point),
`authoring/characters/soldier.usda` (ADR-0040), `authoring/sounds/` (placeholder
cue sounds, ADR-0020), `authoring/scripts/parameters/rules_of_engagement.lua`
(ADR-0039), `authoring/scripts/rules/last_man_standing.lua` (ADR-0022), and
`authoring/scenarios/firebase.yaml` (the manifest composing them, ADR-0041) -
committed to this repo so a fresh environment has something to cook straight
away:

```powershell
augusta-pack firebase
```

`augusta-pack` takes the scenario's bare name (ADR-0041), always resolved as
`<assets-root>\authoring\scenarios\<name>.yaml` - never a path, and never
resolved from the current directory.

The assets root looks like this:

| Path         | Contents                                                                                                                                                                                                    |
| ------------ | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `authoring/` | `maps/` (ADR-0015), `characters/` (ADR-0040), `sounds/` (ADR-0020), `scripts/parameters/` (ADR-0039), `scripts/rules/` (ADR-0022), `scenarios/<name>.yaml` (ADR-0041) - resolved by `augusta-pack`          |
| `packs/`     | Cooked, signed packs                                                                                                                                                                                        |
| `keys/`      | `signing.key` / `signing.pub` (Ed25519). Never commit these.                                                                                                                                                |
| `bin/`       | `augusta-pack`, `augusta-keygen`, `augusta-inspect`, `augusta-verify`, `augusta-publish` (`.exe` on Windows; installed here by `uv tool install`), plus `augusta-composer.sh` if the Composer bootstrap ran |
| `python/`    | The uv tool venv (`python/pack`), with this project installed editable                                                                                                                                      |
| `tools/`     | Composer and Adobe plugins (installed by the Composer bootstrap)                                                                                                                                            |

### Running the commands

The pack commands are self-contained launchers in `<assets-root>/bin`, so they
run from any directory and any shell, by full path. The Composer launcher,
`<assets-root>/bin/augusta-composer.sh`, is present only if its separate
bootstrap ran, and runs in Git Bash on Windows:

```powershell
<assets-root>\bin\augusta-pack.exe --help
<assets-root>\bin\augusta-keygen.exe --help
<assets-root>\bin\augusta-inspect.exe --help
<assets-root>\bin\augusta-verify.exe --help
<assets-root>\bin\augusta-publish.exe --help
```

To type just `augusta-pack`, add the directory to `PATH` for the current
session:

```powershell
$env:Path = "<assets-root>\bin;$env:Path"
augusta-pack --help
```

The examples below assume `bin` is on `PATH`.

### Cooking a scenario

A scenario is named, not pathed (ADR-0041): `augusta-pack` resolves the bare
name you give it to `<assets-root>\authoring\scenarios\<name>.yaml`, a manifest
naming, by file, the one map, every character, the sound of every cue and the
scripts that scenario composes. Every path in it is relative to `authoring\`,
and any file can be named by several scenarios:

```text
scenarios\test_map.yaml                 # the manifest below
maps\test_map.usda                      # a stage (.usd, .usda, .usdc or .usdz)
characters\marine.usda                  # a character (ADR-0040)
sounds\gunshot.wav                      # a mono PCM WAV for each cue (ADR-0020)
scripts\parameters\default.lua          # Parameters (ADR-0039)
scripts\rules\last_man_standing.lua     # rules: spawns and win condition (ADR-0022)
```

```yaml
# scenarios\test_map.yaml
map: maps/test_map.usda
characters:
  marine: characters/marine.usda              # by name
sounds:
  gunshot: sounds/gunshot.wav
  hit_marker: sounds/hit_marker.wav
  hit_taken: sounds/hit_taken.wav
  death: sounds/death.wav
  match_won: sounds/match_won.wav
  match_lost: sounds/match_lost.wav
scripts:
  parameters: scripts/parameters/default.lua   # required
  rules: scripts/rules/last_man_standing.lua    # optional
```

```powershell
augusta-pack <name>                    # -> <assets-root>\packs\<name>\{client,server}.pack
augusta-pack <name> --skip-validation  # skip usd-validation-nvidia only
```

A successful run ends with the paths of the client and server packs it wrote.

The cooker packs everything the manifest names: the map's stage and every named
character's stage into both packs, and each script into the **server** pack
only, at its role's fixed path, `parameters.lua` or `rules.lua`, whatever its
file is called (ADR-0031). A character's **name** is its key under `characters`
(`marine`), one lowercase word: its own prims are addressed
`<character name>/<prim path>` (e.g. `marine/Character/Visual` - ADR-0040), and
it is what a client names to play it. A client is sent the values a script
decides and never receives the script (ADR-0019). The characters' names, in
manifest order, go into both packs as the `Characters` entry, the list a joining
client's character is checked against (ADR-0042); a manifest naming more than
255 characters fails the cook. Each cue's sound goes into the **client** pack
only, as an audio asset addressed `sounds/<cue>` (e.g. `sounds/gunshot`), with
the prefix `sounds` as the `Sounds` entry the client finds them by (ADR-0020,
ADR-0031); one file may be the sound of several cues. Each character's
`Character/Eye` prim, where its player's camera sits and its Shots leave from,
goes into both packs as that point alone (ADR-0040). It is an error if the
manifest is missing, if it holds a key, cue or script role the cooker does not
know, if a file it names is missing or is not what its key needs (the map and
characters a USD stage, a sound a mono PCM WAV - the error names the file), if a
character's name is not one lowercase word or is a root prim of the map, if a
character has no `Character/Eye`, if a cue has no sound (the error names the
cue), or if there is no `parameters` script: the server reads its Parameters out
of its pack at startup, so that is found here rather than when a server starts
on the pack.

By default, packs are written under `<assets-root>/packs`, keyed by the
scenario's name alone, not its `authoring/scenarios/` position (`firebase` ->
`packs/firebase/client.pack`, `packs/firebase/server.pack`). Pass
`--client-output-pack`/`--server-output-pack` to put them somewhere else.
Scripts are part of the signed pack: to change a value, edit the file and cook
again.

The cooker's geometry reader classifies `UsdGeomMesh`, `UsdGeomCube`, and
`UsdGeomCapsule` (ADR-0032/ADR-0041) - a character authored as any of the three,
like `composer/examples/authoring/characters/soldier.usda`, cooks into real
mesh/collision entries.

#### `augusta-pack` reference

```text
augusta-pack [-h] [--assets-root ASSETS_ROOT]
         [--client-output-pack CLIENT_OUTPUT_PACK]
         [--server-output-pack SERVER_OUTPUT_PACK]
         [--signing-key SIGNING_KEY] [--skip-validation]
         scenario
```

| Argument                                  | Default                                                                 | Description                                                                                                                                     |
| ----------------------------------------- | ----------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------- |
| `scenario` (required)                     |                                                                         | Scenario folder - relative to the current directory or absolute, but must be under `<assets-root>/authoring` (see above).                       |
| `-h`, `--help`                            |                                                                         | Print the usage and option list, then exit.                                                                                                     |
| `--assets-root ASSETS_ROOT`               | the root of the venv the command runs from (`<assets-root>/python/...`) | Assets root holding `authoring/`, `packs/` and `keys/`: `scenario` must sit under its `authoring/`, and it's used for the three defaults below. |
| `--client-output-pack CLIENT_OUTPUT_PACK` | `<assets-root>/packs/<scenario's path under authoring>/client.pack`     | Where to write the client pack. Missing parent directories are created.                                                                         |
| `--server-output-pack SERVER_OUTPUT_PACK` | `<assets-root>/packs/<scenario's path under authoring>/server.pack`     | Where to write the server pack. Missing parent directories are created.                                                                         |
| `--signing-key SIGNING_KEY`               | `<assets-root>/keys/signing.key`                                        | Ed25519 private key (64 bytes) the packs are signed with.                                                                                       |
| `--skip-validation`                       | off                                                                     | Skip usd-validation-nvidia (step 2) for stages that fail its checks. usd-optimize and the cook still run.                                       |

Exit status is `0` on success and `1` if any step fails.

### Signing keys

The bootstrap already generates `keys\signing.key` / `signing.pub`, so you only
need `augusta-keygen` to create an additional keypair:

```powershell
augusta-keygen <key-prefix>   # writes <key-prefix>.key and <key-prefix>.pub
augusta-pack <scenario> --signing-key <key-prefix>.key
```

#### `augusta-keygen` reference

```text
augusta-keygen [-h] prefix
```

| Argument            | Description                                                                                                                                                                |
| ------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `prefix` (required) | Path prefix for the output files: writes `<prefix>.key` (64-byte private key) and `<prefix>.pub` (32-byte public key), raw and unframed. The directory must already exist. |
| `-h`, `--help`      | Print the usage and option list, then exit.                                                                                                                                |

On success it prints the two paths it wrote and exits `0`. The public key is
what the runtime verifies packs against (ADR-0018).

It **overwrites** existing `<prefix>.key` / `<prefix>.pub` without asking. Never
point it at `keys\signing`: that would invalidate every pack already signed with
the current key and the public key already deployed for verification.

### Inspecting and verifying packs

```powershell
augusta-inspect <pack>   # what does the pack contain?
augusta-verify <pack>    # is it intact, and signed by the key I expect?
```

`<pack>` is an ordinary path - relative to the current directory or absolute,
never resolved against an assets root. The `.pack` extension is optional, so
`<stage>.client` finds `<stage>.client.pack`.

#### `augusta-inspect` reference

```text
augusta-inspect [-h] pack
```

| Argument          | Description                                 |
| ----------------- | ------------------------------------------- |
| `pack` (required) | Pack file (see above).                      |
| `-h`, `--help`    | Print the usage and option list, then exit. |

The output follows the file's own layout (ADR-0031), each section with its
offset and size in bytes:

- **Header:** magic, format version, data offset, index offset, index count and
  the client pack it names (a server pack's), or `none`.
- **Data:** only its offset and size. The blobs themselves are not read.
- **Index:** one line per entry with its type (`mesh`, `texture`, `audio`,
  `collision`, `spawn-point`, `hitbox`, `scene`, `script`, `characters`, `eye`
  or `sounds`), its offset and size within the pack, and its pack-relative path.
- **Trailer:** the stored BLAKE3 hash and Ed25519 signature, in hex.

Only the header, index and trailer are read, so it is fast on large packs. It
does **not** check the hash or signature (the trailer is labelled as
unverified): use `augusta-verify` before trusting the contents.

#### `augusta-verify` reference

```text
augusta-verify [-h] [--assets-root ASSETS_ROOT] [--public-key PUBLIC_KEY] pack
```

| Argument                    | Default                                    | Description                                                      |
| --------------------------- | ------------------------------------------ | ---------------------------------------------------------------- |
| `pack` (required)           |                                            | Pack file (see above).                                           |
| `-h`, `--help`              |                                            | Print the usage and option list, then exit.                      |
| `--assets-root ASSETS_ROOT` | the root of the venv the command runs from | Assets root, for the `--public-key` default only.                |
| `--public-key PUBLIC_KEY`   | `<assets-root>/keys/signing.pub`           | Ed25519 public key (32 bytes) the signature must verify against. |

It recomputes the BLAKE3 hash of everything but the trailer, compares it with
the trailer's hash, verifies the trailer's Ed25519 signature over that hash, and
only then checks that the header and index are well formed (ADR-0031). On
success it prints the entry count, size, hash and key used and exits `0`. On any
failure it prints the reason to stderr and exits `1`:

| Message                                                     | Meaning                                                                     |
| ----------------------------------------------------------- | --------------------------------------------------------------------------- |
| `content hash mismatch`                                     | The pack was corrupted or modified after it was signed.                     |
| `signature is not valid for this public key`                | The pack was signed by a different key, or the signature was tampered with. |
| `bad magic`, `unsupported format version`, `too small`, ... | The file isn't a (supported) Augusta pack.                                  |

### Publishing a server pack

```powershell
augusta-publish <scenario> --host <node>
```

Puts a scenario's server pack on the k3s node's shared asset-pack volume, where
the cluster's servers read it (ADR-0026). It publishes; it does not deploy: a
server serves the pack once its environment's `HelmRelease` in
`clusters/onprem/apps/` names the version it prints
([Rotate the Pack Signing Key](../docs/runbooks/pack-key-rotation.md), step 7).

Before copying anything it verifies both packs of the scenario's cook against
the public key, and checks that the server pack names that client pack in its
header (ADR-0031). It then copies only the server pack and the public key, as
`server.pack` and `signing.pub`, into
`/srv/augusta/asset-packs/<scenario>/<version>/`, `<version>` being the first 12
hex characters of the server pack's BLAKE3 hash. The folder is assembled beside
its final place and renamed into it, so it never exists half-written, and a
folder that exists is never written again: publishing the same cook twice checks
the node holds the same files and copies nothing, and a folder holding other
files is an error.

It runs `ssh` and `scp` from `PATH` (on Windows, the OpenSSH client), as a user
that can `sudo` on the node without a password.

#### `augusta-publish` reference

```text
augusta-publish [-h] --host HOST [--assets-root ASSETS_ROOT] [--client-pack CLIENT_PACK]
                [--server-pack SERVER_PACK] [--public-key PUBLIC_KEY] scenario
```

| Argument                    | Default                                      | Description                                                                                                                     |
| --------------------------- | -------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------- |
| `scenario` (required)       |                                              | Scenario name (ADR-0041), which also names its server in the cluster: lowercase letters, digits and `-`, at most 46 characters. |
| `--host HOST` (required)    |                                              | The k3s node, as `ssh` names it (an address, or an alias in `~/.ssh/config`).                                                   |
| `-h`, `--help`              |                                              | Print the usage and option list, then exit.                                                                                     |
| `--assets-root ASSETS_ROOT` | the root of the venv the command runs from   | Assets root, for the defaults below only.                                                                                       |
| `--client-pack CLIENT_PACK` | `<assets-root>/packs/<scenario>/client.pack` | The client pack of the cook. Verified, never copied.                                                                            |
| `--server-pack SERVER_PACK` | `<assets-root>/packs/<scenario>/server.pack` | The server pack to publish.                                                                                                     |
| `--public-key PUBLIC_KEY`   | `<assets-root>/keys/signing.pub`             | The key both packs are signed with, published as `signing.pub`.                                                                 |

On success it prints the folder on the node and the line to put under the
scenario's server in the `HelmRelease`
(`servers.<scenario>.packVersion: "<version>"`), and exits `0`, whether it
copied the pack or found it there already. On any failure it prints the reason
to stderr and exits `1`.

### Layout

| Path                               | Role                                                                                                                                                                                                                                                                                                                             |
| ---------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `pack/src/pack/cli.py`             | `augusta-pack` entry point                                                                                                                                                                                                                                                                                                       |
| `pack/src/pack/scenario.py`        | Scenario folder resolution (stage, `*.lua` scripts, cue sounds)                                                                                                                                                                                                                                                                  |
| `pack/src/pack/sounds.py`          | The cue catalogue and the mono PCM WAV reader                                                                                                                                                                                                                                                                                    |
| `pack/src/pack/optimize.py`        | usd-optimize step                                                                                                                                                                                                                                                                                                                |
| `pack/src/pack/validate.py`        | usd-validation-nvidia step                                                                                                                                                                                                                                                                                                       |
| `pack/src/pack/cook.py`            | Stage walk and asset conversion                                                                                                                                                                                                                                                                                                  |
| `pack/src/pack/pack.py`, `wire.py` | Pack wire format, hashing, signing                                                                                                                                                                                                                                                                                               |
| `pack/src/pack/keys.py`            | `augusta-keygen` and key file I/O                                                                                                                                                                                                                                                                                                |
| `pack/src/pack/pack_cli.py`        | `augusta-inspect` and `augusta-verify` entry points                                                                                                                                                                                                                                                                              |
| `pack/src/pack/publish.py`         | `augusta-publish` entry point                                                                                                                                                                                                                                                                                                    |
| `pack/src/pack/reader.py`          | Pack container parsing and verification (the read side of `pack.py`)                                                                                                                                                                                                                                                             |
| `pack/src/pack/assets_root.py`     | Assets-root inference shared by the entry points                                                                                                                                                                                                                                                                                 |
| `pack/cpp/`                        | Standalone CMake/vcpkg project for the two native modules. It builds straight into `pack/src/pack/`.                                                                                                                                                                                                                             |
| `composer/`                        | Composer bootstrap, playback file that scaffolds the app, and `augusta-composer.sh` (launches it)                                                                                                                                                                                                                                |
| `pack/tests/`                      | pytest suite and the USD fixtures it cooks (see Running the tests)                                                                                                                                                                                                                                                               |
| `composer/examples/authoring/`     | A committed `<assets-root>/authoring/` sample the pack bootstrap seeds into a fresh assets root: `maps/firebase.usda` (ADR-0015), `characters/soldier.usda` (ADR-0040), `sounds/` (placeholder cue sounds, ADR-0020), `scripts/` (Parameters and rules, ADR-0039, ADR-0022), `scenarios/firebase.yaml` composing them (ADR-0041) |
| `pack/scripts/bootstrap.sh`        | Builds the pack assets root                                                                                                                                                                                                                                                                                                      |

### Rebuilding the native modules

The bootstrap builds them once and skips the step if the `.pyd` (Windows) or
`.so` (Linux) files exist. To rebuild after changing `cpp/`, delete them from
`src/pack/` and re-run the bootstrap, or build directly:

```powershell
cmake --preset windows -S tools\pack\cpp "-DPYTHON_EXECUTABLE=<assets-root>\python\pack\Scripts\python.exe"
cmake --build tools\pack\cpp\build\x64-windows
```

On Linux, with the `linux` preset (clang):

```bash
cmake --preset linux -S tools/pack/cpp "-DPYTHON_EXECUTABLE=<assets-root>/python/pack/bin/python"
cmake --build tools/pack/cpp/build/x64-linux
```

`PYTHON_EXECUTABLE` must point at the venv so the extension's ABI matches the
interpreter that imports it.

### Running the tests

The tests live in [pack/tests/](pack/tests/) and run with pytest in the tools'
shared environment, `tools/.venv` (see Python environment above), not the assets
root's venv. They need the native modules built into `src/pack/` (above) against
that environment's interpreter:

```powershell
cd tools
uv sync
cmake --preset windows -S pack\cpp "-DPYTHON_EXECUTABLE=$PWD\.venv\Scripts\python.exe"
cmake --build pack\cpp\build\x64-windows
uv run pytest pack
```

On Linux:

```bash
cd tools
uv sync
cmake --preset linux -S pack/cpp "-DPYTHON_EXECUTABLE=$PWD/.venv/bin/python"
cmake --build pack/cpp/build/x64-linux
uv run pytest pack
```

The USD stages the cook tests read, including the malformed ones, are in
[pack/tests/fixtures/](pack/tests/fixtures/); the end-to-end test cooks
[composer/examples/authoring/](composer/examples/authoring/) with a throwaway
key.

#### Golden packs

`../tests/fixtures/example-packs/` at the repository root holds the example
scenario's client and server packs, cooked with the test key next to them (never
the release key). The C++ runtime's tests load them, and
`pack/tests/test_golden.py` requires the cooker to still write them byte for
byte: that is the contract between the two implementations of the pack format
(ADR-0013). After a deliberate change to the format or to the example,
regenerate them from `tools/pack` and commit the result:

```powershell
$golden = "..\..\tests\fixtures\example-packs"
uv run augusta-pack firebase --assets-root ..\composer\examples --signing-key $golden\test.key `
  --client-output-pack $golden\client.pack --server-output-pack $golden\server.pack
```

## Swarm

`augusta-swarm` plays the scenario's Player count of Scripted players (see
[CONTEXT.md](../CONTEXT.md)) against a running `augustad` until every one has
seen a number of Match ends, then exits 0; it exits 1 as soon as one fails or a
timeout passes (ADR-0013). Each player is a client with no window or GPU: it
predicts and sends its Commands like `augustac`, but decides them itself from
what the server tells it, from a seed. Like `augusta-replay`, it is C++ built
with the engine, on Windows and Linux, in every build: the CMake option
`AUGUSTA_TOOLS`, on by default, builds the C++ tools all together or none (the
server image turns it off):

```powershell
cmake --preset windows
cmake --build --preset windows --target augusta-swarm
# build/x64-windows/tools/swarm/augusta-swarm.exe
```

It reads `augusta-swarm.yaml` next to the executable, or the file `--config`
names (ADR-0034); copy
[`swarm/augusta-swarm.example.yaml`](swarm/augusta-swarm.example.yaml), which
documents every key. Its pack must be the client pack cooked with the server's,
and its scenario's Player count 2 or more: a Match of one ends only when its
player dies, which nothing in it can cause. The example scenario's is 1, so a
single player can run it alone.

On Linux the preset is `linux`, and the executable lands in
`build/x64-linux/tools/swarm/`.

Its tests are in [`swarm/tests/`](swarm/tests/). They join `augusta_tests`, and
`ctest` runs them with the engine's:

```powershell
cmake --build --preset windows --target augusta_tests
ctest --preset windows
```

but for the netcode tests (label `netcode`): runs of Scripted players under
simulated latency, jitter, loss and reordering, minutes each, which every test
preset leaves out and the nightly runs (ADR-0013). To run them by hand, on
Linux:

```sh
ctest --preset linux-netcode
```

or on Windows, `ctest --test-dir build/x64-windows -L netcode`.

## Replay

`augusta-replay` replays a match recording `augustad` wrote (its
`simulation.recording` setting) on a fresh SimulationWorld, and checks that
every tick resolves what it recorded (ADR-0048):

```text
augusta-replay <recording> <server pack> <public key> [--across-builds]
```

The pack must be the one the recording names. Without `--across-builds` the
outcome must match exactly, which holds on the build that recorded it; with it,
positions may be a grid step off. It exits 0 when every tick matches, 1 when one
diverges (printing both sides' bodies), and 2 when the replay cannot start. It
is C++ built with the engine on every platform, by every build (`AUGUSTA_TOOLS`,
as `augusta-swarm` is):

```powershell
cmake --build --preset windows-debug --target augusta-replay
# build/x64-windows-debug/tools/replay/augusta-replay.exe
```

Its tests are in [`replay/tests/`](replay/tests/) and join `augusta_tests`,
among them the golden match (ADR-0013): a recording of a scripted duel on the
example scenario's golden server pack,
[`replay/tests/fixtures/golden_match.rec`](replay/tests/fixtures/golden_match.rec),
which must replay to its recorded outcome on every build. After a deliberate
change to the simulation, rewrite it and commit the result:

```powershell
cmake --build --preset windows-debug --target augusta_golden_match
```
