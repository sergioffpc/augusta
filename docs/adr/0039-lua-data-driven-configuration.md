# Data-Driven Configuration in Lua, Shipped in the Scenario's Server Pack

Data-driven configuration (the tunable values of ADR-0023: weapon and ammo
numbers, stamina rules, and their like) is called **Parameters**. It is written as
a Lua script, `parameters.lua`, so a value may be an expression of other values.
It uses the same embedded Lua (sol2) and sandbox as game policy (ADR-0022), in a
Lua state of its own: the parameters script and a policy script share no globals.

**A scenario's scripts live with its stage.** The values change from scenario to
scenario, so the script is authored in the scenario's folder next to its stage
(`authoring/test_map/test_map.usda`, `authoring/test_map/parameters.lua`,
ADR-0015). The cooker is told the folder and packs every `*.lua` file under it
into the scenario's **server** pack (ADR-0030, ADR-0031), addressed by its path
relative to the folder. The scripts are therefore signed with the map (ADR-0018)
and cannot change during a run. A client never receives a script (ADR-0019): it
is sent the values.

**Where each kind of decision lives.** The YAML files of ADR-0034 (`augustac.yaml`,
`augustad.yaml`) exist to replace command-line arguments: where the pack is, which
key verifies it, which address to use, and the one engine setting the server needs
before it starts and can never change, its tick rate. They never hold a simulation
rule. A simulation rule is one of three things: mechanism, in C++ code; a tunable
value, in the scenario's parameters script of this ADR; or policy, in a behaviour
script (ADR-0022).

**Evaluated once, at startup, never in the tick.** The server reads
`parameters.lua` out of its pack before it opens a socket, runs it once and keeps
the resulting `Parameters` for the life of the process. A pure function,
`script text → std::expected<Parameters, Error>` (ADR-0033), reads the returned
table into `Parameters`, a plain immutable C++ struct. Mechanism code reads that
struct and never calls Lua, so an expression costs nothing per tick and cannot see
simulation state: the script runs outside the tick, with nothing of the world in
reach. Beyond ADR-0022's sandbox, the environment has no `os.time`, `os.clock` or
`math.random`, and an instruction limit ends a script that does not return. The
loader reads named fields and, to refuse an unknown key, reports the first one in
name order, so the table's own order cannot change the result.

**Validated like untrusted input.** Unknown keys, missing keys, a value of the
wrong type and a number out of range are errors, as in ADR-0034: a misspelled
field never falls back to a default, which a Lua table would otherwise allow in
silence. A server whose pack has no `parameters.lua`, or one that does not load,
exits at startup naming the script and the field, as it does for a bad pack. The
cooker refuses a scenario folder without a `parameters.lua`, so that is found when
the pack is made rather than when a server starts on it.

**The tick rate is not a parameter.** It is fixed for the life of the server
process: every command, acknowledgement and stretch of history is counted in
ticks, so a rate changed under them would put both sides out of step. It is
engine configuration the server needs before it loads the map, like the pack and
the address it listens on. It is the `tick_rate_hz` key of `augustad.yaml`
(ADR-0034), required and any finite number above zero: NFR-01's 60 Hz is what the
server must sustain, measured, not a floor on the value, so a run may go slower to
be debugged. A `tick_rate_hz` left in the script is an unknown key.

**The server is the only source.** Whatever client and server must agree on is
decided by the server and nowhere else. A client ticking at another rate than the
server it predicts against steps its physics with another delta and can never
agree with it, so the server sends the tick rate, and the `Parameters`, in Join
accepted (ADR-0038), once. The client has no copy of a shared value, no default
and no key for it in `augustac.yaml`; it does not start ticking its prediction
until it holds the server's tick rate and parameters, and it drops a Join accepted
whose rate is not finite or whose parameters fail the same range checks.

**No hot reload, for now.** The scripts are inside a signed pack, so tuning a
value means editing the file, cooking the scenario and restarting the server.
Nothing watches a file and there is no second content channel beside the pack;
what a run used is what the pack held, and the pack's own hash names it. If live
tuning is wanted again, the way back is an overlay file system that lets files on
disk shadow files inside the pack (a decision for then, and it would also have to
say how a running simulation and its clients adopt a new value).

## Considered Options

- **A loose script the server watches and reloads live**: a value is tuned and seen at once, but it is a second content channel
  beside the signed pack, unsigned and not versioned with the scenario it tunes,
  and the reload needs a watcher, a swap between ticks, numbered generations, a
  reliable update message and the client's adoption and replay of it. Dropped for
  now in favour of the pack; see above for the way back.
- **Keep the tick rate in the Parameters script**: one file for every shared
  value, but the one value that cannot change needs a refusal path to forbid what
  a reloadable file allows, and it is needed before the map is loaded.
- **Keep the values in YAML** (ADR-0034's format): a value cannot be derived from
  others, so a ballistic coefficient or an energy is computed by hand and kept in
  step by hand. Startup settings stay in YAML (ADR-0034), read once; they are not
  parameters.
- **Evaluate in asset cooking** (ADR-0030) and ship plain values in the pack:
  simplest at runtime and the client needs no Lua, but the cooker (Python) would
  need the interpreter and the loader's rules. The server evaluating the script it
  finds in its pack keeps one loader, and shipping evaluated values instead stays
  possible later because the loader is a pure function.
- **Call Lua from the tick** to compute a field on each use: rejected. It puts
  the interpreter in the hot path, lets a field read simulation state (so it is
  policy, not configuration), and lets client and server compute different
  numbers.
- **Other names** (Tuning, Ruleset, Definitions): rejected. Ruleset collides with
  Game policy, whose rules are also rules; Definitions suggests entities, and
  stamina is not one; Tuning names the activity, not the values.

## Consequences

- Stamina rules move from `augustad.yaml` into the script as its first values:
  the `stamina_*` keys leave the YAML files and the `augusta_config` module, and
  a `stamina_*` key left in a file is an unknown key, an error (ADR-0034). The
  Join accepted message carries them as part of `Parameters`.
- `tick_rate_hz` is a required key of `augustad.yaml` and is not in `Parameters`
  or the client's runtime `Config`. The client ticks at the rate it was sent in
  Join accepted, so its prediction thread waits for it instead of starting at a
  rate of its own.
- `augustad.yaml` has no key naming a Parameters script: it is in the pack. A
  `parameters` key left in the file is an unknown key.
- ADR-0038's message catalogue has no Parameters update, and Join accepted carries
  the tick rate and `Parameters`.
- `Parameters` are what a run's outcome depends on besides the map and the
  commands. They are part of the server pack, so the pack a server ran from names
  them: any log or record of a run that names its pack is reproducible.
- ADR-0031 gains a script asset type, and ADR-0030's cooker takes a scenario
  folder instead of a single stage.
- Whether a release build bakes the evaluated `Parameters` into the pack instead
  of the script is not decided here. The loader being a pure function keeps both
  possible.
