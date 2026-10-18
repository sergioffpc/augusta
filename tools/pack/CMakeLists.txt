# The asset cooker (ADR-0030): a Python package (src/augusta_pack), and the
# pybind11 module it wraps its mesh and texture passes with (native/), which the
# root build builds with AUGUSTA_TOOLS and the tools environment's Python. Its
# tests are the package's pytest, uv's, not ctest's.
add_subdirectory(native)
