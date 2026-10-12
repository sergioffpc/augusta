# Changelog

Generated from the Conventional Commits history by git-cliff (cliff.toml).

## 3.1.0 - 2026-10-12

### Features

- **cluster:** run augustad in a user namespace under its own seccomp profile
- **cluster:** commit augustad's seccomp profile recorded on x86-64
- **policies:** enforce every Kyverno policy, failing closed
- **cluster:** roll out a revision only once its image is published
- **cluster:** enforce restricted and default-deny in staging
- **pack:** keep each Environment's packs in its own tree and key
- **release:** ship the client as a zip of its whole runtime folder
- **release:** name Falcor's dependencies and the MSVC runtime in the client's SBOM

### Bug Fixes

- **cluster:** check every image change under the seccomp profile
- **cluster:** play one Agent's Match at a time in the seccomp check
- **cluster:** keep recording the seccomp profile when a run logs nothing
- **ci:** publish develop's chart only once every check passes
- **pack:** address review of the per-Environment pack trees
- **release:** check the client runs, and keep it out of a plain install
- **release:** list only what the client ships, with valid SPDX IDs

### Documentation

- **requirements:** state the Node's kernel hardening in NFR-15 and M15
- **runbook:** check the Node after a reboot
- **runbook:** isolate each admission probe's refusal
- mark US-21, US-23 and NFR-09 as shipped in v3
- **adr:** record the client's release as its whole runtime folder

### Tests

- **agent:** name US-23 on the tests that check it
- **agent:** name US-23 on the rest of the Intent tests that check it

### Build

- **bootstrap:** install kubectl, helm and flux, and put Git's bash on PATH
## 3.0.0 - 2026-10-11

### Features

- **cluster:** run a firebase server in develop
- **cluster:** drop the augusta scenario, serve firebase on 30777
- **cluster:** give develop and staging their own node port ranges
- **ci:** publish the nightly's test results and coverage as one report
- **tests:** trace each requirement to the tests that check it
- **tests:** benchmark recording, replication and render frames
- **ballistics:** bound a bullet's flight to five seconds
- **failure:** add the shared error disposition model
- **metrics:** make the endpoint's permanent failure explicit
- **supervisor:** make the runtime stop a typed first-cause contract
- **supervisor:** let a worker escalate its own typed failure
- **config:** add simulation.recording_mode to augustad.yaml
- **server:** add optional and strict recording failure modes
- **application:** add the application boundary both executables end through
- **server,client:** route startup and runtime failures through the boundary
- **networking:** expose and escalate local transport failures
- **chart:** alert when an augustad container restarts
- **cluster:** aggregate pod logs in Loki and query them from Grafana
- **protocol:** reject outbound invariant violations in every build
- **pack:** port the asset cooker to Linux
- **server:** capture each Match and read captures in augusta-inspect
- **chart:** harden the augustad pod and capture Matches to a host directory
- **server:** add optional and strict Match capture failure modes
- **server:** bound each server's Match captures by count and size
- **server:** count Match captures in augustad's metrics
- **chart:** add the Match captures dashboard and capture alerts
- **protocol:** add the Replay list, Replay request and Replay view messages
- **config:** read replay.captures and replay.max_viewers
- **server:** replay Match captures on a replay server
- **harness:** watch a Replay as a Replay viewer
- **client:** list and watch Replays with --replays and --replay
- **protocol:** add the Reenact request and the reenactments-not-accepted refusal
- **server:** admit Reenact requests at their spawn under simulation.reenactments
- **harness:** join with a Reenact request when the Session names a spawn
- **harness:** pace a Captured player's Commands to the server's ticks
- **config:** read augustac's --reenact <capture> --player <n>
- **client:** reenact a Match capture's player with augustac --reenact
- **harness:** run an Agent's Intents as its Runner's Command source
- **agent:** connect Agents from a Python script with a Raw Intent
- **harness:** add the tracking Intents and build the Scripted player on them
- **agent:** tell a Python script its Agents' events, Intent endings and failures
- **agent:** give Python scripts the whole Agent API and load_test.py
- **pack:** give the node an augusta user and keep its packs readable by that group alone
- **bootstrap:** bootstrap the k3s node on Debian 13
- **release:** release the server for Linux arm64, binary and image
- **node:** keep k3s's configuration in Git and meet the CIS k3s profile
- **cluster:** run Kyverno in the cluster and its policies in CI
- **node:** harden the Node's OS: firewall, SSH, updates, auditd
- **cluster:** operate day to day with a least-privilege kubeconfig
- **cluster:** enforce Pod Security levels per namespace
- **cluster:** run only signed revisions; staging follows release tags
- **cluster:** pin every third-party image by digest
- **cluster:** anonymous read-only Grafana and a cluster without Secrets
- **chart:** mount packs and captures from local PersistentVolumes
- **cluster:** default-deny NetworkPolicies; augustad has no egress
- **ci:** sign the augustad image and verify it on admission
- **publish:** publish packs as a user without sudo and a restricted key
- **cluster:** ship security logs to Loki and alert on security events
- **node:** harden the kernel against an escaped container
- **node:** tune network, writeback and lockups
- **node:** size the 10 GbE rings and balance IRQs
- **node:** give transparent huge pages on request only
- **node:** run the Node without swap
- **cluster:** serve firebase's pack 073cb2bb854f in develop
- **cluster:** serve firebase's pack 073cb2bb854f in staging
- **pack:** publish a pack over one SSH connection

