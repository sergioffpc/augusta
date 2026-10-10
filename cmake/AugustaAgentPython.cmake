# Finds the Python the Agents' module (tools/agent, ADR-0052) is built against:
# the tools environment's interpreter, Python 3.12, which `uv sync` in tools/
# creates (tools/README.md), as the cooker's native modules are built against
# it (tools/pack/cpp). PYTHON_EXECUTABLE names another.
#
# Usage: augusta_find_agent_python(<out>), before project(). Sets <out> to the
# interpreter, or to "" if the tools environment has none that runs here (none
# synced, or one synced for another OS, as a macOS checkout's in the dev
# container): the module is then left out, with a warning. A PYTHON_EXECUTABLE
# that is not a Python 3.12 stops the configure, as CI names it.
include_guard(GLOBAL)

function(augusta_find_agent_python out)
  if(CMAKE_HOST_WIN32)
    set(default "${CMAKE_CURRENT_SOURCE_DIR}/tools/.venv/Scripts/python.exe")
  else()
    set(default "${CMAKE_CURRENT_SOURCE_DIR}/tools/.venv/bin/python")
  endif()
  # Named by the caller, unless it is the default this function chose last
  # time: pybind11 caches the interpreter it was given, which a later
  # configure must not take for the caller's choice.
  set(named FALSE)
  set(python "${default}")
  if(PYTHON_EXECUTABLE AND NOT PYTHON_EXECUTABLE STREQUAL "${AUGUSTA_AGENT_PYTHON_DEFAULTED}")
    set(named TRUE)
    set(python "${PYTHON_EXECUTABLE}")
  endif()

  execute_process(
    COMMAND "${python}" -c "import sys; print('%d.%d' % sys.version_info[:2])"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE version
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
  )
  if(result EQUAL 0 AND version STREQUAL "3.12")
    message(STATUS "Agents' Python module: built against ${python}")
    if(NOT named)
      set(
        AUGUSTA_AGENT_PYTHON_DEFAULTED
        "${python}"
        CACHE INTERNAL
        "The tools environment's interpreter, found by default"
      )
    endif()
    set(${out} "${python}" PARENT_SCOPE)
    return()
  endif()

  set(reason "${python} is not a Python 3.12 that runs here")
  if(named)
    message(FATAL_ERROR "PYTHON_EXECUTABLE: ${reason}.")
  endif()
  message(
    WARNING
    "Leaving out the Agents' Python module (tools/agent): ${reason}. Run `uv sync` in tools/ (tools/README.md) and configure again."
  )
  set(${out} "" PARENT_SCOPE)
endfunction()
