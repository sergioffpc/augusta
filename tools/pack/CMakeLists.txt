# The asset cooker (ADR-0030): a Python package (src/augusta_pack), and the
# pybind11 module it reaches native code through (native/): its mesh and
# texture passes, and augusta_assets' pack encoder, reader and key generation.
# The root build builds it with AUGUSTA_TOOLS and the tools environment's
# Python. Its tests are the package's pytest, uv's, not ctest's.
add_subdirectory(native)