### Bug Fixes

- **cluster:** run staging's server from the servers map
- **vscode:** spell stylua.searchBinaryInPATH as the extension declares it
- **ci:** let the nightly's jobs ask for the permissions they need
- **server:** collect every Session's Connection health from one Record
- **server:** report a match recording's real I/O outcomes
- **server:** name the Recorder's stopped accessor in CamelCase
- **harness:** take combat events of the Server view's own Match
- **metrics:** contain connection failures and publish unavailability last
- **swarm:** describe a worker failure as a failure::Failure
- **tests:** stop discarding the supervisor test's WorkerResult
- **server:** fail a strict run on a recording loss found at the stop
- **server,client:** address review of the application boundary
- **networking:** address review of the local transport failure change
- **tests:** keep received queues alive across their range-for
- **swarm:** hold the server view while reading its admission
- **tests:** keep the requirement markers directly above their tests
- **server:** default-initialise RecorderOptions::on_state
- **server:** satisfy clang-tidy in the recording failure modes
- **server:** name on_state in the recording tests' RecorderOptions
- **hooks:** make pre-commit and commit-msg executable
- **protocol:** report each outbound invariant once and keep ticks whole
- **tests:** initialise every JoinAcceptedWire field the refusal test names
- **server:** keep protocol types out of host_impl.h and fix CI
- **docker:** drop the base image's unpatched Pebble from the runtime stage
- **server:** address review of the Match capture
- **tests:** build the capture tests on Windows
- **tests:** keep the capture directory until the host has closed its file
- **server:** name capture losses by step and stop captures at run end
- **tests:** build the capture tests on Windows
- **server:** include what the capture metrics use
- **ci:** compare each night's benchmarks only with the same runner CPU
- **ci:** fail the benchmarks job when the runner names no CPU model
- **runtime:** stop a failed runtime's sends and keep a startup invariant's Code
- **server:** address the replay server review
- **runtime:** stop a failed Host's tick messages mid-tick
- satisfy clang-tidy and clang-format on the reenactment changes
- **harness:** hold a Captured player to the server's ticks and leave without losing Commands
- **client:** include the header that declares presentation::Aim
- **networking:** keep what arrived before the server closed a client's connection
- **harness:** define Held and Tracking outside IntentExecutor
- **agent:** keep every event until the script takes it
- **bootstrap:** stop the node bootstrap under sh and leave a syncing Flux alone
- **ci:** publish every main/develop commit's image, even when superseded
- **node:** refuse to configure a node without encryption prepared
- **node:** keep the LAN out of the pod and service networks
- **cluster:** trust each of the maintainer's signing keys
- **cluster:** stop trusting the Dell R630's signing key
- **publish:** seal a pack before it is published, and refuse more
- **server:** stop captures before Run returns when the loop never ran
- **node:** reach every interface's redirects, and review fixes
- **cluster:** let Grafana's data-sources init container exit
- **pack:** leave nothing behind when a publish fails or races

### Performance

- **simulation:** pose a bullet's hitboxes once a tick, near it only
- **server:** write match recordings off the Simulation thread
- **client:** convert server data once and lend it to every render frame
- **server:** replicate a tick's bodies once for every recipient
- **interpolation:** index remote bodies by Entity ID
- **interpolation:** keep each body's history in a fixed ring

### Refactoring

- **pack:** name the pack signing keypair signing.key/signing.pub
- **harness:** name the pending events' Match by matches_started
- **simulation:** measure hitbox reach standing, not per listed stance
- **protocol:** add neutral shared primitives alongside the codec's
- **protocol:** tighten the neutral primitives after review
- **server:** name a recipient update by its entity and tidy review nits
- **config:** keep client Input off the headless server surface
- **config:** address review of the server/client config split
- **config:** move key-binding errors out of the shared config
- **protocol:** take counters and bounds from the neutral primitives
- **protocol:** contract the codec to math and the neutral primitives
- **harness:** move NetcodeStats from augusta-swarm into the Harness
- keep the protocol at the replay server's edge and satisfy clang-tidy
- **server:** **BREAKING** remove the Match recording and augusta-replay
- **server:** fix the capture's frame format in frames.h
- share the capture file format and the shared core's wire conversions
- drop the includes the shared capture format and wire conversions left unused
- **failure:** retire the legacy error paths
- **agent:** type LoadPack's error and keep a cached default Python lax
- **agent:** retire augusta-swarm into tools/agent
- **pack:** name the native modules' dir native and the package augusta_pack
- **pack:** name the project augusta-pack, as augusta-agent is
- **pack:** bind the cooker's native steps as one _native module
- **node:** install the kernel parameters from a list

### Documentation

