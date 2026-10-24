# Tools

This directory contains every tool, apart from the runtime in `src/`: `pack`
cooks authored content into signed runtime packs, `composer` sets up the
optional USD authoring application and its launcher (both Windows), and `agent`
is the `augusta_agent` package a Python script controls Agents with, for load
and end-to-end tests among other things, and `wireshark` is the dissector that
reads a Development build's pcapng of messages. Each tool's tests live beside
it. `docs` is not a tool of its own: it holds how the documentation site is
built (ADR-0046).

| Tool                       | Purpose                                                                    |
| -------------------------- | -------------------------------------------------------------------------- |
| [`pack/`](pack/)           | Python asset cooker, signing utilities, and native cooking modules.        |
| [`composer/`](composer/)   | Optional NVIDIA USD Composer setup, playback definition, and launcher.     |
| [`agent/`](agent/)         | `augusta_agent`: Agents, players a Python script controls through Intents. |
| [`wireshark/`](wireshark/) | The `augusta` Lua dissector, its install script and its `tshark` test.     |
| [`docs/`](docs/)           | The documentation site's MkDocs hooks and Doxyfile, built by `make docs`.  |

## Python environment

The Python tools share one environment: [`pyproject.toml`](pyproject.toml) here
is a uv workspace with `pack`, `agent` and `wireshark` (tests only, no package)
as its members and one lock, `uv.lock`. `uv sync`, run from this directory,
creates `tools/.venv` with the cooker and `augusta_agent` installed editable,
pytest, and MkDocs (the `docs` group, which `make docs` runs alone). The editor
uses it as the workspace's interpreter. Run `uv sync` here, not in a member,
where it would sync that member alone and drop the rest.

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
scenarios/<scenario>.yaml -> the map's and characters' stages -> usd-optimize -> usd-validation-nvidia -> cook -> packs/<scenario>/client.apak
                          -> the cue sounds and the scripts                                             -> packs/<scenario>/server.apak
```

1. **usd-optimize** cleans the stage (triangulate, dedupe, flatten, drop small
   geometry). ADR-0015.
2. **usd-validation-nvidia** validates the cleaned stage. Any issue, warnings
   included, aborts the cook and no pack is written. ADR-0015.
3. **cook** walks the stage with `pxr`, converts meshes, colliders, hitboxes,
   skeletons, Clips, spawn points and textures, then packs, BLAKE3-hashes and
   Ed25519-signs the result. ADR-0031, ADR-0032, ADR-0061, ADR-0066.

The pipeline is Python over one pybind11 module in [pack/native/](pack/native/),
`_native`: the mesh pass (ADR-0016), the texture compression (ADR-0017),
ozz-animation's archives of a Character's Skeleton and Clips (ADR-0066) and
`augusta_assets`, which the module links to encode every blob and write, hash
and sign the packs, so the wire format the client and server read has no Python
copy. The repository's root CMake project builds it, as it builds the Agents'
module, with `AUGUSTA_TOOLS` and the tools environment's Python. It has no USD
dependency on purpose; see ADR-0030 for why.

The same build generates `augusta_pack.augusta_pb2` beside it with vcpkg's
`protoc`, from the protocol's one schema,
[augusta.proto](../src/modules/protocol/proto/augusta.proto) (ADR-0007), so a
Match capture reads through the `protobuf` runtime with no hand-written copy of
its format. Neither module is checked in.

### Setup

The pack bootstrap creates the cooker's **assets root** (a uv-managed Python
environment, native module, a signing key and content directories). The cooker
runs on Windows and Linux, the platforms vcpkg's `ktx` port builds for, and
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
`authoring/characters/soldier.usda` (ADR-0040), `authoring/animations/` (its
Clips, a file each, ADR-0066), `authoring/rifles/rifle.usda` (its Grip, Muzzle
and Sight, ADR-0064), `authoring/sounds/` (placeholder cue sounds, ADR-0020),
`authoring/scripts/parameters/rules_of_engagement.lua` (ADR-0039),
`authoring/scripts/rules/last_man_standing.lua` (ADR-0022), and
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
| `keys/`      | One Ed25519 keypair per environment: `develop.key` / `develop.pub`, and `staging.key` / `staging.pub`, the release key. Never commit these.                                                                 |
| `bin/`       | `augusta-pack`, `augusta-keygen`, `augusta-inspect`, `augusta-verify`, `augusta-publish` (`.exe` on Windows; installed here by `uv tool install`), plus `augusta-composer.sh` if the Composer bootstrap ran |
| `python/`    | The uv tool venv (`python/augusta-pack`), with this project installed editable                                                                                                                              |
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
  gunshot:                                     # one of its variants each time
    variants: [sounds/gunshot.wav, sounds/gunshot_2.wav]
    gain: -3                                   # decibels; 0 if left out
  hit_marker: sounds/hit_marker.wav            # one variant at 0 dB
  hit_taken: sounds/hit_taken.wav
  death: sounds/death.wav
  match_won: sounds/match_won.wav
  match_lost: sounds/match_lost.wav
scripts:
  parameters: scripts/parameters/default.lua   # required
  rules: scripts/rules/last_man_standing.lua    # optional
```

