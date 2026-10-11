# Agents (ADR-0052): players a Python script controls through Intents. Their
# C++ part and the Python module wrapping it (native/), and the C++ tests of
# whole Matches over them, which need no Python (tests/).
add_subdirectory(native)

# Its C++ tests build only on request, through augusta_tests
# (tests/CMakeLists.txt), as the engine's do, and ctest runs them in a build
# that has the tool. The package's pytest beside them is uv's, not ctest's.
add_subdirectory(tests EXCLUDE_FROM_ALL)
