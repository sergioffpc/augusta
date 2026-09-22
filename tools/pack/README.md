# pack

The offline asset cooker (ADR-0030): it turns a raw authored OpenUSD stage into
the signed client and server packs the game loads (ADR-0018, ADR-0019). It is a
tooling-time project only - nothing here is linked into the shipped client or
server.

```
<scenario>/map.usda -> usd-optimize -> usd-validation-nvidia -> cook -> <scenario>.client.pack
<scenario>/*.lua                                                          -> <scenario>.server.pack
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
it never overwrites the example scenario below once one exists at its path.

The script also seeds `authoring/examples/augusta` from
[examples/augusta/](examples/augusta/) - a small worked scenario (a floor, a
prop, a spawn point, its `parameters.lua`, and placeholder `objectives.lua`/
`behaviours.lua` for when game policy, ADR-0022, is wired up) committed to this
repo so a fresh environment has something to cook straight away:

```powershell
augustap examples\augusta
```

The assets root looks like this:

| Path | Contents |
|---|---|
| `authoring/` | Scenario folders (a USD stage and its Lua scripts each), the cooker's input root |
| `packs/` | Cooked, signed packs |
| `keys/` | `augusta.key` / `augusta.pub` (Ed25519). Never commit these. |
| `bin/` | `augustap.exe`, `augustap-keygen.exe`, `augustap-inspect.exe`, `augustap-verify.exe` (installed here by `uv tool install`) |
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
```

To type just `augustap`, add the directory to `PATH` for the current session:

```powershell
$env:Path = "<assets-root>\bin;$env:Path"
augustap --help
```

The examples below assume `bin` is on `PATH`.

## Cooking a scenario

A scenario is a folder under `<assets-root>/authoring` holding one USD stage,
always named `map`, and the Lua scripts that go with it (ADR-0015, ADR-0039):

```
authoring\test_map\map.usda           # the stage (.usd, .usda, .usdc or .usdz)
authoring\test_map\parameters.lua     # required: the scenario's Parameters
authoring\test_map\rules\round.lua    # any other *.lua, in any subfolder
```

```powershell
augustap <scenario>                    # authoring\<scenario>\ -> packs\<scenario>.{client,server}.pack
augustap <dir>\<scenario>              # authoring\<dir>\<scenario>\ -> packs\<dir>\<scenario>.*.pack
augustap <scenario> --skip-validation  # skip usd-validation-nvidia only
```

A successful run ends with the paths of the client and server packs it wrote.

The cooker packs everything under the folder: the stage into both packs, and
every `*.lua` file into the **server** pack only, as a script asset addressed by
its path relative to the folder (`parameters.lua`, `rules/round.lua`; ADR-0031).
A client is sent the values a script decides and never receives the script
(ADR-0019). It is an error if the folder is missing, if the stage
`<scenario>/map.*` is missing or ambiguous, or if there is no `parameters.lua`:
the server reads its Parameters out of its pack at startup, so that is found
here rather than when a server starts on the pack. Absolute paths and `..` are
rejected. [`examples/augusta/`](examples/augusta/) is a full worked scenario to
copy from, seeded into a fresh assets root by the bootstrap (see Setup above).

Packs are written under `<assets-root>/packs` at the scenario's own relative
location, as `<scenario>.client.pack` and `<scenario>.server.pack`. Scripts are
part of the signed pack: to change a value, edit the file and cook again.

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
| `scenario` (required) | | Scenario folder, relative to `<assets-root>/authoring`: its stage and its `*.lua` scripts (see above). |
| `-h`, `--help` | | Print the usage and option list, then exit. |
| `--assets-root ASSETS_ROOT` | the root of the venv the command runs from (`<assets-root>/python/...`) | Assets root holding `authoring/`, `packs/` and `keys/`. |
| `--client-output-pack CLIENT_OUTPUT_PACK` | `<assets-root>/packs/<scenario>.client.pack` | Where to write the client pack. Missing parent directories are created. |
| `--server-output-pack SERVER_OUTPUT_PACK` | `<assets-root>/packs/<scenario>.server.pack` | Where to write the server pack. Missing parent directories are created. |
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

`<pack>` is an absolute path, or relative to `<assets-root>/packs`. The `.pack`
extension is optional, so `<stage>.client` finds `<stage>.client.pack`.

### `augustap-inspect` reference

```
augustap-inspect [-h] [--assets-root ASSETS_ROOT] pack
```

| Argument | Default | Description |
|---|---|---|
| `pack` (required) | | Pack file (see above). |
| `-h`, `--help` | | Print the usage and option list, then exit. |
| `--assets-root ASSETS_ROOT` | the root of the venv the command runs from | Assets root whose `packs/` a relative `pack` is resolved against. |

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
| `--assets-root ASSETS_ROOT` | the root of the venv the command runs from | Assets root whose `packs/` and `keys/` are used for the defaults. |
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
| `composer/` | Playback file that scaffolds the Augusta USD Composer app |
| `examples/augusta/` | The example scenario the bootstrap seeds into a fresh assets root |
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
