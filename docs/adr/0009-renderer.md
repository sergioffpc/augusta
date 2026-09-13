# Renderer: NVIDIA Falcor

The Windows client renders via NVIDIA Falcor (D3D12). The server is Linux-only and headless, fully decoupled from Falcor, so Falcor's platform limitations (Linux/Vulkan support is experimental) never come into play.

## Consequences

Falcor is a research/prototyping framework, not built for shipping games, and has had no commits since Jan 2025 (~20 months stale as of writing). A fork/vendor of the source is recommended to insulate against upstream abandonment (tracked as a risk in ARCHITECTURE.md).
