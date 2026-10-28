# Checks that an executable carries the binary hardening the root
# CMakeLists.txt compiles and links every target with, against a memory-safety
# bug that malformed input reaches (NFR-12), by what the binary itself shows: on
# Linux, BIND_NOW (-z now), the stack protector's __stack_chk_fail and, in an
# optimised build, _FORTIFY_SOURCE's checked __*_chk functions; on Windows,
# Control Flow Guard and CET shadow-stack compatibility. The flags that leave no
# such mark (stack-clash protection, -fcf-protection, libc++'s hardening mode)
# go unchecked.
#
# Run by ctest as
#   cmake -DBINARY=<executable> -DREADOBJ=<llvm-readobj> -DNM=<llvm-nm>
#         -DSYSTEM=<Linux|Windows> -DOPTIMISED=<ON|OFF> -P binary_hardening.cmake
foreach(
  variable
  IN
  ITEMS BINARY READOBJ NM SYSTEM OPTIMISED
)
  if(NOT DEFINED ${variable})
    message(FATAL_ERROR "binary_hardening.cmake: pass -D${variable}=...")
  endif()
endforeach()

set(failures "")

# The output of a tool run on BINARY, which must succeed.
function(run_tool out)
  execute_process(COMMAND ${ARGN} "${BINARY}" OUTPUT_VARIABLE output ERROR_VARIABLE error RESULT_VARIABLE result)
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "binary_hardening.cmake: ${ARGN} ${BINARY} failed (${result}):\n${error}")
  endif()
  set(${out} "${output}" PARENT_SCOPE)
endfunction()

# Fails the check, naming what, unless text matches pattern.
function(expect text pattern what)
  if(NOT text MATCHES "${pattern}")
    set(failures "${failures}\n  ${what}" PARENT_SCOPE)
  endif()
endfunction()

if(SYSTEM STREQUAL "Linux")
  run_tool(dynamic "${READOBJ}" --dynamic-table)
  expect("${dynamic}" "BIND_NOW" "no BIND_NOW in the dynamic table: linked without -z now")
  # The dynamic symbols, which a stripped binary keeps: the protector's and the
  # fortified functions are glibc's, so every one is imported.
  run_tool(symbols "${NM}" --dynamic --undefined-only)
  expect("${symbols}" "__stack_chk_fail" "no __stack_chk_fail: compiled without the stack protector")
  if(OPTIMISED)
    expect("${symbols}" "__[a-z0-9_]+_chk[@ \n]" "no __*_chk function: compiled without _FORTIFY_SOURCE")
  endif()
elseif(SYSTEM STREQUAL "Windows")
  run_tool(headers "${READOBJ}" --file-headers --coff-debug-directory)
  expect("${headers}" "IMAGE_DLL_CHARACTERISTICS_GUARD_CF" "no GUARD_CF characteristic: built without /guard:cf")
  expect(
    "${headers}"
    "IMAGE_DLL_CHARACTERISTICS_EX_CET_COMPAT"
    "no CET_COMPAT extended characteristic: linked without /CETCOMPAT"
  )
else()
  message(FATAL_ERROR "binary_hardening.cmake: no hardening to check on ${SYSTEM}")
endif()

if(failures)
  message(FATAL_ERROR "${BINARY} lacks its binary hardening:${failures}")
endif()
message(STATUS "binary_hardening: ${BINARY} is hardened")
