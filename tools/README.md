# Tools

This directory contains three separate tools: `pack` cooks authored content
into signed runtime packs, `composer` sets up the optional USD authoring
application and its launcher (both Windows), and `loadtest` builds
`augusta-loadtest`, which fills a server with Scripted players for load and
end-to-end tests. `docs` is not a tool of its own: it holds how the
documentation site is built (ADR-0046).

| Tool | Purpose |
|---|---|
| [`pack/`](pack/) | Python asset cooker, signing utilities, and native cooking modules. |
| [`composer/`](composer/) | Optional NVIDIA USD Composer setup, playback definition, and launcher. |
| [`loadtest/`](loadtest/) | `augusta-loadtest`: the Scripted players, a server's worth of headless clients. |
| [`docs/`](docs/) | The documentation site's MkDocs hooks and Doxyfile, built by `make docs`. |

## Composer

Composer is an optional authoring tool; it is not needed to cook stages someone
else authored. After the repository's `scripts\bootstrap-windows.ps1`, run the
separate Composer bootstrap from the repository root:

```powershell
.\tools\composer\scripts\bootstrap-windows.ps1 <assets-root>
```

It builds the Augusta app with NVIDIA's Kit App Template, fetches Adobe's
USD-Fileformat-plugins, and copies `augusta-composer.ps1` to
`<assets-root>\bin`. The bootstrap checks NVIDIA's license interactively; for
unattended setup, pass `-AcceptOmniverseEula` only after accepting the terms.
Launch with `<assets-root>\bin\augusta-composer.ps1` or
`augusta-composer.ps1` when `bin` is on `PATH`.

## Pack

The offline asset cooker (ADR-0030): it turns a raw authored OpenUSD stage into
the signed client and server packs the game loads (ADR-0018, ADR-0019). It is a
tooling-time project only - nothing here is linked into the shipped client or
server.

```
<scenario>/map.usda -> usd-optimize -> usd-validation-nvidia -> cook -> <scenario>/client.pack
<scenario>/*.lua                                                          -> <scenario>/server.pack
```

1. **usd-optimize** cleans the stage (triangulate, dedupe, flatten, drop small
   geometry). ADR-0015.
2. **usd-validation-nvidia** validates the cleaned stage. Any issue, warnings
   included, aborts the cook and no pack is written. ADR-0015.
3. **cook** walks the stage with `pxr`, converts meshes, colliders, hitboxes,
   spawn points and textures, then packs, BLAKE3-hashes and Ed25519-signs the
   result. ADR-0031, ADR-0032.

The whole pipeline is pure Python except for two small pybind11 modules in
[pack/cpp/](pack/cpp/): `_meshoptimizer` (ADR-0016) and `_textconv` (ADR-0017). They have
no USD dependency on purpose; see ADR-0030 for why.

### Setup

The pack bootstrap creates the cooker's **assets root** (a uv-managed Python
environment, native modules, a signing key and content directories). From an
elevated PowerShell, after the repository's own `scripts\bootstrap-windows.ps1`:

```powershell
.\tools\pack\scripts\bootstrap-windows.ps1 <assets-root>
```

This bootstraps only the cooker. It is safe to re-run, never regenerates an
existing signing key (which would invalidate every pack already signed with
it), and never overwrites a seeded piece below once it exists at its path.

The script also seeds a small worked example from
[composer/examples/authoring/](composer/examples/authoring/), piece by piece: `authoring/maps/augusta`
(a floor, a prop, a spawn point), `authoring/characters/player` (ADR-0040),
`authoring/sounds/augusta` (placeholder cue sounds, ADR-0020), and
`authoring/scenarios/augusta` (its `manifest.yaml`, ADR-0041, composing
the other three, plus `parameters.lua` and placeholder `objectives.lua`/
`behaviours.lua` for when game policy, ADR-0022, is wired up) - committed to
this repo so a fresh environment has something to cook straight away:

```powershell
augusta-pack augusta
```

`augusta-pack` takes the scenario's bare name (ADR-0041), always resolved as
`<assets-root>\authoring\scenarios\<name>` - never a path, and never
resolved from the current directory.

The assets root looks like this:

| Path | Contents |
|---|---|
| `authoring/` | `maps/<name>/` (ADR-0015), `characters/<name>/` (ADR-0040), `sounds/<name>/` (ADR-0020), `scenarios/<name>/` (`manifest.yaml` + Lua scripts, ADR-0041) - resolved by `augusta-pack` |
| `packs/` | Cooked, signed packs |
| `keys/` | `augusta.key` / `augusta.pub` (Ed25519). Never commit these. |
| `bin/` | `augusta-pack.exe`, `augusta-keygen.exe`, `augusta-inspect.exe`, `augusta-verify.exe` (installed here by `uv tool install`), plus `augusta-composer.ps1` if the Composer bootstrap ran |
| `python/` | The uv tool venv (`python/pack`), with this project installed editable |
| `tools/` | Composer and Adobe plugins (installed by the Composer bootstrap) |