```powershell
augusta-pack <name>                    # -> <assets-root>\packs\<name>\{client,server}.apak
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
only: its gain and its variants' paths at `sounds/<cue>` (e.g.
`sounds/gunshot`), and each variant as an audio asset at `sounds/<cue>/<n>`
(ADR-0020, ADR-0031, ADR-0041). A cue's sound is one WAV file, shorthand for one
variant at 0 dB, or a mapping of its `variants` and an optional `gain` in
decibels; one file may be a variant of several cues. Each character's
`Character/Eye` prim, where its player's camera sits and its Shots leave from,
goes into both packs as that point alone (ADR-0040). It is an error if the
manifest is missing, if it holds a key, cue or script role the cooker does not
know, if a file it names is missing or is not what its key needs (the map and
characters a USD stage, a sound a mono PCM WAV - the error names the file), if a
character's name is not one lowercase word or is a root prim of the map, if a
character has no `Character/Eye`, if a cue has no sound, no variant, a missing
WAV or a gain that is not a finite number (the error names the cue), or if there
is no `parameters` script: the server reads its Parameters out of its pack at
startup, so that is found here rather than when a server starts on the pack.

By default, packs are written under `<assets-root>/packs`, keyed by the
scenario's name alone, not its `authoring/scenarios/` position (`firebase` ->
`packs/firebase/client.apak`, `packs/firebase/server.apak`). Pass
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
| `--client-output-pack CLIENT_OUTPUT_PACK` | `<assets-root>/packs/<scenario's path under authoring>/client.apak`     | Where to write the client pack. Missing parent directories are created.                                                                         |
| `--server-output-pack SERVER_OUTPUT_PACK` | `<assets-root>/packs/<scenario's path under authoring>/server.apak`     | Where to write the server pack. Missing parent directories are created.                                                                         |
| `--signing-key SIGNING_KEY`               | `<assets-root>/keys/develop.key`                                        | Ed25519 private key (64 bytes) the packs are signed with: `develop`'s by default, `keys/staging.key` for a release.                             |
| `--skip-validation`                       | off                                                                     | Skip usd-validation-nvidia (step 2) for stages that fail its checks. usd-optimize and the cook still run.                                       |

Exit status is `0` on success and `1` if any step fails.

### Signing keys