- **runbooks:** fast-forward the release branch to main before the back-merge
- **security:** support the latest release, not a pre-1.0 main
- align the remaining server platform statements with NFR-04
- **client:** tighten the render-frame reuse contracts after review
- **readme:** trim the project summary paragraph
- **readme:** drop the PhysX mention from the ECS highlight
- **readme:** name the physics layer and drop the v1 scope highlight
- **readme:** reduce the status section to the roadmap pointer
- **vision:** trim the vision statement
- **vision:** record v1 as met and drop its scope line
- **requirements:** document the engine's undocumented requirements
- **adr:** replace the match recording with match capture, replay and reenactment
- **requirements:** rewrite US-21 and NFR-09 for capture and replay
- **adr:** add ADR-0052 for Agents scripted in Python through Intents
- **requirements:** version each requirement and add horizontal scalability
- drop v1-scoped and hobby wording, and describe connections as encrypted
- **roadmap:** group milestones by release and record what v2 shipped
- **roadmap:** plan v3 as Agents, client metrics, capture, replay and scale-out
- **requirements:** add v3's Agent, server-with-room and load observability requirements
- **adr:** record outbound protocol invariants in ADR-0038
- **comments:** drop the v1 scope from code comments
- **engineering:** give v1.0.0 as the example of a tagged release
- **architecture:** drop the v1 scope from the deployment view
- **architecture:** drop the v1 scope from the threading constraint
- **adr:** drop the v1 scope from ADR-0005's threading model
- **architecture:** drop the v1 scope from the scope-ambition risk
- drop the remaining v1 scope wording
- **adr:** frame Match capture records with a one-byte length
- **agents:** name the main flow and the git flow in CLAUDE.md
- **agents:** name the engineering flow in CLAUDE.md
- **readme:** say how to install the agent skills the flows run on
- add a table of contents to README, ARCHITECTURE, ENGINEERING and ROADMAP
- **readme:** say make install covers only the server
- **release:** address review of augustac's PDB publishing
- **adr:** say a Reenact request is refused for reenactments off before its other checks
- **runbooks:** roll staging back by Git revert, as develop
- **runbooks:** spell out staging's hotfix revert commands
- **architecture:** draw the deployment view
- **runbooks:** point a capture pod stuck in ContainerCreating at the capture filesystem
- **roadmap:** move client-side metrics and horizontal scalability past v3
- **agent:** document Agents in place of augusta-swarm
- **agent:** restore the example commands' line breaks and tighten the Agent wording
- **adr:** record the cluster hardening decisions for v3
- **engineering:** describe the cluster's admission policies
- **cluster:** cite ADR-0056 without linking it before it lands
- **runbooks:** name the release tags staging can roll back to
- **cluster:** link ADR-0056 now it is on develop; staging's Environment
- **runbooks:** link ADR-0056 from the rollback runbook
- explain why kube-bench keeps 4.2.12 at WARN
- **runbooks:** check Grafana refuses admin/admin at its login
- **adr:** note ADR-0057's move of node-exporter in ADR-0049
- **runbooks:** mark removing a server's volumes as cluster-admin
- **cluster:** untangle ADR-0057's isolation paragraph; rename Kyverno's egress policy
- **adr:** record that attestations satisfy the image signature policy

### Tests

- **harness:** an ended Match's view takes nothing once the next has sent its own
- **simulation:** pin hit semantics a shared hitbox pose must keep
- **benchmarks:** time a tick whose rounds are judged at different moments
- **interpolation:** tidy the indexed interpolator's tests per review
- **networking:** cover local transport failures at the runtime seams
- name the new requirements the tests check
- **server:** hold a capture frame to its one-byte length

### Build

- **windows:** pin StyLua and taplo, fail the bootstrap on a failed pin
- enforce the server platform-support contract
- **make:** add a coverage target
- **bootstrap:** add macOS and Linux bootstraps, one recipe with the dev container
- **devcontainer:** run the dev container from a macOS terminal
- **platform:** accept Linux arm64 as a development server platform
- **bootstrap:** one bash bootstrap for Windows and Linux, no PowerShell
- **bootstrap:** bootstrap macOS through the dev container
- **container:** cut the server image's root with chisel, on scratch
- **release:** publish augustac's PDB with each release
- **image:** copy CMake files after the vcpkg install layer
- **presets:** run the Linux test presets four tests at a time
- **bootstrap:** bootstrap a fresh checkout from develop
- **deps:** bump third_party/vcpkg from `eb2d3a3` to `2750401`
- **deps:** bump the actions group with 4 updates
## 2.0.2 - 2026-10-09

### Bug Fixes

- **docker:** drop the base image's unpatched Pebble from the runtime stage

## 2.0.1 - 2026-10-08

### Bug Fixes

- **swarm:** hold the server view while reading its admission

## 2.0.0 - 2026-10-07

### Features

