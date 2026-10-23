# client

`augusta_client`, `augusta_client_application`, `augusta_client_content`,
`augusta_client_frame_mapping`, `augusta_client_lobby_readiness`,
`augusta_client_net_stats`, `augusta_client_network_panel`,
`augusta_client_phases`, `augusta_client_profiling_panel`, `augusta_client_run`,
`augusta_client_run_files`, and augustac, the executable that runs them.
Client-only: the headless server never links it.

## Responsibility

The client Runtime, augustac's: it owns one of every client module and runs the
Harness live, with its config, its Main/Render thread and the wiring of the
Harness to the renderer, the HUD, input, PresentationWorld and audio, and runs
it behind the application boundary in augustac's Lifecycle, beside listing a
replay server's Replays. It decides at its edges what the client loads from its
verified pack (the scene, the characters other players bring, the map's
collision and every cue's sound) and what it loads in the Lobby before it
reports Ready, converts between the harness's, presentation's and the renderer's
types, classifies each phase's failure, and shows the connection's numbers to
Nsight Systems and, in a Development build with the debug HUD on (ADR-0074), on
the debug HUD. In a Development build it routes the window's events by Debug
interaction, to input or to the Debug panels, shows the Debug index, which
starts each tool as the `debug` section says and switches it until the client
exits, and routes each panel's control events to the area that built it. Its own
are the Profiling panel, a window of frames it marks with the NVTX range Nsight
Systems captures, and Aftermath's status as the renderer reports it, and the
Network panel, the client's connection, its traffic per message type,
Reconciliation's corrections and the interpolation buffer's slack as rates,
plots and peaks, built here because its numbers come from modules that ship in
every build (ADR-0075). The Network panel's Impairment group applies the
transport's simulated latency, jitter, loss and reorder to the client's one
connection, both ways, from sliders or a named profile, until Off or the client
exits; the server is never told, and the debug HUD's network line ends in
`IMPAIRED` while any of it is on. It starts each Run: takes its Run ID and opens
its log file beside the executable, then writes its first lines. It decides over
the files Runs leave there: their names from the Run ID, which Runs are kept,
and how the previous Run ended, which a launch reports when it was a crash.

## Not its responsibility

- What augustac's entry point (`main.cpp`) does: read the command line, then run
  the Lifecycle and conclude its outcome - Glue, tested only end to end
  (ADR-0060).
- Playing: the Harness sends commands, receives the server's state and predicts.
- The window, drawing, input, audio and what the HUD's lines say: window,
  renderer, input, audio and hud, which the Runtime only wires together.
- The Agent's Runtime: agent, under `tools/agent/native`.

## Decisions

[ADR-0005](../../docs/adr/0005-threading-model.md),
[ADR-0021](../../docs/adr/0021-client-runtime-decomposition.md),
[ADR-0027](../../docs/adr/0027-logging.md),
[ADR-0033](../../docs/adr/0033-error-handling.md),
[ADR-0043](../../docs/adr/0043-lobby-and-match-lifecycle.md),
[ADR-0051](../../docs/adr/0051-replay.md),
[ADR-0059](../../docs/adr/0059-renderer-nvrhi.md),
[ADR-0060](../../docs/adr/0060-executables-are-glue.md),
[ADR-0074](../../docs/adr/0074-debug-tools-compiled-out-of-shipping-builds.md),
[ADR-0075](../../docs/adr/0075-debug-overlays-and-panels-are-values.md).
