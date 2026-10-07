# Fails when the FALCOR_HAS_* features Falcor.dll was compiled with differ
# from the ones augusta_renderer compiles Falcor's headers with. Those macros
# add and remove members of Falcor's classes (Falcor::Device among them), so a
# difference gives the DLL and the executable different layouts for the same
# object: the executable allocates one size and the DLL writes another, which
# shows up at runtime as heap corruption, not as a build error.
#
# Run as a script (cmake -P) once Falcor is built:
#   -DCOMPILE_COMMANDS=<Falcor's compile_commands.json>
#   -DEXPECTED=<augusta_renderer's FALCOR_HAS_* definitions, ';'-separated>

if(NOT EXISTS "${COMPILE_COMMANDS}")
  message(FATAL_ERROR "No ${COMPILE_COMMANDS} to read Falcor's features from")
endif()

# Every Falcor translation unit is compiled with the same FALCOR_HAS_*
# definitions; the first one compiling the DLL itself stands for all.
file(STRINGS "${COMPILE_COMMANDS}" falcor_command REGEX "-DFALCOR_DLL " LIMIT_COUNT 1)
string(REGEX MATCHALL "-DFALCOR_HAS_[A-Z0-9_]+=[^ ]+" falcor_features "${falcor_command}")
list(TRANSFORM falcor_features REPLACE "^-D" "")
if(NOT falcor_features)
  message(FATAL_ERROR "No FALCOR_HAS_* definitions found in ${COMPILE_COMMANDS}")
endif()

set(expected_features ${EXPECTED})
list(SORT falcor_features)
list(SORT expected_features)
if(NOT falcor_features STREQUAL expected_features)
  list(JOIN falcor_features " " falcor_text)
  list(JOIN expected_features " " expected_text)
  message(
    FATAL_ERROR
    "Falcor.dll and augusta_renderer disagree on Falcor's features:\n"
    "  Falcor.dll:       ${falcor_text}\n"
    "  augusta_renderer: ${expected_text}\n"
    "Update AUGUSTA_FALCOR_FEATURES in src/modules/renderer/CMakeLists.txt to match."
  )
endif()
