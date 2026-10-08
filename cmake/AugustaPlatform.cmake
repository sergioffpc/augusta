# The server's platform-support contract (NFR-04): augustad runs in production
# on Linux x86-64 and builds on Windows x64 (ADR-0047) and Linux arm64 - the dev
# container's on an arm64 host - for development only. Any other target -
# another OS, another architecture, a 32-bit toolchain - is refused when the
# build is configured, so it can't compile by accident and pass for supported.
#
# Usage: augusta_require_server_host() before project() and
# augusta_require_server_platform() right after it, both in the root
# CMakeLists.txt. augusta_server_platform_support() is the decision on its own,
# which tests/server_platform.cmake checks.
include_guard(GLOBAL)

# Sets <out_var> to production, development or unsupported for a target
# described the way CMake describes it: CMAKE_SYSTEM_NAME,
# CMAKE_SYSTEM_PROCESSOR and CMAKE_SIZEOF_VOID_P. The processor alone can't
# tell a 32-bit build from a 64-bit one - a 32-bit toolchain on a 64-bit host
# reports the host's - so the pointer size decides that. Linux names x86-64
# x86_64 and Windows AMD64; either spelling counts on both. Linux names arm64
# aarch64, and arm64 too.
function(augusta_server_platform_support out_var system processor pointer_size)
  string(TOLOWER "${processor}" processor)
  set(support unsupported)
  if(pointer_size EQUAL 8)
    if(processor MATCHES "^(x86_64|amd64)$")
      if(system STREQUAL "Linux")
        set(support production)
      elseif(system STREQUAL "Windows")
        set(support development)
      endif()
    elseif(processor MATCHES "^(aarch64|arm64)$" AND system STREQUAL "Linux")
      set(support development)
    endif()
  endif()
  set(${out_var} ${support} PARENT_SCOPE)
endfunction()

function(_augusta_refuse_server_platform target)
  message(
    FATAL_ERROR
    "augustad does not support ${target}. Supported server platforms: Linux "
    "x86-64 (production), and Windows x64 and Linux arm64 (development only) - "
    "see docs/REQUIREMENTS.md NFR-04."
  )
endfunction()

# Before project() only the host's OS is known, but a native build on any other
# OS is already decided: refusing it here spares installing every vcpkg
# dependency - whose own failure would say nothing about why - first. A
# cross-compile (CMAKE_SYSTEM_NAME set by its toolchain) is left to
# augusta_require_server_platform().
function(augusta_require_server_host)
  if(NOT DEFINED CMAKE_SYSTEM_NAME AND NOT CMAKE_HOST_SYSTEM_NAME MATCHES "^(Linux|Windows)$")
    _augusta_refuse_server_platform("${CMAKE_HOST_SYSTEM_NAME}")
  endif()
endfunction()

# Stops the configure with what is supported when this build's target isn't.
function(augusta_require_server_platform)
  augusta_server_platform_support(support "${CMAKE_SYSTEM_NAME}" "${CMAKE_SYSTEM_PROCESSOR}" "${CMAKE_SIZEOF_VOID_P}")
  if(support STREQUAL "unsupported")
    math(EXPR bits "${CMAKE_SIZEOF_VOID_P} * 8")
    _augusta_refuse_server_platform("${CMAKE_SYSTEM_NAME} ${CMAKE_SYSTEM_PROCESSOR} (${bits}-bit)")
  endif()
  message(STATUS "augustad: ${CMAKE_SYSTEM_NAME} ${CMAKE_SYSTEM_PROCESSOR} is a ${support} server platform")
endfunction()