- **devcontainer:** reproduce the Linux/server build environment
- **loadtest:** fill a server with Scripted players
- **chart:** declare augustad's readiness, resources and grace period
- **bench:** benchmark the server tick, protocol, pack load and ballistics
- **server:** log a symbolized stack and leave a core dump on a crash
- **protocol:** encode match recordings as records of their own
- **server:** record matches and replay them on SimulationWorld
- **server:** serve /metrics and /livez from augustad
- **cluster:** install kube-prometheus-stack through Flux
- **server:** expose augustad's tick, Lobby/Match, Session, network and combat metrics
- **server:** measure every client's Connection health
- **chart:** scrape, liveness-probe and alert on augustad
- **chart:** ship the Server and Connection health dashboards
- **cluster:** sign Grafana in as admin/admin
- **cluster:** run a server per scenario, from packs augusta-publish puts on the node
- **pack:** compose scenarios from files named in scenarios/<name>.yaml
- **pack:** **BREAKING** name characters by their key in the scenario manifest

### Bug Fixes

- **presentation:** initialize every character a designated initializer builds
- **pack:** store the texture fixture as an LFS pointer
- **server:** let the crash handler's re-raise end augustad in the container
- **server:** include signal.h for SIGBUS
- **server:** keep the protocol out of host_impl.h
- **audio:** exclude output_miniaudio.cpp from the Linux lint by its new name
- **server:** re-sync bodies across builds and harden recordings
- **protocol:** keep WriteList's bound used when asserts compile out
- **physics:** default a movement input's direction to zero
- **networking:** listen on a port no client socket holds
- **networking:** drop an include the socket test does not use
- **loadtest:** keep Scripted players within reach of their spawn
- **server:** find a Session's gauge slot without an iterator type
- **chart:** hold augustad's liveness probe behind a startup probe
- **docker:** copy tools/replay into the image's build stage
- **swarm:** initialize every BodyState field in the scripted player tests
- **swarm:** look up a whole-number key without an iterator type
- **pack:** seed the example pieces from a plain list
- **lint:** keep PSUseCorrectCasing out of the parallel analyzer pass

### Refactoring

- **assets:** keep a pack's path in Pack and verify packs in one place
- **client:** move content loading into runtime
- **client:** describe a run failure in the runtime
- **protocol:** **BREAKING** name a character by its path, not an index
- **client:** split the client by responsibility into augusta::client
- **client:** create the runtime in one place
- **client:** verify the pack where the runtime is created
- **client:** set up networking with the rest of the process
- **server:** split the server by responsibility in augusta::server
- include what each source uses, as misc-include-cleaner asks
- **client:** decide Lobby readiness in its own type
- **client:** drop the <set> include CharactersToLoad's move left behind
- **client:** wait for admission before the render loop
- **harness:** run a Session's threads in a harness Runner
- **client:** rename Show to StageRenderFrame
- name the moment a frame shows the other players its Seen time
- **config:** expose the schema readers for a tool's own settings file
- **bench:** measure every tick phase and share the benchmark scene
- **map:** add the Map's collision to a world the same way on both sides
- **server:** split Host into a file per responsibility
- **harness:** split Session into its inbox and its command stream
- **audio:** name the playback pair and the output device symmetrically
- **protocol:** read and write every counted list through one helper
- **assets:** **BREAKING** name the client pack in the pack header
- **server:** serve the metrics endpoint with Boost.Beast
- **server:** account for every command and guard metric ranges at compile time
- **server:** record Connection health without waiting on a scrape
- **replay:** move augusta-replay out of src into tools/replay
- **swarm:** rename augusta-loadtest to augusta-swarm

### Documentation

- document every header's module and drop comments that restate names
- mark each header's module comment as its file description
- **runbooks:** add the Flux rollback, key rotation, k3s recovery and release runbooks
- **site:** list the runbooks in the site's navigation
- **runbooks:** roll staging back by image pin and tighten the release path
- point the docs index and Git Workflow at the runbooks
- **adr:** number the crash-reports ADR 0047
- **adr:** decide match recording and replay (ADR-0050)
- **adr:** re-sync across builds, the record limit and the write's thread
- **adr:** number the match-recording ADR 0048
- **adr:** add metrics and liveness for the dedicated server
- add status and project badges to the README

### Tests

- **networking:** bind test servers to a port of the server's choosing
- **fuzz:** regenerate the pack_load server seed from the new golden pack
- **harness:** drop runner_test's unused memory include
- **server:** move the crash timestamp property into its own labelled target
- **server:** keep the crash timestamp property within system_clock's range
- **server:** show both sides' bodies when a replay test diverges
- **netcode:** play Scripted players through an impaired link nightly
- **loadtest:** name every HostConfig field in the example server
- **server:** re-record the golden match with the renamed pack header
- **server:** include what the host metrics test uses
- **server:** include the Connection sample header where the tests use it

### Build

- **deps:** bump third_party/vcpkg from `ee6a47d` to `b8b8df2`
- **lua:** format and lint the scenarios' Lua scripts
- **toml:** format and lint TOML, and name selene's library for itself
- **lua:** lint the Lua scripts with luacheck instead of selene
- **lint:** run include-cleaner on the sources clang-tidy leaves out
- **deps:** bump third_party/vcpkg from `b8b8df2` to `eb2d3a3`
- **docs:** publish a documentation site with MkDocs and Doxygen
- **docs:** build the API reference without Graphviz graphs
- **devcontainer:** make the dev container the one Linux environment
- **loadtest:** build augusta-loadtest through tools presets
- **coverage:** show the coverage report in VS Code
- **lint:** turn off a clang-analyzer check that misfires in MSVC's STL
- build the C++ tools together under AUGUSTA_TOOLS
- **tools:** share one uv environment across tools/
- lint and format Python, shell, workflows, CMake, Markdown and PowerShell
- format Markdown with Prettier
- follow Google's Markdown, Python and shell style guides

