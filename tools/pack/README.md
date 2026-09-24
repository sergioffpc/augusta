# pack

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
[cpp/](cpp/): `_meshoptimizer` (ADR-0016) and `_textconv` (ADR-0017). They have
no USD dependency on purpose; see ADR-0030 for why.

## Setup

Everything lives under one hermetic **assets root** (a uv-managed Python venv,
the native modules, the signing key and the content directories). Nothing is
installed globally. From an elevated PowerShell, after the repository's own
`scripts\bootstrap-windows.ps1`:

```powershell
.\tools\pack\scripts\bootstrap-windows.ps1 <assets-root>
```

Pass `-SkipAuthoring` on a machine that only cooks stages someone else authored;
otherwise NVIDIA Omniverse USD Composer and Adobe's USD-Fileformat-plugins are
set up as well. The script is safe to re-run. It never regenerates an existing
signing key, since that would invalidate every pack already signed with it, and
it never overwrites a seeded piece below once it exists at its path.

The script also seeds a small worked example from
[examples/authoring/](examples/authoring/), piece by piece: `authoring/maps/augusta`
(a floor, a prop, a spawn point), `authoring/characters/player` (ADR-0040), and
`authoring/scenarios/augusta` (its `manifest.yaml`, ADR-0041, composing
the other two, plus `parameters.lua` and placeholder `objectives.lua`/
`behaviours.lua` for when game policy, ADR-0022, is wired up) - committed to
this repo so a fresh environment has something to cook straight away:

```powershell
augustap augusta
```

`augustap` takes the scenario's bare name (ADR-0041), always resolved as
`<assets-root>\authoring\scenarios\<name>` - never a path, and never
resolved from the current directory.

The assets root looks like this:

| Path | Contents |
|---|---|
| `authoring/` | `maps/<name>/` (ADR-0015), `characters/<name>/` (ADR-0040), `scenarios/<name>/` (`manifest.yaml` + Lua scripts, ADR-0041) - the only one of the three `augustap` resolves a name against |
| `packs/` | Cooked, signed packs |
| `keys/` | `augusta.key` / `augusta.pub` (Ed25519). Never commit these. |
| `bin/` | `augustap.exe`, `augustap-keygen.exe`, `augustap-inspect.exe`, `augustap-verify.exe` (installed here by `uv tool install`), plus `augustap-composer.ps1` unless `-SkipAuthoring` |
| `python/` | The uv tool venv (`python/pack`), with this project installed editable |
| `tools/` | Composer and the Adobe plugins (unless `-SkipAuthoring`) |

## Running the commands

All the commands are self-contained launchers in `<assets-root>/bin`, so they run
from any directory and any shell, by full path:

```powershell
<assets-root>\bin\augustap.exe --help
<assets-root>\bin\augustap-keygen.exe --help
<assets-root>\bin\augustap-inspect.exe --help
<assets-root>\bin\augustap-verify.exe --help
<assets-root>\bin\augustap-composer.ps1
```

To type just `augustap`, add the directory to `PATH` for the current session:

```powershell
$env:Path = "<assets-root>\bin;$env:Path"
augustap --help
```

The examples below assume `bin` is on `PATH`.

## Launching Composer

```powershell
augustap-composer.ps1
```

A PowerShell script rather than an `.exe`, so its extension has to be typed
(PowerShell doesn't resolve a bare name to a `.ps1` on `PATH` the way it does
for `.exe`). Only installed when the bootstrap ran without `-SkipAuthoring`. A
thin wrapper (`composer/augustap-composer.ps1`, committed here and copied into
`bin/`) around kit-app-template's own `repo.bat launch`, run from
`<assets-root>/tools/kit-app-template` (it locates that directory relative to
its own path via `$PSScriptRoot`, so it works wherever the assets root lives) -
the same app the bootstrap scaffolds and builds
(`composer/augusta.playback.toml`). Arguments are forwarded as-is, e.g.
`augustap-composer.ps1 --name augusta.kit` if
`repo.bat launch` asks which app when more than one is registered.

## Cooking a scenario

A scenario is named, not pathed (ADR-0041): `augustap` resolves the bare name
you give it to `<assets-root>\authoring\scenarios\<name>`, which holds a
`manifest.yaml` naming the one map and every character that scenario
composes, plus the Lua scripts that go with it:

