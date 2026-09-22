# Data-Driven Configuration in Lua, Reloaded Live

Data-driven configuration (the tunable values of ADR-0023: weapon and ammo
numbers, stamina rules, and their like) is called **Parameters**. It is written as
a Lua script, `parameters.lua`, so a value may be an expression of other values,
and the server reloads it while running so a value can be tuned and seen at once.
It uses the same embedded Lua (sol2) and sandbox as game policy (ADR-0022), in a
Lua state of its own: the parameters script and a policy script share no globals.

**Where each kind of decision lives.** The YAML files of ADR-0034 (`augustac.yaml`,
`augustad.yaml`) exist to replace command-line arguments: where the pack is, which
key verifies it, which address to use, and the one engine setting the server needs
before it starts and can never change, its tick rate. They never hold a simulation
rule. A simulation rule is one of three things: mechanism, in C++ code; a tunable
value, in the parameters script of this ADR; or policy, in a behaviour script
(ADR-0022).

**Evaluated at load, never in the tick.** The script is run once per load and
returns a table. A pure function, `file → std::expected<Parameters, Error>`
(ADR-0033), reads it into `Parameters`, a plain immutable C++ struct. Mechanism
code reads that struct and never calls Lua, so an expression costs nothing per
tick and cannot see simulation state: the script runs outside the tick, with
nothing of the world in reach. Beyond ADR-0022's sandbox, the environment has no
`os.time`, `os.clock` or `math.random`, and an instruction limit ends a script
that does not return. The loader reads named fields and, to refuse an unknown
key, reports the first one in name order, so the table's own order cannot change
the result.

**Validated like untrusted input.** Unknown keys, missing keys, a value of the
wrong type and a number out of range are errors, as in ADR-0034: a misspelled
field never falls back to a default, which a Lua table would otherwise allow in
silence.

**Reload.** The server watches the script (debounced). A file that fails to load
or validate is logged and the running `Parameters` stay; a good one becomes the
next generation, swapped in between two ticks, before CommandIngestion, so no
phase of a tick sees two generations. The generation number is logged with every
reload (ADR-0029). Mechanism reads `Parameters` at each use, so a new generation
applies at once to everything except state that already took a value when it was
created: a projectile in flight keeps the muzzle velocity it was fired with.

**The tick rate is not a parameter.** It is fixed for the life of the server
process: every command, acknowledgement and stretch of history is counted in
ticks, so a rate changed under them would put both sides out of step. A value
that can never be reloaded does not belong in a script that is reloaded live, and
it is engine configuration the server needs before it loads the map, like the
pack and the address it listens on. It is the `tick_rate_hz` key of
`augustad.yaml` (ADR-0034), required and any finite number above zero: NFR-01's
60 Hz is what the server must sustain, measured, not a floor on the value, so a
run may go slower to be debugged. A `tick_rate_hz` left in the script is an
unknown key.

**The server is the only source.** Whatever client and server must agree on is
decided by the server and nowhere else. A client ticking at another rate than the
server it predicts against steps its physics with another delta and can never
agree with it, so the server sends the tick rate in Join accepted (ADR-0038),
once. The client has no copy of a shared value, no default and no key for it in
`augustac.yaml`; it does not start ticking its prediction until it holds the
server's tick rate and parameters, and it drops a Join accepted whose rate is not
finite and above zero.

**Clients never read the script.** The client predicts with the server's
numbers, as it already does for stamina (ADR-0038), so the server is the only
place a value is decided. The server sends the current `Parameters` in Join
accepted, and a new reliable message with the whole `Parameters` and their
generation to every client on each reload. A client decodes it like any message
(a value that fails the same range checks is dropped and logged), adopts it, and
reconciles as usual (ADR-0004): the replay of its unacknowledged commands runs
under the new values, so for a moment its prediction may differ from what the
server did under the old ones, and the next acknowledgement corrects it.

## Considered Options

- **Keep the tick rate in the Parameters script**: one file for every shared
  value, but the one value that cannot reload needs a refusal path (a reload that
  changes it is refused as a whole, and a client drops an update that carries
  another rate) to forbid what the file's purpose is to allow.
- **Keep the values in YAML** (ADR-0034's format): the reload is as easy, but a
  value cannot be derived from others, so a ballistic coefficient or an energy
  is computed by hand and kept in step by hand. Startup settings stay in YAML
  (ADR-0034), read once; they are not parameters.
- **Evaluate in asset cooking** (ADR-0030) and ship plain values in the pack:
  simplest at runtime and the client needs no Lua, but it gives up tuning a value
  while the simulation runs, which is the reason for this ADR.
- **Call Lua from the tick** to compute a field on each use: rejected. It puts
  the interpreter in the hot path, lets a field read simulation state (so it is
  policy, not configuration), and lets client and server compute different
  numbers.
- **Each side reloads the file itself**: rejected. Two processes reading two
  copies at two moments are out of step for the whole tuning session, and the
  client would show the difference as constant reconciliation.
- **Apply a generation at a named future tick** on both sides: rejected as more
  than tuning needs. Reconciliation already absorbs the difference of a
  generation adopted on arrival.
- **Other names** (Tuning, Ruleset, Definitions): rejected. Ruleset collides with
  Game policy, whose rules are also rules; Definitions suggests entities, and
  stamina is not one; Tuning names the activity, not the values.

## Consequences

- Stamina rules move from `augustad.yaml` into the script as its first values:
  the `stamina_*` keys leave the YAML files and the `augusta_config` module, and
  a `stamina_*` key left in a file is an unknown key, an error (ADR-0034). The
  Join accepted message keeps carrying them, now as part of `Parameters`.
- `tick_rate_hz` is a required key of `augustad.yaml` and leaves `Parameters`
  and the client's runtime `Config`. The server takes its rate from its config,
  and the client ticks at the rate it was sent in Join accepted, so its prediction
  thread waits for it instead of starting at a rate of its own. A reload cannot be
  refused for the tick rate, since the script no longer has one.
- ADR-0038's message catalogue gains the Parameters update, and Join accepted
  changes from "the stamina rules" to the tick rate and `Parameters`.
- `Parameters` are what a run's outcome depends on besides the map and the
  commands, so the generation is what makes a result reproducible: any log or
  record of a run names it.
- Whether a release build bakes the evaluated `Parameters` into the pack instead
  of loading the script is not decided here. The loader being a pure function
  keeps both possible.
