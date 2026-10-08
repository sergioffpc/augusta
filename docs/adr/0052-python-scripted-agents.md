# Agents: Players Scripted in Python Through Intents the Harness Runs

> Replaces augusta-swarm (ADR-0013's load test): its Scripted players become
> Agents, and its executable and config file (ADR-0034) go.

augusta-swarm plays one fixed behaviour, written in C++: wander, aim at the
nearest player, fire in Bursts. A load test that needs players to do something
else, or a player controlled by an AI, needs a change to the tool and a rebuild.
This ADR decides that a player with no one at the keyboard - an **Agent** - is
controlled by a Python script, through **Intents** the Harness carries out every
tick, so that a load test and an autonomous agent are two scripts over one API.

**A script is the program; the engine is an extension module it imports.** A
script is run with `python <script>.py` and imports `augusta_agent`, a Python
package whose native part (`augusta_agent._native`) is a pybind11 module over
the Harness. The script owns the process: its event loop (`asyncio`), its
command line (`argparse`) and whatever libraries it uses. There is no
`augusta-agent` executable and no config file: what augusta-swarm read from
`augusta-swarm.yaml` - the server, the pack and its key, the Character, the seed

- is passed to the API by the script, which takes it from wherever it likes.

**An Intent is what an Agent is doing; the Harness turns it into a Command every
tick.** A Command has to be produced at the tick rate, on the Prediction thread
(ADR-0005), from the newest Authoritative State and the Agent's own prediction.
A script cannot run there: a Python call per tick per Agent would contend for
the GIL on the thread that must not miss a tick, and an AI that takes seconds to
decide could not keep a moving target in its sights at all. So the script
chooses an Intent, and the Harness (`augusta/intent.h`, in `augusta_harness`)
carries it out each tick until the script replaces it, as the source of the
Runner's `next_command`. The Runner does not change.

An Agent has three **channels**, each holding at most one Intent, replaced on
its own:

- **Movement**: `MoveTo(point, sprint, stance)` walks in a straight line to a
  point; `Move(direction, sprint, stance)` holds a direction.
- **Aim**: `AimAt(entity)` follows a body as the newest Authoritative State
  places it, from where the Agent's prediction places its own;
  `Look(yaw, pitch)` holds a view.
- **Trigger**: `FireBursts()` holds and releases the trigger so the Recoil
  offset recovers between Bursts, and reloads when the magazine is empty;
  `HoldFire()` holds it; `Reload()` reloads once.

`Raw(Command)` takes all three channels and sends that Command every tick until
it is replaced: for a script that wants exact control and accepts that its aim
does not follow anything. Its `reload` is a rising edge, sent on one tick and
then cleared, as the server expects. A Command's Seen time is always the
Harness's, never the script's: it is how lag compensation judges a round
(ADR-0044), not a choice of the player's. The catalogue is fixed in C++ and
grows only when a script needs something it cannot compose, as ADR-0022's hooks
do.

**An Intent tells the script when it is over.** Setting one returns an awaitable
that resolves when the Intent ends: `Arrived` or `Blocked` for `MoveTo` (no
progress for about a second), `Done` for `Reload`, `TargetGone` for `AimAt` once
its target is dead or out of the Match, and `Replaced` for any of them when the
script sets another on its channel. A script sleeps until something changes
rather than polling. `MoveTo` knows nothing of the Map but its collision: there
is no navigation mesh or pathfinding, and navigation is a decision of its own
when a script needs one.

**A script observes; it is never called on a tick.** `agent.view()` returns an
immutable snapshot of what the server has told the Agent (its `ServerView`) and
of its own predicted body, and `await agent.next_event()` returns the next Shot,
Hit confirmation, Death, Match start or Match end. A script wakes on events, on
Intents ending, or on its own timers. There is no per-tick callback.

**The Runner's threads never touch Python.** The Prediction and Network I/O
threads put events and ended Intents on one queue per process, in C++. One
dispatch thread per process waits on it with the GIL released and hands each
item to the script's event loop (`call_soon_threadsafe`). A script's calls into
an Agent - setting an Intent, taking a view - only swap state guarded by a mutex
or an atomic, and never wait for a tick. A slow or stuck script can delay its
own decisions, never a tick, and no Python code runs on a native thread.

**What the API holds:**

- `load_pack(path, public_key)` verifies the client pack and loads its Map's
  collision once, raising if either fails, as augustac does before it opens a
  socket (ADR-0018). Every Agent of the script shares it.
- `Agent(server, pack, character, faults=None, auto_ready=True)` connects one
  player, under a `harness::Runner` of its own. `faults` simulates latency,
  jitter, loss and reordering on its link, as the netcode tests do
  (`augusta/faults.h`). With `auto_ready`, it reports Ready for every Roster as
  soon as it is sent, as a Scripted player did; without, the script calls
  `agent.ready()`.
- `agent.stats()` returns the Agent's NetcodeStats - what its prediction and
  fire came to - and its connection's statistics. NetcodeStats moves from
  `tools/swarm` into `augusta_harness`.
- A failed Session or Runner - refused, unreachable, connection lost - fails
  every awaitable pending on that Agent with `AgentFailed`, carrying
  `DescribeFailure`'s sentence, and sets `agent.failure`. There is no
  reconnecting.

There is no seed in the API: the Intents decide only from what they see, and
whatever a script chooses at random it draws from its own `random.Random`. There
is no verdict either: how many Match ends make a run succeed, and how long it
may take, is the script's to decide.

**Scale is processes, not the library's concern.** One process runs as many
Agents as the script starts, each with its Runner's two threads; with the tick
work in C++, the GIL is taken only when a script decides. A load test larger
than a process runs more processes, started by whatever starts them; the library
does not orchestrate across processes.

**It is built with the engine, in `tools/agent`.** The bindings and the Python
package live in `tools/agent/`, a member of the `tools/` uv workspace. The root
build builds `_native` under `AUGUSTA_TOOLS`, with pybind11 from vcpkg, against
the Python 3.12 of `tools/.venv` (`PYTHON_EXECUTABLE`), as `tools/pack/cpp`
does; it writes the module into the package's source directory, installed
editable, and builds no wheel. A module that links the engine's static libraries
must be position-independent on Linux, so `AUGUSTA_TOOLS` turns on
`CMAKE_POSITION_INDEPENDENT_CODE` for the whole build. augustac and the server
image need no Python: `augusta_harness` itself has no Python in it, and the
image builds with `AUGUSTA_TOOLS` off.

**The first scripts are a load test and a minimal example.**
`tools/agent/examples/load_test.py` replaces augusta-swarm: it connects the
scenario's Player count of Agents, each wandering at random in Python, aiming at
the nearest living other player and firing in Bursts, and exits 0 once every one
has seen the Match ends asked for, or 1 when one fails or a timeout passes,
printing each Agent's stats. `raw_hold.py` holds one `Raw` Command. Neither uses
an AI model: the API comes first, and an autonomous agent is a script on top of
it.

**Tests stay where they are.** The netcode tests and the whole Match loop
against an in-process `server::Host` stay C++, in `tools/agent/tests/`, over a
test fixture that composes the C++ Intents the way `load_test.py` does, so ctest
needs no Python. pytest covers the Python layer with the native module built,
and a smoke test plays one Match of two Agents against an in-process
`server::Host` that `augusta_agent._testing` exposes for tests only.

## Consequences

- **Scripted player and augusta-swarm go.** The Scripted player's behaviour is
  split: aiming, Bursts and reloading become Intents; choosing a target and
  wandering become the script's. `tools/swarm` - its executable, settings, YAML
  and `ScriptedPlayer` - is removed, and its tests move to `tools/agent/tests/`.
- **The C++ jobs of CI need Python.** The Windows and Linux builds set up uv and
  run `uv sync` before configuring, since `AUGUSTA_TOOLS` is on by default. The
  `tools` job keeps building `tools/pack/cpp`, which stays a separate project.
- **The Linux build with tools is position-independent.** Code that will be
  linked into a shared object can be slightly slower; augustad from the server
  image is not affected, since it builds with `AUGUSTA_TOOLS` off.
- **The module is tied to Python 3.12**, the ABI it is built against, as the
  cooker's native modules are. Moving the tools to another Python means a
  rebuild.
- **An Intent's behaviour is C++.** A script that needs a behaviour no Intent
  offers composes it from the channels at its own rate, or the catalogue grows.
- **An Agent's aim is only as fresh as the Intent.** `Raw` and `Look` do not
  follow anything; only `AimAt` tracks a target between a script's decisions.

## Considered Options

- **A Python callback for every tick**: rejected - it runs Python on the
  Prediction thread, so the GIL and a slow script delay ticks, and an AI that
  takes seconds could never drive it.
- **Only Raw Commands, with Intents written in Python**: rejected - a Python
  Intent has to rewrite the Command every tick to keep aim on a moving target,
  bringing back a per-tick Python loop per Agent, behind by however long the
  script last took.
- **A C++ executable embedding Python**: rejected - the executable would own an
  interpreter, its paths and its packages, and a script could not use `asyncio`,
  its own command line or a library's event loop as a Python program does.
- **The Intents in a module of their own, or in `tools/agent`**: rejected - the
  Harness is where anything that plays connects (CONTEXT.md), and the Intents
  are a source of the Runner's Commands, using only what `augusta_harness`
  already depends on.
- **The bindings in `src/modules/harness`**: rejected - augustac links the
  Harness, and the client must not need Python to build.
- **The bindings as a separate CMake project, as `tools/pack/cpp` is**:
  rejected - `_native` links the engine's own static libraries, which must be
  built with the same flags and dependencies, by the same build.
- **Keeping augusta-swarm's YAML for the load test**: rejected - a script
  already is its configuration, and its arguments are the script's to choose.
- **Python tests launching an augustad subprocess**: rejected - the test would
  need to find the built server and its packs; an in-process `server::Host` is
  what the C++ Match loop test already uses.
