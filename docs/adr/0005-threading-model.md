# Threading Model

The engine is multithreaded from v1, using fixed dedicated threads rather than a generic job/task scheduler. The client runs 3 threads (Main/Render, Simulation, Network I/O); the server runs 3 (Simulation, Network I/O, Metrics) — no render thread, since it's headless. The server's Metrics thread is prometheus-cpp's HTTP server for `/metrics` and `/livez` (ADR-0049).

Both tick loops — the server's Simulation thread and the client's Prediction thread — keep a fixed schedule (`augusta::tick`): each tick is due one tick after the previous one was due, not one tick after it ended, so a late tick never delays the ones after it. A loop more than a few ticks behind resynchronises to now instead of running the missed ticks back to back. On Windows the client raises the system timer resolution to 1 ms (`timeBeginPeriod`) for the life of the process, since the default 15.6 ms would not let a thread sleep to a 60 Hz deadline.

Both Network I/O threads — the server's and the client's — do one round of transport work (connection events, received messages), then wait about 1 ms before the next, rather than looping flat out: GameNetworkingSockets offers no wait on incoming work, and a spinning loop would hold a core for nothing between the tick rate's messages. That wait bounds both how late a received message is handled and how long stopping the thread takes.

The client's Prediction and Network I/O threads belong to the Harness (`harness::Runner`), not to the client executable: anything that plays live — the real client, an autonomous agent, a load test's clients — runs its `harness::Session` under one and supplies only each tick's command, so all of them keep the same schedule and pace their commands the same way. The client executable adds only its Main/Render thread. `harness::Session` itself owns no thread and reads no clock, so a test drives it by hand instead (ADR-0013).

## Ownership and thread boundaries

Each piece of mutable state has one owning thread, and crosses to another only in a form that cannot change under the reader:

```text
Transport callback -> transport event queue -> Network I/O owner -> immutable Server view
Input -> PredictionWorld -> Prediction State -> PresentationWorld -> Renderer / Audio
Host -> SimulationWorld -> TickResult -> replication + typed Game policy actions
```

- **Transport callbacks publish events, nothing else.** GameNetworkingSockets' connection-status callback (which runs inside `RunCallbacks`, under the networking module's handler-registry lock) publishes one minimal event — the connection, its new state, its remote address — onto a queue. It changes no state, makes no transport call and writes no log line. The Network I/O owner (`PumpEvents`) drains the queue after `RunCallbacks` returns and applies each event: the decision of what it does to the peers or the client's connection under the owner's lock, then the transport call it needs (join the poll group, close the connection) and its log line with no lock held. The decision is a plain function of the event and the current state, tested without a live connection.
- **The client reads what the server said through one immutable Server view.** The Network I/O thread owns the client's view of the server: for every message that changes it, it publishes a new `harness::ServerView` whole, and never changes one it has published. A reader on another thread takes one view and reads everything it needs from it, so what a render frame shows (the Authoritative State, the bodies it names, the match they belong to, how it ended) is of one moment, never a mix of two messages. Independent getters remain for a single value; a reader that needs several takes the view.
- **Prediction State crosses to presentation as a value.** The Prediction thread publishes each tick's Prediction State (with the one before it and the tick's schedule) under a lock, and the Main/Render thread copies it out once per frame.
- **SimulationWorld returns a TickResult.** The server's Simulation thread owns SimulationWorld; each tick returns the tick's Authoritative State, its combat events included, and the Game policy actions taken on it, already typed and validated (ADR-0022, ADR-0023). `server::Host` owns the Lobby, Match lifecycle and replication: it replicates the state and acts on the typed actions, and never interprets a policy hook's answer itself.
- **Metrics are written in place and only read across.** The Simulation and Network I/O threads update each metric where they count it: a counter or histogram is a lock-free atomic, and a value read together with others is published whole. The Metrics thread only reads them, and only when it answers a request (ADR-0049).

## Failure semantics

A runtime supervisor (`augusta::supervisor`) owns each runtime's worker threads, the one stop request every loop watches, and the first terminal error. A worker whose body throws does not terminate the process: the supervisor records the failure (which thread, what it said) if it is the first, and requests the stop; every other loop then returns at its next check. The runtime stops and joins every worker before it returns, on success, failure or exception, and returns the first failure for `main` to report and exit non-zero — the client among its other failures (refused, unreachable, connection lost), the server as its own. A stop requested from outside (the server's SIGINT/SIGTERM handler) is a single lock-free atomic store, safe from a signal handler, and is not a failure.

## Consequences

- No state is shared between threads through independent concurrent getters where the reader needs a consistent combination of values; each such boundary is one immutable value or one published copy.
- No transport SDK call runs inside a transport callback or under a lock the callback takes.
- A worker failure is reported, never a `std::terminate`.
- The server's Metrics thread is not a supervised worker: its failure is logged and does not request the stop, because an HTTP request must never stop the tick loop (ADR-0049).
