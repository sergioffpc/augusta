# Gameplay Scripting Language: Lua

Game policy (round lifecycle, win conditions, spawn rules) runs as Lua (MIT, lua.org reference implementation), embedded via sol2 (MIT, header-only C++ binding), in a sandboxed environment (no `io`, `os.execute`, `package.loadlib`) inside SimulationWorld's Scripts/Behaviours phase. This keeps game policy separate from mechanism code.

## Considered Options

- **LuaJIT**: considered and deferred — policy logic is low-frequency, not hot-path numeric work, so JIT performance is unnecessary, and LuaJIT's upstream is stalled (would mean depending on the OpenResty-maintained fork rather than lua.org directly).
- **Python**: already used for offline asset tooling via OpenUSD, but deliberately not reused here — it isn't designed for embedding into a 60Hz real-time tick loop (CPython overhead, GIL).
