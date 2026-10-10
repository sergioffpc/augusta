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
  if(PYTHON_EXECUTABLE)
    set(python "${PYTHON_EXECUTABLE}")
  elseif(CMAKE_HOST_WIN32)
    set(python "${CMAKE_CURRENT_SOURCE_DIR}/tools/.venv/Scripts/python.exe")
  else()
    set(python "${CMAKE_CURRENT_SOURCE_DIR}/tools/.venv/bin/python")
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
    set(${out} "${python}" PARENT_SCOPE)
    return()
  endif()

  set(reason "${python} is not a Python 3.12 that runs here")
  if(PYTHON_EXECUTABLE)
    message(FATAL_ERROR "PYTHON_EXECUTABLE: ${reason}.")
  endif()
  message(
    WARNING
    "Leaving out the Agents' Python module (tools/agent): ${reason}. Run `uv sync` in tools/ (tools/README.md) and configure again."
  )
  set(${out} "" PARENT_SCOPE)
endfunction()
