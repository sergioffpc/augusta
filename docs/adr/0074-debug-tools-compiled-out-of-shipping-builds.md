# Debug Tools Are Compiled Out of Shipping Builds

Every debug tool - the Debug HUD, Debug overlays, Debug panels, Debug views, the
benchmark mode, reenactments, the pcapng of messages - is for developers only,
and a Shipping build leaves it out of the binary rather than switching it off: a
collision or Hitbox overlay, or a server that hands its state to whoever asks,
is a wallhack in a player's hands. Developers still run optimised builds, so
whether the tools are in is its own axis, not the CMake build type.

**One CMake option.** `AUGUSTA_DEBUG_TOOLS`, on by default, compiles every debug
tool of `augustac` and `augustad` in; the code tests one macro of the same name.
There is one option for all of them, not one per tool: which tools run is a
runtime choice, below. Every existing preset, Release (`windows`, `linux`) and
Debug alike, makes a Development build. Two presets make a Shipping build:
`windows-shipping` and `linux-shipping`, which inherit `windows` and `linux` and
turn the option off. There are no shipping variants of the Debug, sanitizer,
fuzz or coverage presets.

**Who builds which.**

- The release workflow builds the published client and server with the shipping
  presets.
- The `Dockerfile` takes the preset as a build argument, and CI picks it by
  branch: a push to `develop` builds a Development build, which the `develop`
  Environment runs; a push to `main` and every pull request's `container-verify`
  build a Shipping build, which is what a release tag's image, and so `staging`
  (ADR-0056), runs. The `develop` Environment is for developers only and never
  serves players. The image `staging` runs is the same code as the one `develop`
  ran, compiled without the tools, not the same binary.
- The nightly's Linux client build uses `linux-shipping`, so the client's side
  without tools compiles before a tag does; the server's compiles on every pull
  request through `container-verify`.

**Switched on at runtime.** In a Development build, a `debug` section in each
executable's config file (ADR-0034) says which tools start on. Its keys exist
only in a Development build, so a Shipping build fails a config that has them as
it fails any unknown key, rather than ignoring them. How a developer opens and
closes a tool while the game runs (keys, a menu, a console) is decided with the
tools themselves. The server's reenactments setting is one of these keys,
`debug.reenactments` (ADR-0050), and so is each stream of the server's state a
client's Debug overlay can subscribe to, such as `debug.shot_judgements`
(ADR-0038).

**The two kinds never meet.** A Development build's engine version ends in
`+dev`, so a Development client never joins a Shipping server, nor a Shipping
client a Development one: each refuses the other as a version mismatch
(ADR-0038). A Development build's messages then never reach a peer that cannot
decode them.

**Tests.** A debug tool's tests compile only in a Development build. The release
workflow runs the rest of the suite on the Shipping build it publishes, and one
test, compiled only there, checks that a `debug` section is refused.

## Considered Options

- **The tools follow `NDEBUG`** (in Debug builds only): no new option, but
  developers lose optimisation exactly when they need a playable frame rate to
  see what a tool shows.
- **A third CMake build type** (`Development`): the same split, but one more
  configuration for every multi-config generator and for vcpkg's per-config
  libraries, and it ties the tools to one set of optimisation flags.
- **The option off by default, the developer presets turning it on**: a
  configure without a preset is a Shipping build, but every developer's and CI's
  build would have to ask for its tools, and the one build that must leave them
  out would be the default rather than a preset with a name.
- **The tools compiled into every build, switched off by the config**: one build
  to test, but a switch in a config file is one edit, or one patched byte, away
  from a wallhack.
- **The `develop` Environment on a Shipping build too**: the cluster would run
  only what players get, but a developer could then see the server's state only
  against a server run locally.

## Consequences

- The Debug HUD leaves the client players get. FPS or ping shown to players
  would be a feature of the game, decided apart from the Debug HUD.
- `simulation.reenactments` becomes `debug.reenactments`: an older `augustad`
  config that names it fails to load, a breaking change for the release that
  carries it.
- The specs that call a debug tool "absent from release builds" mean absent from
  Shipping builds.
- A developer plays a Development client against the `develop` Environment or a
  local server, never against `staging`; and a Match capture from the `develop`
  Environment names a `+dev` version, so only a Development build reenacts or
  replays it.
