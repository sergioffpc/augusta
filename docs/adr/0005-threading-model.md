# Threading Model

The engine is multithreaded from v1, using fixed dedicated threads rather than a generic job/task scheduler. The client runs 3 threads (Main/Render, Simulation, Network I/O); the server runs 2 (Simulation, Network I/O) — no render thread, since it's headless.