### Running the commands

The pack commands are self-contained launchers in `<assets-root>/bin`, so they
run from any directory and any shell, by full path. The Composer launcher is
present only if its separate bootstrap ran:

```powershell
<assets-root>\bin\augusta-pack.exe --help
<assets-root>\bin\augusta-keygen.exe --help
<assets-root>\bin\augusta-inspect.exe --help
<assets-root>\bin\augusta-verify.exe --help
<assets-root>\bin\augusta-composer.ps1
```

To type just `augusta-pack`, add the directory to `PATH` for the current session:

```powershell
$env:Path = "<assets-root>\bin;$env:Path"
augusta-pack --help
```

The examples below assume `bin` is on `PATH`.

### Cooking a scenario

A scenario is named, not pathed (ADR-0041): `augusta-pack` resolves the bare name
you give it to `<assets-root>\authoring\scenarios\<name>`, which holds a
`manifest.yaml` naming the one map, every character and the sounds folder that
scenario composes, plus the Lua scripts that go with it:

```
scenarios\test_map\manifest.yaml     # map: maps/test_map, characters: [...]
scenarios\test_map\parameters.lua    # required: the scenario's Parameters
scenarios\test_map\rules\round.lua   # any other *.lua, in any subfolder
maps\test_map\map.usda               # the stage (.usd, .usda, .usdc or .usdz)
characters\marine\character.usda     # a character the manifest can name (ADR-0040)
sounds\test_map\gunshot.wav          # a mono PCM WAV for each cue (ADR-0020)
```

```yaml
# scenarios\test_map\manifest.yaml
map: maps/test_map
characters:
  - characters/marine
sounds: sounds/test_map
```

```powershell
augusta-pack <name>                    # -> <assets-root>\packs\<name>\{client,server}.pack
augusta-pack <name> --skip-validation  # skip usd-validation-nvidia only
```

A successful run ends with the paths of the client and server packs it wrote.

The cooker packs everything the manifest names: the map's stage and every
named character's stage into both packs (a character's own prims addressed
`<manifest path>/<prim path>`, e.g. `characters/marine/Visual` - ADR-0040),
and every `*.lua` file under the scenario folder into the **server** pack
only, as a script asset addressed by its path relative to that folder
(`parameters.lua`, `rules/round.lua`; ADR-0031). A client is sent the values a
script decides and never receives the script (ADR-0019). The manifest's
`characters` list itself, in manifest order, goes into both packs as the
`Characters` entry, the table a character index resolves against (ADR-0042);
a manifest naming more than 255 characters fails the cook. The sounds folder
holds one mono PCM WAV file for each of the client's cues, named after it:
`gunshot.wav`, `hit_marker.wav`, `hit_taken.wav`, `death.wav`, `match_won.wav`
and `match_lost.wav`. Each goes into the **client** pack only, as an audio
asset addressed `<sounds path>/<cue>` (e.g. `sounds/test_map/gunshot`), and the
sounds path itself as the `Sounds` entry the client finds them by (ADR-0020,
ADR-0031). Each character's
`Character/Eye` prim, where its player's camera sits and its Shots leave from,
goes into both packs as that point alone (ADR-0040). It is an error if the scenario folder or its
`manifest.yaml` is missing, if the map or a named character doesn't resolve to
a stage, if a character has no `Character/Eye`, if the manifest names no
`sounds` folder, if that folder lacks a cue's sound (the error names the cue),
if a sound is not a mono PCM WAV (the error names the file), or if there is no
`parameters.lua`: the server reads its Parameters out of its pack at startup, so that is found here
rather than when a server starts on the pack.

By default, packs are written under `<assets-root>/packs`, keyed by the
scenario's name alone, not its `authoring/scenarios/` position (`augusta` ->
`packs/augusta/client.pack`, `packs/augusta/server.pack`).
Pass `--client-output-pack`/`--server-output-pack` to put them somewhere else.
Scripts are part of the signed pack: to change a value, edit the file and cook
again.

The cooker's geometry reader classifies `UsdGeomMesh`, `UsdGeomCube`, and
`UsdGeomCapsule` (ADR-0032/ADR-0041) - a character authored as any of the
three, like `composer/examples/authoring/characters/player/`, cooks into real
mesh/collision entries.

#### `augusta-pack` reference

```
augusta-pack [-h] [--assets-root ASSETS_ROOT]
         [--client-output-pack CLIENT_OUTPUT_PACK]
         [--server-output-pack SERVER_OUTPUT_PACK]
         [--signing-key SIGNING_KEY] [--skip-validation]
         scenario
```