## 1.0.0 - 2026-10-02

### Features

- scaffold M0a client & dev environment
- wire up sccache and Ninja+cl compile_commands for the client build
- fully enforce Google C++ Style Guide via clang-format/clang-tidy
- add CMake presets, VS Code config, and CI/tooling hardening
- add CodeQL analysis; fix formatting for 120-col limit
- add tag-triggered release workflow for client/server binaries
- add header-only augusta::logging module wrapping spdlog
- **client:** migrate entry point to augusta::logging
- add server Dockerfile
- add Helm chart for the dedicated server
- add Flux GitOps manifests for the r630 cluster
- **ci:** add helm lint + docker build/push validation
- scaffold M1 engine modules, design Physics and Math interfaces
- design Input and Renderer interfaces, fold Window into Renderer
- design Networking interface (Client/Server, GameNetworkingSockets)
- design Audio interface, add ADR-0028 (miniaudio output backend)
- design Scripting interface (Lua/sol2 policy hooks)
- design Ballistics interface, add physics::World::Raycast
- **ecs:** add SimulationWorld/PredictionWorld/PresentationWorld pipelines
- **animation:** add animation module and wire into PresentationWorld
- **ecs:** trace-log each pipeline phase
- **runtime:** add ClientRuntime/ServerRuntime and wire them to main
- implement placeholder stubs so augustac/augustad link
- **renderer:** implement real Falcor renderer for M1's spike
- **renderer:** retune Falcor's Profiler HUD defaults and colors via patch
- **renderer:** monochrome debug HUD with auto-sized windows
- **tooling:** add a pre-commit hook that auto-formats staged C++ files
- **renderer:** add Memory and geometry stats to the Stats window
- **renderer:** apply a 700px minimum width to all HUD windows
- **networking:** implement GameNetworkingSockets round-trip (issue #31)
- **logging:** add structured logging to networking, permit peer IPs
- **devenv:** add Debug presets, fix bootstrap gaps for a plug-and-play setup
- **physics:** implement PhysX prediction/reconciliation spike (issue #32)
- **renderer:** replace in-app debug HUD with Nsight/Tracy/NVTX tooling
- **client:** plot ConnectionStats as NVTX counters on the network thread
- **scripts:** add asset pipeline tooling bootstrap script
- **tools:** scaffold the asset-cooking CLI and define its pipeline
- **assets:** cook a single mesh into an unsigned pack and load it back
- **assets:** scene graph, lazy loading, and BLAKE3/Ed25519 pack signing
- **assets:** compress cooked textures via DirectXTex (BC7/BC5/BC4)
- **assets:** run cooked mesh geometry through meshoptimizer
- **assets:** split cooked packs into client and server variants
- **tools:** orchestrate stage cleanup/validation before cooking
- **presentation:** add NVTX ranges to each ECS phase
- **runtime:** load and verify a signed pack at startup
- **tools:** rewrite the asset cooker as a pure-Python pipeline
- **tools:** add cooker progress, --skip-validation and a scaffolded Composer app
- **tools:** resolve cooker stage extensions and install commands under bin/
- **tools:** add pack inspect/verify commands and rename the project to pack
- **renderer:** add an always-on debug HUD with frame time and connection stats
- **client:** load and render the scene of the pack given on the command line
- **logging:** ISO-8601 UTC console format with upper-case level
- **build:** add a Makefile over the CMake presets and install make in the bootstraps
- **config:** read client and server settings from a YAML file
- **pack:** cook UsdGeomCube geometry and per-mesh base color
- **client:** draw meshes in their cooked base color
- **config:** parse the command line with Boost.Program_options
- **networking:** per-message reliability and simulated network conditions
- **physics:** share one PhysX foundation across every World in a process
- **physics:** static level geometry and stance headroom
- **level:** build collision from the signed pack for both worlds
- join handshake with the Networking Protocol and match admission
- server-authoritative movement round trip
- client prediction reconciled against the acknowledged sequence
- reconcile the client by replaying its commands from the server's state
- skip the reconciliation when the server agrees with the prediction
- **map:** load a scene's spawn points in world space
- **config:** read validated stamina tuning from the server config
- **networking:** say why a peer disconnected and let tests shorten timeouts
- spawn at map spawn points, take the server's stamina rules, survive bad peers
- load Parameters from a Lua script with strict validation
- sandbox the Parameters script and allow expressions in it
- start the server from a Parameters script; stamina keys leave the YAML
- take the tick rate from the server's Parameters
- reload Parameters live on the server, between ticks
- reload Parameters when the script file changes
- send Parameters to clients after each reload
- ship a scenario's Lua scripts in its server pack, drop the reload
- commit a worked example scenario, seed it from bootstrap
- add a runtime log-level floor, config-driven, below TRACE by default
- **presentation:** interpolate remote players in PresentationWorld
- draw remote-player placeholder boxes; M3 8-client soak test
- **pack:** compose a scenario's pack from a manifest naming its map and characters
- **client:** draw every remote player as its character mesh, in the right stance
- **pack:** record the scenario's ordered character list in both packs
- **client:** drive the local player from the keyboard and mouse
- **client:** release the cursor and keyboard on Escape, recapture on click
- **client:** bind controls to keys in augustac.yaml, grouped into sections
- choose a character in augustac.yaml and have the server validate it at join
- set a scenario's Player count in its Parameters
- **protocol:** quantize a body's and a command's numbers on the wire
- add a Lobby before every match, with automatic Ready (ADR-0043)
- refuse a Join whose client pack is not the server's cook
- send yaw and pitch on a 2^-21 rad grid, precise enough to aim at 800 m
- **protocol:** make network contracts explicit and compact
- **client:** put the camera at the character's authored eye
- **tick:** keep the server and client tick loops on a fixed schedule
- **client:** raise the Windows timer resolution for the process lifetime
- **logging:** give every log level a _LIMITED variant
- **presentation:** interpolate remote players on the server's timeline
- **physics:** force a walk on stamina exhaustion until recovery (US-05)
- **net:** keep Commands and server ticks in step
- **presentation:** smooth local view with tick blending, per-frame mouse-look and stance eye height
- **parameters:** configure the rifle, its ammo and starting health
- **pack:** author hitboxes by body part and load them on the server
- **pack:** cook each character's eye into the server pack
- **server:** fire the rifle and announce every Shot
- **weapon:** reload the magazine
- **server:** resolve hits by body part and apply damage
- **prediction:** predict the rifle and reconcile it against the server
- **server:** judge hits at the Shooter's delay
- **weapon:** climb by the recoil pattern per Burst, tamed in ADS
- **presentation:** draw tracers, impacts, muzzle flashes and the aiming view
- **build:** --help and --version, cmake --install, GNU make targets
- **scripting:** run Game policy in a sandboxed Lua engine loaded from the server pack
- **simulation:** a player whose health reaches zero dies, and every client is told
- **pack:** cook the client's cue sounds into its pack
- **presentation:** a dead player spectates living players until the Match ends
- **audio:** play spatialized gunshots and the hit-marker sound
- **simulation:** Game policy assigns Spawn points at Match start
- **simulation:** the last player standing wins, the Match ends and the next starts on its own
- **presentation:** hit-taken, death and match-end stingers
- **runtime:** introduce explicit runtime boundaries and typed tick outcomes
- **server:** disconnect a peer that keeps misbehaving
- **server:** disconnect a peer not admitted within a deadline

### Bug Fixes

- install mono for vcpkg NuGet binary caching on Linux CI runners
- vendor vcpkg as a pinned submodule instead of a machine-wide install
- pin CI runners to windows-2025/ubuntu-26.04; drop fragile LLVM pin
- separate Windows/Linux build dirs; fix VS Build Tools winget ID
- copy compile_commands.json to workspace root for clangd
- point clang-tidy at build-linux, not build
- pin codeql-action init/analyze to the same version; group Dependabot Actions PRs
- point the flux-system self-sync at develop, not main
- **ci:** use commit sha for the container job's PR-only build tag
- **chart:** sanitize the helm.sh/chart label's version segment
- wire stub modules into the build, fix formatting
- **editor:** make clangd fallback include paths machine-independent
- **cluster:** pin storageNamespace to stop develop/staging release collision
- **ci:** replace broken Mono/NuGet vcpkg cache with actions/cache
- **lint:** suppress a Flecs builder-chain clang-analyzer false positive
- **vscode:** force an external console for the Debug Client launch
- **lint:** disable modernize-use-scoped-lock, crashes this clang-tidy build
- **lint:** resolve real clang-tidy findings from the runtime/stub work
- **renderer:** vendor Falcor as a plain submodule, not a personal fork
- **cmake:** move Falcor patch under cmake/patches/, declare byproducts
- **cmake:** fix IMPORTED-target validation bug CI hit; expand debug GUI
- **cmake:** run Falcor's setup.bat before configuring it
- **cmake:** stop Ninja pre-creating packman's own link targets
- **renderer:** fully release the old swapchain before recreating it
- **renderer:** stop the CPU stat from counting the profiler's GPU wait
- **renderer:** stop the Stats window overlapping Settings
- **renderer:** darken the default clear color to near-black
- **renderer:** mark Renderer::Impl final to silence a real dtor warning
- **networking:** add Shutdown() to fix an ASan leak in the round-trip test
- **devenv:** add LLVM to PATH explicitly and verify tools after bootstrap
- **physics:** satisfy clang-tidy and fix Linux PhysX link order
- **physics:** remove classic PVD wiring from the runtime engine
- **assets:** fix Pack::Load CI build failures
- **docker:** copy cmake/ into the server image build context
- **assets:** restore assets.h's re-export of encoder.h
- **assets:** fix Linux CI failures in the encoder/decoder split
- **tests:** raise gtest discovery timeout for the PhysX test binaries
- **build:** make clean succeed when nothing is configured
- default-initialize error struct members
- **networking:** satisfy clang-tidy naming on the handler id
- default-initialize the session and host config members
- finish renaming the client session header guard to harness
- **deps:** pin third_party/nvtx back to 3.6.0
- **physics:** give CanChangeStance parameters names clang-tidy accepts
- **protocol:** default-initialize message list and string members
- **physics:** create a body with its feet at the given position
- stop a Parameters script from catching the instruction limit
- cook packs under authoring/'s own layout, refuse scenarios outside it
- NOLINT the auto that CI's libstdc++ clang-tidy wants as auto*
- **lint:** use ranges find, name the stance-switch midpoint constant
- cross-platform CI failures in PR #119 (missing initializer, unpaced soak test)
- **client:** track the render camera to the local player's live position
- **renderer:** give each frame in flight its own remote-player region
- **presentation:** keep <windows.h>'s max macro out of presentation.cpp
- **deps:** pin NVTX back to v3.6.0 and build on submodule bumps
- **make:** print help without the shell
- **test:** build the RapidCheck property tests with clang
- **renderer:** compile Falcor's headers with NVAPI, as Falcor 9 builds it
- **build:** uninstall honours DESTDIR, and --version comes from ParseCommandLine
- **hooks:** pre-push lints the audio output sources only where they build
- **tests:** don't name a fixture helper Setup, which gtest reserves
- **client:** a worker thread that throws stops the runtime and is reported
- **protocol:** the server tick is 64 bits wide, so it never wraps
- **networking:** a connection GNS cannot create leaves the client disconnected
- **build:** align Falcor compiler selection and setup docs
- **test:** initialize the impossible-action snapshot member by member
- **test:** initialize every field without a default in the reported-outcomes claim
- **test:** name each test's temporary pack and directory after the test
- **ci:** retry transient vcpkg source download failures
- **ci:** retry vcpkg bootstrap downloads
- **ci:** check out LFS files in the release workflow's tools job

### Performance

- **assets:** memory-map packs instead of double-reading them
- **logging:** check the runtime level before formatting a log call
- **networking:** send unreliable messages without Nagle and stop spinning the network threads

### Refactoring

- move vcpkg submodule under third_party/
- split src/shared into src/modules/version
- drop redundant spdlog::spdlog link from the server target
- rename cluster dir from r630 to onprem
- **cluster:** rename production environment to staging
- **build:** reorder build dir naming to arch-first (x64-windows, x64-linux)
- **renderer:** split DrawGui() into one method per HUD window
- **renderer:** extract the debug HUD into its own DebugHud class
- **renderer:** consolidate Falcor's vendoring patches into one file
- **assets:** split blob encode/decode out of assets.cpp
- **assets:** give encoder/decoder their own public-style headers
- **src:** apply new coding standards to physics and assets
- **build:** make the asset-pipeline native modules a standalone project
- **logging:** replace spdlog with Boost.Log
- **assets:** map packs with Boost.Interprocess instead of mio
- **client:** extract pack verification out of main
- **config:** report errors as ConfigError instead of strings
- **client:** report scene and pack errors as types
- **client:** name the prediction thread for what it runs
- extract a headless client session and a steppable server host
- rename the client session module to harness
- **assets:** share scene world transforms, the scene path and resolve-error wording
- rename the level module to map
- load the client map through Session::AddStaticMesh
- hand the harness Session a prediction world; rename AddStaticMesh to AddCollisionMesh
- build the client's prediction world in the runtime constructor body
- publish the harness's server state as immutable views instead of locking it
- carry out each world phase in its own OnXxx method
- rename the client's scene_loader to loader
- name the networking status-handler registry for what it holds
- mark physics CanChangeStance as nodiscard
- carry a shared Parameters type instead of the stamina rules
- share the Parameters decisions and number them as one type
- move the Parameters loader into the server
- name the client's loader scene_loader again
- move the tick rate from Parameters to augustad.yaml
- cut the per-tick log volume down to a heartbeat
- augustap takes plain paths; add augustap-composer to launch it
- rename SetMinSeverity/LoadMap, move collision out of runtime::Config
- wrap the client's collision load in a Map struct
- **harness:** name Session's connection getters after the connection
- **parameters:** hold the Player count in one byte
- **protocol:** carry only the protocol's own plain types on the wire
- **protocol:** suffix the types that mirror the engine's with Wire
- **config:** build config errors through one Fail helper
- **client:** decompose startup and standardize error logging
- **assets:** centralize pack section writing and verification data
- keep protocol Wire types at the network edge
- drop augusta_identity; each peer names sessions its own way
- **renderer:** draw every remote player given, with no cap of its own
- **renderer:** draw remote players as authored, without height_scale
- **client:** include what each file uses
- **grid:** move the quantisation grids out of protocol
- **command:** move Command into the shared core
- **server:** stop passing a placeholder script to SimulationWorld
- **math:** keep the quantisation grids in augusta_math, not a module of their own
- **physics:** drop the unused GPU option and dispatcher threads
- **server:** log a command's rejection in its own function
- **presentation:** take input types of its own instead of the harness's
- **presentation:** take a WorldSnapshot of DynamicBody, not PlayerBody
- name every dynamic body by an Entity ID, not a Session ID
- **physics:** name the capsule diameter StanceHeight adds
- **ballistics:** share the trajectory, hit the Map and posed hitboxes
- **server:** hold a character's hitboxes with its path
- **simulation:** give Match end refusals and reasons types, and never declare Session ID 0 the winner
- **tick:** every tick is a tick::Tick, so its width is set in one place
- **tools:** separate Composer and pack setup
- **protocol:** widen command sequences to a 64-bit Sequence type

### Documentation

- add initial project documentation
- add README with project description
- restructure domain docs into CONTEXT.md and docs/adr/
- add Building section to README; require manual CodeQL build mode
- add SECURITY.md
- adopt Flux for main/develop CD; drop self-hosted runner entirely
- **adr:** rework asset pipeline around Omniverse tooling
- **roadmap:** move Asset Pipeline milestone from M5 to M2
- **adr:** add ADR-0032 (scene graph format), fix stale namespace note
- **agents:** add coding standards for generated C++ code
- **adr:** never return errors as strings
- record Boost.Program_options and fix the pack-input note
- decide server-owned Lua Parameters as data-driven configuration (ADR-0039)
- add session/match/command glossary entries, fix stale replication note
- **adr:** decide how a player's character is chosen and replicated
- **adr:** put a lobby in front of every match
- correct header comments that still called the protocol and replication future work
- **adr:** record that a raycast sees a body where it was created
- define testing strategy by pipeline stage
- correct how Harness tests simulate latency in ADR-0013
- **adr:** record shot lag compensation and replication (ADR-0044)
- resolve contradictions across ADRs, requirements and architecture
- stop calling Prediction and Presentation State deferred designs
- **adr:** decide dynamic bodies and where PhysX simulates
- **networking:** status callbacks are queued, never run inside the call that queues them
- **engineering:** record the release-signing decision

### Tests

- add a trivial Google Benchmark target
- keep the packet-loss test from depending on its last datagram
- **session:** bind the scripted server once for every invalid parameter
- initialize every JoinRequest field, as GCC requires
- **protocol:** expect the one-byte tick rate in Join accepted's layout
- **config:** check the tick-rate message under the key the parser reports
- **build:** fail when physics or SimulationWorld name protocol or input
- **networking:** wait for the client's own timeout, not just the server's
- **session:** tick the server only once the commands a step sent have arrived
- **networking:** wait for the client's own timeout, not just the server's
- **session:** tick the server only once the commands a step sent have arrived
- **physics:** show a raycast hits a moved body where it was created
- **networking:** wait for the client's own timeout, not just the server's
- **ballistics:** check NFR-03 against golden trajectories
- **pack:** cover the asset cooker with pytest
- hold the pack contract to committed golden packs
- regenerate the golden server pack for develop's parameters.lua
- **protocol:** add RapidCheck and a round-trip property
- **protocol:** fuzz protocol decoding with libFuzzer
- **assets:** fuzz pack loading on every pull request
- **simulation:** add stamina and reconciliation properties
- **server:** initialize Character's hitboxes in session tests
- **server:** eight clients firing full-auto hold the tick
- **server:** eight players play a full Match to its end with no missed ticks
- **server:** prove impossible client values are rejected end to end
- **server:** check the heartbeat count only where DEBUG is compiled in
- **server:** prove impossible game actions are corrected end to end
- **assets:** name temporary packs like the other suites' files

### Build

- reorganize CMake output dirs and rename client/server binaries
- **chart:** rename augusta-server chart to augustad, pin develop NodePort
- enforce the client/server module boundary at configure time
- split client/server runtime and asset-cooking tool dependencies
- split AUGUSTA_BUILD_TOOLS from AUGUSTA_BUILD_RUNTIME
- **docker:** install autotools for vcpkg's libsodium port
- **deps:** bump third_party/vcpkg from `a1cae00` to `386d7c4`
- **deps:** bump third_party/nvtx from `029d607` to `7d113f2`
- **deps:** bump the actions group with 2 updates
- add make tidy and make lint targets
- **clang-tidy:** enforce include-what-you-use with misc-include-cleaner
- **clang-tidy:** include what the dependency-heavy files use
- **deps:** bump third_party/vcpkg from `386d7c4` to `dc1232a`
- **deps:** bump third_party/falcor from `eb540f6` to `759aad0`
- **deps:** bump third_party/nvtx from `029d607` to `7d113f2`
- **deps:** bump the actions group with 2 updates
- compile Linux with clang
- **hooks:** run clang-tidy on changed sources before a push
- **wsl:** install the autotools vcpkg's libsodium needs
- include NVTX as a system header and glm's angleAxis directly
- **deps:** bump third_party/vcpkg from `dc1232a` to `ee6a47d`
- pin the local LLVM to the major CI's Ubuntu runner ships
- compile the tests only when they are asked for
- **falcor:** download a prebuilt Falcor from a release instead of building it
- **vcpkg:** rename the retrying vcpkg fetch to fetch-vcpkg.ps1
- **vcpkg:** drop the vcpkg download retries the hotfix brought
