# Checks the server's platform-support contract (NFR-04): which target platforms
# augustad configures for, and as what. Linux x86-64 is where it runs in
# production; Windows x64 is a development build only (ADR-0047); every other
# target is refused at configure time rather than compiled by accident.
#
# Run by ctest as `cmake -DSOURCE_DIR=<repo root> -P server_platform.cmake`.
if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "server_platform.cmake: pass -DSOURCE_DIR=<repo root>")
endif()

include("${SOURCE_DIR}/cmake/AugustaPlatform.cmake")

set(failures "")

# Each case: what CMake reports for the target (CMAKE_SYSTEM_NAME,
# CMAKE_SYSTEM_PROCESSOR, CMAKE_SIZEOF_VOID_P) and the support it must get.
function(expect system processor pointer_size expected)
  augusta_server_platform_support(support "${system}" "${processor}" "${pointer_size}")
  if(NOT support STREQUAL expected)
    set(
      failures
      "${failures}\n  ${system} ${processor} ${pointer_size}-byte: expected '${expected}', got '${support}'"
      PARENT_SCOPE
    )
  endif()
endfunction()

expect(Linux x86_64 8 production)
expect(Linux AMD64 8 production)
expect(Windows AMD64 8 development)
expect(Windows x86_64 8 development)

# A 32-bit toolchain on a 64-bit host still reports the host's processor.
expect(Linux x86_64 4 unsupported)
expect(Windows AMD64 4 unsupported)
expect(Linux aarch64 8 unsupported)
expect(Windows ARM64 8 unsupported)
expect(Darwin arm64 8 unsupported)
expect(Darwin x86_64 8 unsupported)
expect(FreeBSD amd64 8 unsupported)

if(failures)
  message(FATAL_ERROR "server platform-support contract broken:${failures}")
endif()
message(STATUS "server_platform: contract holds")
