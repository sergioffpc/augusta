# mimalloc in place of the platform's malloc/free and new/delete, for the whole
# process, in augustac and augustad alone (ADR-0078): not in a library, whose
# every test executable would then run on it too, nor under a sanitizer, whose
# own allocator serves (AUGUSTA_SANITIZER_ALLOCATOR, the root CMakeLists.txt).
include_guard(GLOBAL)

if(NOT AUGUSTA_SANITIZER_ALLOCATOR)
  find_package(mimalloc CONFIG REQUIRED)
endif()

# Links mimalloc into executable, and defines AUGUSTA_MIMALLOC (1, or 0 under a
# sanitizer) for its main.cpp, the only code that calls it, to read its facts
# and check them (application.h). Static where the triplet links statically
# (Linux, macOS), where its malloc replaces the C library's at link time; the
# DLL on Windows, to which its mimalloc-redirect.dll redirects the CRT's malloc
# as the process starts, which only happens when mimalloc.dll is the first DLL
# the executable loads. So call it right after add_executable, before any other
# link: the first library on the link line is the first DLL in the import
# table. The Glue check (cmake/AugustaModule.cmake) lets an executable link it.
function(augusta_link_mimalloc executable)
  if(AUGUSTA_SANITIZER_ALLOCATOR)
    target_compile_definitions(${executable} PRIVATE AUGUSTA_MIMALLOC=0)
    return()
  endif()
  if(TARGET mimalloc-static)
    target_link_libraries(${executable} PRIVATE mimalloc-static)
  else()
    target_link_libraries(${executable} PRIVATE mimalloc)
  endif()
  target_compile_definitions(${executable} PRIVATE AUGUSTA_MIMALLOC=1)
endfunction()