Each environment signs its packs with its own key, published beside them, and
its servers refuse a pack that does not verify against it. The bootstrap
generates `develop`'s, `keys\develop.key` / `develop.pub`, the default of
`augusta-pack` and `augusta-verify`. `staging`'s is the release key,
`keys\staging.key` / `staging.pub`, generated offline with `augusta-keygen`
([ENGINEERING.md, Release signing](../docs/ENGINEERING.md#cicd)). A cook for
both is two runs, one per key:

```powershell
augusta-keygen <key-prefix>   # writes <key-prefix>.key and <key-prefix>.pub
augusta-pack <scenario> --signing-key <assets-root>\keys\staging.key
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
point it at `keys\develop` or `keys\staging`: that would invalidate every pack
already signed with the current key and the public key already deployed for
verification.

### Inspecting and verifying packs

```powershell
augusta-inspect <pack>   # what does the pack contain?
augusta-verify <pack>    # is it intact, and signed by the key I expect?
```

`<pack>` is an ordinary path - relative to the current directory or absolute,
never resolved against an assets root. The `.apak` extension is optional, so
`<stage>.client` finds `<stage>.client.apak`.

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

Given a Match capture (ADR-0050) instead, which it tells from a pack by the
file's first bytes, it prints the capture's header (format version, engine
version, server and client pack hashes, tick rate, when the Match started in
UTC), how long the Match lasted in ticks and seconds, one line per player (its
number, which `augustac --reenact --player` takes, Session, Character, spawn,
how many Commands it sent, and when it died and who killed it or when it left),
then the Match end's winner or a Draw, and whether the file ends partway through
a record. It reads each record through `augusta_pack.augusta_pb2`, generated
from the protocol's one schema, and reports a Command only by count.

It reads the pack through `augusta_assets`, with the same header and index
parser the client and server load packs with, and refuses a malformed one with
the same reason (see the table under `augusta-verify`). Only the header, index
and trailer are read, so it is fast on large packs. It does **not** check the
hash or signature (the trailer is labelled as unverified): use `augusta-verify`
before trusting the contents.

#### `augusta-verify` reference

```text
augusta-verify [-h] [--assets-root ASSETS_ROOT] [--public-key PUBLIC_KEY] pack
```

| Argument                    | Default                                    | Description                                                      |
| --------------------------- | ------------------------------------------ | ---------------------------------------------------------------- |
| `pack` (required)           |                                            | Pack file (see above).                                           |
| `-h`, `--help`              |                                            | Print the usage and option list, then exit.                      |
| `--assets-root ASSETS_ROOT` | the root of the venv the command runs from | Assets root, for the `--public-key` default only.                |
| `--public-key PUBLIC_KEY`   | `<assets-root>/keys/develop.pub`           | Ed25519 public key (32 bytes) the signature must verify against. |

It loads the pack as the client and server do, through `augusta_assets`, so a
pack it accepts is one they load: it recomputes the BLAKE3 hash of everything
but the trailer, compares it with the trailer's hash, verifies the trailer's
Ed25519 signature over that hash, and checks that the header and index are well
formed (ADR-0031). On success it prints the entry count, size, hash and key used
and exits `0`. On any failure it prints the pack's path and the reason to stderr
and exits `1`:

| Message                                                  | Meaning                                                                     |
| -------------------------------------------------------- | --------------------------------------------------------------------------- |
| `failed its integrity check (content hash mismatch)`     | The pack was corrupted or modified after it was signed.                     |
| `failed signature verification`                          | The pack was signed by a different key, or the signature was tampered with. |
| `is not an augusta pack file (bad magic)`                | The file isn't an Augusta pack.                                             |
| `is pack format N; this build reads format M`            | The pack is of format version `N`; this build reads `M` alone.              |
| `is truncated, or exceeds this build's pack size limits` | The file is shorter than its header and index say, or too large.            |
| `has a corrupt index`                                    | An index entry has an unknown type, or two entries share a path.            |

### Publishing a server pack

```powershell
augusta-publish <scenario> --environment <develop|staging> --host <node>
```

Puts a scenario's server pack on the k3s node's shared asset-pack volume, in the
tree of the environment that serves it, where its servers read it (ADR-0026). It
publishes; it does not deploy: a server serves the pack once its environment's
`HelmRelease` in `clusters/onprem/apps/` names the version it prints
([Rotate the Pack Signing Key](../docs/runbooks/pack-key-rotation.md), step 7).

Before copying anything it verifies both packs of the scenario's cook against
the public key, and checks that the server pack names that client pack in its
header (ADR-0031). It then copies only the server pack and the public key, as
`server.apak` and `signing.pub`, into
`/srv/augusta/asset-packs/<environment>/<scenario>/<version>/`, `<version>`
being the first 12 hex characters of the server pack's BLAKE3 hash, which does
not cover the signature: one cook signed with `develop`'s key and with
`staging`'s is one version in two folders. The node takes `develop` and
`staging` alone. The folder is in the `augusta` group so only the servers' group
reads it. The folder is assembled beside its final place and renamed into it, so
it never exists half-written, and a folder that exists is never written again:
publishing the same cook twice checks the node holds the same files and copies
nothing, and a folder holding other files is an error. Once written, the folder
is read-only, to the publish user too.

It runs `ssh` from `PATH` (on Windows, the OpenSSH client), as the node's
`augusta-publish` user, with no `sudo`. That user's key runs nothing but the
node's end of publishing
([`augusta-publish-receive.sh`](../clusters/onprem/node/augusta-publish-receive.sh),
ADR-0057); setting it up is
[The publish user](../docs/runbooks/k3s-node-recovery.md#the-publish-user).

#### `augusta-publish` reference

```text
augusta-publish [-h] --environment {develop,staging} --host HOST
                [--assets-root ASSETS_ROOT] [--client-pack CLIENT_PACK]
                [--server-pack SERVER_PACK] [--public-key PUBLIC_KEY] scenario
```

| Argument                                     | Default                                      | Description                                                                                                                     |
| -------------------------------------------- | -------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------- |
| `scenario` (required)                        |                                              | Scenario name (ADR-0041), which also names its server in the cluster: lowercase letters, digits and `-`, at most 46 characters. |
| `--environment {develop,staging}` (required) |                                              | The environment that serves the pack: its tree on the node, and the key its packs are signed with.                              |
| `--host HOST` (required)                     |                                              | The k3s node as its `augusta-publish` user, as `ssh` names it (`augusta-publish@<node>`, or an alias in `~/.ssh/config`).       |
| `-h`, `--help`                               |                                              | Print the usage and option list, then exit.                                                                                     |
| `--assets-root ASSETS_ROOT`                  | the root of the venv the command runs from   | Assets root, for the defaults below only.                                                                                       |
| `--client-pack CLIENT_PACK`                  | `<assets-root>/packs/<scenario>/client.apak` | The client pack of the cook. Verified, never copied.                                                                            |
| `--server-pack SERVER_PACK`                  | `<assets-root>/packs/<scenario>/server.apak` | The server pack to publish.                                                                                                     |
| `--public-key PUBLIC_KEY`                    | `<assets-root>/keys/<environment>.pub`       | The key both packs are signed with, published as `signing.pub`.                                                                 |

On success it prints the folder on the node and the line to put under the
scenario's server in the `HelmRelease`
(`servers.<scenario>.packVersion: "<version>"`), with the pack format version
the server pack was cooked in to put beside it (`packFormat: <format>`), and
exits `0`, whether it copied the pack or found it there already. On any failure
it prints the reason to stderr and exits `1`.

### Layout

| Path                                   | Role                                                                                                                                                                                                                                                                                                                                                                                                  |
| -------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `pack/src/augusta_pack/cli.py`         | `augusta-pack` entry point                                                                                                                                                                                                                                                                                                                                                                            |
| `pack/src/augusta_pack/scenario.py`    | Scenario folder resolution (stage, `*.lua` scripts, cue sounds)                                                                                                                                                                                                                                                                                                                                       |
| `pack/src/augusta_pack/sounds.py`      | The cue catalogue and the mono PCM WAV reader                                                                                                                                                                                                                                                                                                                                                         |
| `pack/src/augusta_pack/optimize.py`    | usd-optimize step                                                                                                                                                                                                                                                                                                                                                                                     |
| `pack/src/augusta_pack/validate.py`    | usd-validation-nvidia step                                                                                                                                                                                                                                                                                                                                                                            |
| `pack/src/augusta_pack/cook.py`        | Stage walk and asset conversion                                                                                                                                                                                                                                                                                                                                                                       |
| `pack/src/augusta_pack/keys.py`        | `augusta-keygen` and key file I/O                                                                                                                                                                                                                                                                                                                                                                     |
| `pack/src/augusta_pack/pack_cli.py`    | `augusta-inspect` and `augusta-verify` entry points                                                                                                                                                                                                                                                                                                                                                   |
| `pack/src/augusta_pack/publish.py`     | `augusta-publish` entry point                                                                                                                                                                                                                                                                                                                                                                         |
| `pack/src/augusta_pack/capture.py`     | Match capture reading for `augusta-inspect` (the read side of `src/modules/capture`)                                                                                                                                                                                                                                                                                                                  |
| `pack/src/augusta_pack/assets_root.py` | Assets-root inference shared by the entry points                                                                                                                                                                                                                                                                                                                                                      |
| `pack/native/`                         | The native module's sources (the mesh pass, the texture compression and the binding of `augusta_assets`' pack encoder and reader), built by the root CMake project straight into `pack/src/augusta_pack/`.                                                                                                                                                                                            |
| `composer/`                            | Composer bootstrap, playback file that scaffolds the app, and `augusta-composer.sh` (launches it)                                                                                                                                                                                                                                                                                                     |
| `pack/tests/`                          | pytest suite and the USD fixtures it cooks (see Running the tests)                                                                                                                                                                                                                                                                                                                                    |
| `composer/examples/authoring/`         | A committed `<assets-root>/authoring/` sample the pack bootstrap seeds into a fresh assets root: `maps/firebase.usda` (ADR-0015), `characters/soldier.usda` (ADR-0040), `animations/` (its Clips, ADR-0066), `rifles/rifle.usda` (ADR-0064), `sounds/` (placeholder cue sounds, ADR-0020), `scripts/` (Parameters and rules, ADR-0039, ADR-0022), `scenarios/firebase.yaml` composing them (ADR-0041) |
| `pack/scripts/bootstrap.sh`            | Builds the pack assets root                                                                                                                                                                                                                                                                                                                                                                           |

### Rebuilding the native module

The bootstrap builds it and the protocol's generated `augusta_pb2.py` on every
run, which is a no-op when both are up to date. It fails, naming the file, when
the build leaves either one out. To rebuild after changing `native/` or the
protocol's schema, re-run the bootstrap, or build it directly from the
repository's root, as the bootstrap does:

```bash
make pack-native PYTHON_EXECUTABLE=<assets-root>/python/augusta-pack/bin/python
```

On Windows the interpreter is
`<assets-root>/python/augusta-pack/Scripts/python.exe`. `make pack-native`
configures the platform's preset without the client, in a build directory of its
own (`build/x64-<preset>-pack`), and builds the module's target alone.
`PYTHON_EXECUTABLE` must point at the venv so the extension's ABI matches the
interpreter that imports it; without it, the module is built against
`tools/.venv`'s.

### Running the tests

The tests live in [pack/tests/](pack/tests/) and run with pytest in the tools'
shared environment, `tools/.venv` (see Python environment above), not the assets
root's venv. They need the native module built into `src/augusta_pack/` against
that environment's interpreter, which the root build does once `uv sync` has
made it (`make build` and `make test` build it with the rest, `make pack-native`
alone):

```bash
(cd tools && uv sync)
make build
(cd tools && uv run pytest pack)
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
byte: that pins the cook end to end, from the USD stage walk to the signed pack
(ADR-0013). After a deliberate change to the format or to the example,
regenerate them from `tools/pack` and commit the result:

```powershell
$golden = "..\..\tests\fixtures\example-packs"
uv run augusta-pack firebase --assets-root ..\composer\examples --signing-key $golden\test.key `
  --client-output-pack $golden\client.apak --server-output-pack $golden\server.apak
```

## Agent

`augusta_agent` (ADR-0052) is a Python package a script imports to connect
Agents (see [GLOSSARY.md](../GLOSSARY.md)) to a server and tell each what to do;
the Harness carries it out every tick. A script runs its Agents in an asyncio
loop: it awaits each Agent's events (`await agent.next_event()`: Shot, Hit
confirmation, Death, Match start, Match end) and how each Intent it set ended,
and never a tick, so a script that takes seconds to decide still has its Agents
send a Command every tick. An Agent that is refused, cannot reach the server or
loses its connection fails every pending await with `AgentFailed`, carrying the
sentence `augustac` would show, and sets `agent.failure`; there is no
reconnecting. Each Agent reports Ready for every Roster unless made with
`auto_ready=False`, when `agent.ready()` reports it.

### Building the Agent module

The package's native part, `augusta_agent._native`, is C++ built with the
engine, on Windows and Linux, against the interpreter of `tools/.venv`, into
[`agent/src/augusta_agent/`](agent/src/augusta_agent/). It wraps the Agent's
Runtime, the agent module in [`agent/native/`](agent/native/), which the C++
tests link too. The CMake option `AUGUSTA_TOOLS`, on by default, builds the C++
tools all together or none (the server image turns it off). Run `uv sync` here
first, or the configure leaves the module out with a warning;
`-DPYTHON_EXECUTABLE=<python>` names another Python 3.12:

```sh
uv sync
cmake --preset linux
cmake --build --preset linux
```

On Windows the preset is `windows`.

### Running the Agent examples

The examples run from this directory, against a running `augustad`, with the
client pack cooked with the server's and the key it is signed with.
[`agent/examples/raw_hold.py`](agent/examples/raw_hold.py) is the smallest
script: one Agent holding one Raw Command.

```sh
uv run python agent/examples/raw_hold.py --server 127.0.0.1:27015 \
  --pack <client.apak> --public-key <pack.pub>
```

[`agent/examples/load_test.py`](agent/examples/load_test.py) is the load test
(ADR-0013): it plays the scenario's Player count of Agents, each wandering near
its spawn, aiming at the nearest other player and firing in Bursts, until every
one has seen `--matches` Match ends, then exits 0; it exits 1 as soon as one
fails or `--timeout` passes, and prints every Agent's stats either way. Every
random choice comes from `--seed`. The scenario's Player count must be 2 or
more: a Match of one ends only when its player dies, which nothing in it causes.
The example scenario's is 1, so a single player can run it alone.

```sh
uv run python agent/examples/load_test.py --server 127.0.0.1:27015 \
  --pack <client.apak> --public-key <pack.pub> --matches 3
```

Its link fault options (`--latency-ms`, `--jitter-mean-ms`, `--jitter-max-ms`,
`--loss-percent`, `--reorder-percent`, `--reorder-delay-ms`) simulate an
impaired link on every packet the script sends; `--help` lists them all.

### Running the Agent tests

The package's tests, in [`agent/tests/`](agent/tests/), play against a server in
their own process with the example scenario's packs; run them after the build
above:

```sh
cd agent
uv run pytest
```

Beside them are the C++ tests of whole Matches of Agents, which need no Python.
They join `augusta_tests`, and `ctest` runs them with the engine's:

```powershell
cmake --build --preset windows --target augusta_tests
ctest --preset windows
```

but for the netcode tests (label `netcode`): runs of Agents under simulated
latency, jitter, loss and reordering, minutes each, which every test preset
leaves out and the nightly runs (ADR-0013). To run them by hand, on Linux:

```sh
ctest --preset linux-netcode
```

or on Windows, `ctest --test-dir build/x64-windows -L netcode`.