```
scenarios\test_map\manifest.yaml     # map: maps/test_map, characters: [...]
scenarios\test_map\parameters.lua    # required: the scenario's Parameters
scenarios\test_map\rules\round.lua   # any other *.lua, in any subfolder
maps\test_map\map.usda               # the stage (.usd, .usda, .usdc or .usdz)
characters\marine\character.usda     # a character the manifest can name (ADR-0040)
```

```yaml
# scenarios\test_map\manifest.yaml
map: maps/test_map
characters:
  - characters/marine
```

```powershell
augustap <name>                    # -> <assets-root>\packs\<name>\{client,server}.pack
augustap <name> --skip-validation  # skip usd-validation-nvidia only
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
a manifest naming more than 255 characters fails the cook. Each character's
`Character/Eye` prim, where its player's camera sits, goes into the client pack
as that point alone (ADR-0040). It is an error if the scenario folder or its
`manifest.yaml` is missing, if the map or a named character doesn't resolve to
a stage, if a character has no `Character/Eye`, or if there is no
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
three, like `examples/authoring/characters/player/`, cooks into real
mesh/collision entries.

### `augustap` reference

```
augustap [-h] [--assets-root ASSETS_ROOT]
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

## Signing keys

The bootstrap already generates `keys\augusta.key` / `augusta.pub`, so you only
need `augustap-keygen` to create an additional keypair:

```powershell
augustap-keygen <key-prefix>   # writes <key-prefix>.key and <key-prefix>.pub
augustap <scenario> --signing-key <key-prefix>.key
```

### `augustap-keygen` reference

```
augustap-keygen [-h] prefix
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

## Inspecting and verifying packs

```powershell
augustap-inspect <pack>   # what does the pack contain?
augustap-verify <pack>    # is it intact, and signed by the key I expect?
```

`<pack>` is an ordinary path - relative to the current directory or absolute,
never resolved against an assets root. The `.pack` extension is optional, so
`<stage>.client` finds `<stage>.client.pack`.

### `augustap-inspect` reference

```
augustap-inspect [-h] pack
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
  `collision`, `spawn-point`, `hitbox`, `scene` or `script`), its offset and
  size within the pack, and its pack-relative path.
- **Trailer:** the stored BLAKE3 hash and Ed25519 signature, in hex.

Only the header, index and trailer are read, so it is fast on large packs. It
does **not** check the hash or signature (the trailer is labelled as unverified):
use `augustap-verify` before trusting the contents.

### `augustap-verify` reference

```
augustap-verify [-h] [--assets-root ASSETS_ROOT] [--public-key PUBLIC_KEY] pack
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

## Layout

| Path | Role |
|---|---|
| `src/pack/cli.py` | `augustap` entry point |
| `src/pack/scenario.py` | Scenario folder resolution (stage, `*.lua` scripts) |
| `src/pack/optimize.py` | usd-optimize step |
| `src/pack/validate.py` | usd-validation-nvidia step |
| `src/pack/cook.py` | Stage walk and asset conversion |
| `src/pack/pack.py`, `wire.py` | Pack wire format, hashing, signing |
| `src/pack/keys.py` | `augustap-keygen` and key file I/O |
| `src/pack/pack_cli.py` | `augustap-inspect` and `augustap-verify` entry points |
| `src/pack/reader.py` | Pack container parsing and verification (the read side of `pack.py`) |
| `src/pack/assets_root.py` | Assets-root inference shared by the entry points |
| `cpp/` | Standalone CMake/vcpkg project for the two native modules. It builds straight into `src/pack/`. |
| `composer/` | Playback file that scaffolds the Augusta USD Composer app, and `augustap-composer.ps1` (launches it) |
| `examples/authoring/` | A committed `<assets-root>/authoring/` sample the bootstrap seeds into a fresh assets root: `maps/augusta/` (ADR-0015), `characters/player/` (ADR-0040), `scenarios/augusta/` composing both (ADR-0041) |
| `scripts/bootstrap-windows.ps1` | Builds the assets root |

## Rebuilding the native modules

The bootstrap builds them once and skips the step if the `.pyd` files exist. To
rebuild after changing `cpp/`, delete them from `src/pack/` and re-run
the bootstrap, or build directly:

```powershell
cmake --preset windows -S tools\pack\cpp "-DPYTHON_EXECUTABLE=<assets-root>\python\pack\Scripts\python.exe"
cmake --build tools\pack\cpp\build\x64-windows
```

`PYTHON_EXECUTABLE` must point at the venv so the extension's ABI matches the
interpreter that imports it.