| Argument | Default | Description |
|---|---|---|
| `scenario` (required) | | Scenario folder - relative to the current directory or absolute, but must be under `<assets-root>/authoring` (see above). |
| `-h`, `--help` | | Print the usage and option list, then exit. |
| `--assets-root ASSETS_ROOT` | the root of the venv the command runs from (`<assets-root>/python/...`) | Assets root holding `authoring/`, `packs/` and `keys/`: `scenario` must sit under its `authoring/`, and it's used for the three defaults below. |
| `--client-output-pack CLIENT_OUTPUT_PACK` | `<assets-root>/packs/<scenario's path under authoring>/client.pack` | Where to write the client pack. Missing parent directories are created. |
| `--server-output-pack SERVER_OUTPUT_PACK` | `<assets-root>/packs/<scenario's path under authoring>/server.pack` | Where to write the server pack. Missing parent directories are created. |
| `--signing-key SIGNING_KEY` | `<assets-root>/keys/augusta.key` | Ed25519 private key (64 bytes) the packs are signed with. |
| `--skip-validation` | off | Skip usd-validation-nvidia (step 2) for stages that fail its checks. usd-optimize and the cook still run. |

Exit status is `0` on success and `1` if any step fails.

### Signing keys

The bootstrap already generates `keys\augusta.key` / `augusta.pub`, so you only
need `augusta-keygen` to create an additional keypair:

```powershell
augusta-keygen <key-prefix>   # writes <key-prefix>.key and <key-prefix>.pub
augusta-pack <scenario> --signing-key <key-prefix>.key
```

#### `augusta-keygen` reference

```
augusta-keygen [-h] prefix
```

| Argument | Description |
|---|---|
| `prefix` (required) | Path prefix for the output files: writes `<prefix>.key` (64-byte private key) and `<prefix>.pub` (32-byte public key), raw and unframed. The directory must already exist. |
| `-h`, `--help` | Print the usage and option list, then exit. |

On success it prints the two paths it wrote and exits `0`. The public key is
what the runtime verifies packs against (ADR-0018).

It **overwrites** existing `<prefix>.key` / `<prefix>.pub` without asking. Never
point it at `keys\augusta`: that would invalidate every pack already signed with
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

```
augusta-inspect [-h] pack
```

| Argument | Description |
|---|---|
| `pack` (required) | Pack file (see above). |
| `-h`, `--help` | Print the usage and option list, then exit. |

The output follows the file's own layout (ADR-0031), each section with its
offset and size in bytes:

- **Header:** magic, format version, data offset, index offset and index count.
- **Data:** only its offset and size. The blobs themselves are not read.
- **Index:** one line per entry with its type (`mesh`, `texture`, `audio`,
  `collision`, `spawn-point`, `hitbox`, `scene`, `script`, `characters`,
  `client-pack`, `eye` or `sounds`), its offset and
  size within the pack, and its pack-relative path.
- **Trailer:** the stored BLAKE3 hash and Ed25519 signature, in hex.

Only the header, index and trailer are read, so it is fast on large packs. It
does **not** check the hash or signature (the trailer is labelled as unverified):
use `augusta-verify` before trusting the contents.

#### `augusta-verify` reference

```
augusta-verify [-h] [--assets-root ASSETS_ROOT] [--public-key PUBLIC_KEY] pack
```

| Argument | Default | Description |
|---|---|---|
| `pack` (required) | | Pack file (see above). |
| `-h`, `--help` | | Print the usage and option list, then exit. |
| `--assets-root ASSETS_ROOT` | the root of the venv the command runs from | Assets root, for the `--public-key` default only. |
| `--public-key PUBLIC_KEY` | `<assets-root>/keys/augusta.pub` | Ed25519 public key (32 bytes) the signature must verify against. |

It recomputes the BLAKE3 hash of everything but the trailer, compares it with
the trailer's hash, verifies the trailer's Ed25519 signature over that hash, and
only then checks that the header and index are well formed (ADR-0031). On
success it prints the entry count, size, hash and key used and exits `0`. On any
failure it prints the reason to stderr and exits `1`:

| Message | Meaning |
|---|---|
| `content hash mismatch` | The pack was corrupted or modified after it was signed. |
| `signature is not valid for this public key` | The pack was signed by a different key, or the signature was tampered with. |
| `bad magic`, `unsupported format version`, `too small`, ... | The file isn't a (supported) Augusta pack. |

### Layout

