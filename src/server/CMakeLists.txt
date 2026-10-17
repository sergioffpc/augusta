# augustad's entry point: it runs the server Runtime (src/modules/server)
# behind the application boundary (ADR-0033, ADR-0060).
add_executable(augustad main.cpp)
target_link_libraries(
  augustad
  PRIVATE
    augusta_application
    augusta_config
    augusta_server_config
    augusta_server_application
    augusta_server_crash
    augusta_version
    augusta_logging
    augusta_server
)
# The headless server itself, not only its libraries: a client-only module
# linked straight into it fails the configure (tests/server_boundary.cmake).
augusta_set_module_scope(augustad SERVER_ONLY)
# This directory is augustad's Glue: only augustad belongs in it, and a new
# library here fails the configure (ADR-0060, cmake/AugustaModule.cmake).
augusta_set_executable_glue(augustad)

# On Linux the debug info is split off into augustad.debug beside it (ADR-0047).
if(NOT WIN32)
  augusta_split_debug_info(augustad)
endif()

# `cmake --install` puts augustad in the prefix's bin directory (DESTDIR
# honoured), which is how the Dockerfile's image gets it. On Windows the vcpkg
# DLLs it loads go beside it, as the build tree has them; CMake resolves them
# itself, rather than vcpkg's applocal helper, so they are in the install
# manifest `make uninstall` removes. On Linux vcpkg links them statically.
if(WIN32)
  install(TARGETS augustad RUNTIME_DEPENDENCY_SET augustad_dlls RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
  install(
    RUNTIME_DEPENDENCY_SET augustad_dlls
    PRE_EXCLUDE_REGEXES "^api-ms-" "^ext-ms-"
    POST_EXCLUDE_REGEXES "[/\\][Ss]ystem32[/\\]"
    DIRECTORIES "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/$<$<CONFIG:Debug>:debug/>bin"
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  )
else()
  install(TARGETS augustad RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
