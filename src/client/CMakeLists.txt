# augustac's entry point: it runs the client Runtime (src/modules/client)
# behind the application boundary (ADR-0033, ADR-0060).
add_executable(augustac main.cpp)
target_link_libraries(
  augustac
  PRIVATE augusta_application augusta_failure augusta_version augusta_logging augusta_client_application
)
augusta_set_module_scope(augustac CLIENT_ONLY)
# augustac is Glue: its directory holds it alone, and it links only the
# application, the client Runtime, failure, logging and version - anything else
# fails the configure (ADR-0060, cmake/AugustaModule.cmake).
augusta_set_executable_glue(augustac augusta_client_application)
# The Aftermath shared libraries beside augustac, on both platforms, when the
# build has them (cmake/Aftermath.cmake).
augusta_stage_aftermath(augustac)
# What each platform's augustac needs beyond its code: its debug info, and its
# runtime folder, which a release archives (ADR-0055, ADR-0058).
if(WIN32)
  # Release writes augustac.pdb beside augustac.exe, what a released client's
  # crash or GPU crash dump is read with (ADR-0055). /DEBUG turns /OPT:REF and
  # /OPT:ICF off unless they are named, so they are: the code is Release's own.
  target_link_options(augustac PRIVATE "$<$<CONFIG:Release>:/DEBUG:FULL;/OPT:REF;/OPT:ICF>")

  # `cmake --install --component client` stages the client's whole runtime folder,
  # which a release zips (scripts/stage-client.sh): augustac.exe, the DLLs it
  # loads, and the MSVC runtime, so it starts on a Windows with no Visual C++
  # redistributable. CMake resolves the DLLs itself, as for augustad: vcpkg's, and
  # Aftermath's when the build has it (cmake/Aftermath.cmake). The renderer's
  # shaders are in augustac.exe, and the Vulkan loader is the system's, which
  # augustac loads at runtime rather than imports (ADR-0059).
  # EXCLUDE_FROM_ALL: a plain `cmake --install`, augustad's (Dockerfile,
  # `make install`), leaves it out.
  install(
    TARGETS augustac
    RUNTIME_DEPENDENCY_SET augustac_dlls
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT client EXCLUDE_FROM_ALL
  )
  set(dll_directories "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/$<$<CONFIG:Debug>:debug/>bin")
  if(AUGUSTA_AFTERMATH_DIR)
    list(APPEND dll_directories "${AUGUSTA_AFTERMATH_DIR}/lib/x64")
  endif()
  install(
    RUNTIME_DEPENDENCY_SET augustac_dlls
    PRE_EXCLUDE_REGEXES "^api-ms-" "^ext-ms-"
    POST_EXCLUDE_REGEXES "[/\\][Ss]ystem32[/\\]"
    DIRECTORIES ${dll_directories}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT client EXCLUDE_FROM_ALL
  )
  set(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS_SKIP TRUE)
  include(InstallRequiredSystemLibraries)
  install(
    PROGRAMS ${CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS}
    DESTINATION ${CMAKE_INSTALL_BINDIR}
    COMPONENT client
    EXCLUDE_FROM_ALL
  )
else()
  # Release's debug info split off into augustac.debug, as augustad's is
  # (ADR-0047), which a release publishes beside the tarball (ADR-0055). No
  # crash handler: a player's systemd-coredump keeps the core.
  augusta_split_debug_info(augustac)
  # The shared libraries the folder bundles are found beside augustac, wherever
  # it is extracted, with no launcher script setting a library path. In the
  # build tree too, so the installed augustac is the built one, byte for byte.
  set_target_properties(augustac PROPERTIES BUILD_WITH_INSTALL_RPATH ON INSTALL_RPATH "$ORIGIN")

  # `cmake --install --component client` stages the client's runtime folder,
  # which a release tars (scripts/stage-client.sh): augustac and the shared
  # libraries it links that are not the system's. vcpkg links its ports
  # statically, and the toolchain the C++ runtime (cmake/LlvmToolchain.cmake),
  # so the folder bundles only a shared library a dependency ships alone:
  # Aftermath's when the build has it. What it links from the system is held to
  # an allowlist by the staging check.
  # EXCLUDE_FROM_ALL: a plain `cmake --install`, augustad's (Dockerfile,
  # `make install`), leaves it out.
  install(
    TARGETS augustac
    RUNTIME_DEPENDENCY_SET augustac_libraries
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT client EXCLUDE_FROM_ALL
  )
  set(library_directories "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/$<$<CONFIG:Debug>:debug/>lib")
  if(AUGUSTA_AFTERMATH_DIR)
    list(APPEND library_directories "${AUGUSTA_AFTERMATH_DIR}/lib/x64")
  endif()
  install(
    RUNTIME_DEPENDENCY_SET augustac_libraries
    POST_EXCLUDE_REGEXES "^/lib/" "^/lib64/" "^/usr/lib/" "^/usr/lib64/"
    DIRECTORIES ${library_directories}
    LIBRARY DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT client EXCLUDE_FROM_ALL
  )
endif()