| Path | Role |
|---|---|
| `pack/src/pack/cli.py` | `augusta-pack` entry point |
| `pack/src/pack/scenario.py` | Scenario folder resolution (stage, `*.lua` scripts, cue sounds) |
| `pack/src/pack/sounds.py` | The cue catalogue and the mono PCM WAV reader |
| `pack/src/pack/optimize.py` | usd-optimize step |
| `pack/src/pack/validate.py` | usd-validation-nvidia step |
| `pack/src/pack/cook.py` | Stage walk and asset conversion |
| `pack/src/pack/pack.py`, `wire.py` | Pack wire format, hashing, signing |
| `pack/src/pack/keys.py` | `augusta-keygen` and key file I/O |
| `pack/src/pack/pack_cli.py` | `augusta-inspect` and `augusta-verify` entry points |
| `pack/src/pack/reader.py` | Pack container parsing and verification (the read side of `pack.py`) |
| `pack/src/pack/assets_root.py` | Assets-root inference shared by the entry points |
| `pack/cpp/` | Standalone CMake/vcpkg project for the two native modules. It builds straight into `pack/src/pack/`. |
| `composer/` | Composer bootstrap, playback file that scaffolds the app, and `augusta-composer.ps1` (launches it) |
| `pack/tests/` | pytest suite and the USD fixtures it cooks (see Running the tests) |
| `composer/examples/authoring/` | A committed `<assets-root>/authoring/` sample the pack bootstrap seeds into a fresh assets root: `maps/augusta/` (ADR-0015), `characters/player/` (ADR-0040), `sounds/augusta/` (placeholder cue sounds, ADR-0020), `scenarios/augusta/` composing them (ADR-0041) |
| `pack/scripts/bootstrap-windows.ps1` | Builds the pack assets root |

### Rebuilding the native modules

The bootstrap builds them once and skips the step if the `.pyd` files exist. To
rebuild after changing `cpp/`, delete them from `src/pack/` and re-run
the bootstrap, or build directly:

```powershell
cmake --preset windows -S tools\pack\cpp "-DPYTHON_EXECUTABLE=<assets-root>\python\pack\Scripts\python.exe"
cmake --build tools\pack\cpp\build\x64-windows
```

`PYTHON_EXECUTABLE` must point at the venv so the extension's ABI matches the
interpreter that imports it.

### Running the tests

The tests live in [pack/tests/](pack/tests/) and run with pytest in a separate
environment `uv` creates at `tools/pack/.venv`, not the assets root's venv.
They need the native modules built into `src/pack/` (above) against that
environment's interpreter:

```powershell
cd tools\pack
uv sync
cmake --preset windows -S cpp "-DPYTHON_EXECUTABLE=$PWD\.venv\Scripts\python.exe"
cmake --build cpp\build\x64-windows
uv run pytest
```

The USD stages the cook tests read, including the malformed ones, are in
[pack/tests/fixtures/](pack/tests/fixtures/); the end-to-end test cooks
[composer/examples/authoring/](composer/examples/authoring/) with a throwaway key.

#### Golden packs

`../tests/fixtures/example-packs/` at the repository root holds the example
scenario's client and server packs, cooked with the test key next to them
(never the release key). The C++ runtime's tests load them, and
`pack/tests/test_golden.py` requires the cooker to still write them byte for byte:
that is the contract between the two implementations of the pack format
(ADR-0013). After a deliberate change to the format or to the example,
regenerate them from `tools/pack` and commit the result:

```powershell
$golden = "..\..\tests\fixtures\example-packs"
uv run augusta-pack augusta --assets-root examples --signing-key $golden\test.key `
  --client-output-pack $golden\client.pack --server-output-pack $golden\server.pack
```

## Loadtest

`augusta-loadtest` plays the scenario's Player count of Scripted players (see
[CONTEXT.md](../CONTEXT.md)) against a running `augustad` until every one has
seen a number of Match ends, then exits 0; it exits 1 as soon as one fails or a
timeout passes (ADR-0013). Each player is a client with no window or GPU: it
predicts and sends its Commands like `augustac`, but decides them itself from
what the server tells it, from a seed. Unlike the other tools it is C++ built
with the engine, on Windows and Linux, but only when asked for, and never in
CI:

```powershell
cmake --preset windows -DAUGUSTA_LOADTEST=ON
cmake --build --preset windows --target augusta-loadtest
# build/x64-windows/tools/loadtest/augusta-loadtest.exe
```

It reads `augusta-loadtest.yaml` next to the executable, or the file
`--config` names (ADR-0034); copy
[`loadtest/augusta-loadtest.example.yaml`](loadtest/augusta-loadtest.example.yaml),
which documents every key. Its pack must be the client pack cooked with the
server's, and its scenario's Player count 2 or more: a Match of one ends only
when its player dies, which nothing in it can cause. The example scenario's is
1, so a single player can run it alone.

Its tests are in [`loadtest/tests/`](loadtest/tests/). With the option on they
join `augusta_tests`, and `ctest` runs them with the engine's.
