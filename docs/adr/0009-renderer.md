# Renderer: NVIDIA Falcor

The Windows client renders via NVIDIA Falcor (D3D12). The server is Linux-only and headless, fully decoupled from Falcor, so Falcor's platform limitations (Linux/Vulkan support is experimental) never come into play.

## Consequences

Falcor is a research/prototyping framework, not built for shipping games, and has had no commits since Jan 2025 (~20 months stale as of writing). A fork/vendor of the source is recommended to insulate against upstream abandonment (tracked as a risk in ARCHITECTURE.md).

Falcor's `Window` is inseparable from its GPU device and swapchain (`SampleApp` fuses all three, plus the main loop, into one object) rather than offering them as independent pieces. augusta therefore has no separate Window module (see ARCHITECTURE.md §7): the client-side Renderer module owns the OS window directly and pushes keyboard/mouse device events to Input, instead of a third module managing the window handle independently. Falcor also exposes no cursor-lock/hide hook (needed for continuous FPS mouselook) — expected to require a small patch to the vendored copy, same as the abandonment-insulation fork above.
