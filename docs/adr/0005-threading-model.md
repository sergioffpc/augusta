# Threading Model

The engine is multithreaded from v1, using fixed dedicated threads rather than a generic job/task scheduler. The client runs 3 threads (Main/Render, Simulation, Network I/O); the server runs 2 (Simulation, Network I/O) — no render thread, since it's headless.

Both tick loops — the server's Simulation thread and the client's Prediction thread — keep a fixed schedule (`augusta::tick`): each tick is due one tick after the previous one was due, not one tick after it ended, so a late tick never delays the ones after it. A loop more than a few ticks behind resynchronises to now instead of running the missed ticks back to back. On Windows the client raises the system timer resolution to 1 ms (`timeBeginPeriod`) for the life of the process, since the default 15.6 ms would not let a thread sleep to a 60 Hz deadline.
