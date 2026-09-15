add_library(augusta_renderer STATIC
  renderer.cpp
)
target_include_directories(augusta_renderer PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(augusta_renderer PUBLIC augusta_input)

if (WIN32)
  # NVIDIA Falcor (ADR-0009) is vendored as a pinned fork (ADR-0025,
  # third_party/falcor) rather than pulled in via add_subdirectory(): its
  # root CMakeLists.txt relies on ${CMAKE_SOURCE_DIR} throughout (packman
  # fetch paths, its own external/ subdirectory, ...), which would resolve
  # to *this* repo's root instead of Falcor's own if embedded, breaking its
  # build. Instead it's built standalone via its own CMakePresets.json
  # preset - "windows-ninja-msvc-augusta", a small patch to the fork
  # (ADR-0009 anticipated needing one) that pins CMAKE_POLICY_VERSION_MINIMUM
  # for its vendored (old) pybind11 copy against newer CMake, and turns on
  # CMAKE_EXPORT_COMPILE_COMMANDS so the exact flags below could be lifted
  # verbatim from a real, verified-working compile of a Falcor sample.
  set(FALCOR_DIR ${CMAKE_SOURCE_DIR}/third_party/falcor)
  set(FALCOR_PRESET windows-ninja-msvc-augusta)
  set(FALCOR_BUILD_DIR ${FALCOR_DIR}/build/${FALCOR_PRESET})
  set(FALCOR_BIN_DIR ${FALCOR_BUILD_DIR}/bin/${CMAKE_BUILD_TYPE} CACHE INTERNAL "Falcor's own build output directory")
  set(FALCOR_DIR ${FALCOR_DIR} CACHE INTERNAL "Vendored Falcor fork's root directory")

  set(FALCOR_STAMP ${CMAKE_CURRENT_BINARY_DIR}/falcor.stamp)
  add_custom_command(
    OUTPUT ${FALCOR_STAMP}
    COMMAND ${CMAKE_COMMAND} -S ${FALCOR_DIR} --preset ${FALCOR_PRESET}
    COMMAND ${CMAKE_COMMAND} --build ${FALCOR_BUILD_DIR} --config ${CMAKE_BUILD_TYPE} --target Falcor
    COMMAND ${CMAKE_COMMAND} -E touch ${FALCOR_STAMP}
    WORKING_DIRECTORY ${FALCOR_DIR}
    COMMENT "Building vendored Falcor (ADR-0009) - slow on a clean checkout"
    VERBATIM
  )
  add_custom_target(falcor_build DEPENDS ${FALCOR_STAMP})

  # Falcor.dll's own PUBLIC link/include requirements (fmt, Vulkan-Headers,
  # slang/gfx, imgui, ...) - copied verbatim from compile_commands.json /
  # the ninja link edge for Source/Samples/SampleAppTemplate, a real
  # consumer of `Falcor` built the same way this target will be, rather
  # than re-derived by hand from Source/Falcor/CMakeLists.txt's own
  # target_link_libraries/target_compile_definitions (which augusta's
  # plain IMPORTED target below can't just inherit the way a foreign
  # add_subdirectory() consumer normally would).
  add_library(falcor SHARED IMPORTED GLOBAL)
  set_target_properties(falcor PROPERTIES
    IMPORTED_LOCATION "${FALCOR_BIN_DIR}/Falcor.dll"
    IMPORTED_IMPLIB "${FALCOR_BUILD_DIR}/Source/Falcor/${CMAKE_BUILD_TYPE}/Falcor.lib"
    INTERFACE_INCLUDE_DIRECTORIES
      "${FALCOR_DIR}/Source/Falcor;\
${FALCOR_DIR}/external/fmt/include;\
${FALCOR_DIR}/external/vulkan-headers/include;\
${FALCOR_DIR}/external/imgui;\
${FALCOR_DIR}/external/imgui_addons;\
${FALCOR_DIR}/external/packman/nanovdb/include;\
${FALCOR_DIR}/external/include;\
${FALCOR_DIR}/external/packman/rtxdi/rtxdi-sdk/include;\
${FALCOR_DIR}/external/pybind11/include;\
${FALCOR_DIR}/external/packman/python/Include;\
${FALCOR_DIR}/external/packman/slang/include"
    INTERFACE_COMPILE_DEFINITIONS
      "NOMINMAX;UNICODE;_USE_MATH_DEFINES;\
FALCOR_ENABLE_ASSERTS=$<IF:$<CONFIG:Debug>,1,0>;\
FALCOR_ENABLE_PROFILER=1;FALCOR_HAS_D3D12=1;FALCOR_HAS_VULKAN=1;\
FALCOR_HAS_AFTERMATH=0;FALCOR_HAS_NVAPI=0;FALCOR_HAS_CUDA=0;\
FALCOR_HAS_D3D12_AGILITY_SDK=1;FALCOR_HAS_RTXDI=1;\
IMGUI_USER_CONFIG=\"Utils/UI/ImGuiConfig.h\""
    INTERFACE_LINK_LIBRARIES
      "${FALCOR_BUILD_DIR}/external/fmt/${CMAKE_BUILD_TYPE}/fmt.lib;\
${FALCOR_DIR}/external/packman/python/libs/python310.lib;\
${FALCOR_DIR}/external/packman/slang/lib/slang.lib;\
${FALCOR_DIR}/external/packman/slang/lib/gfx.lib"
  )
  add_dependencies(falcor falcor_build)

  target_link_libraries(augusta_renderer PUBLIC falcor)

  # augusta_math (glm) and Falcor's own fmt (external/fmt) both put an
  # `fmt/format.h` on the include path via their respective vcpkg/packman
  # installs - vcpkg's shared vcpkg_installed/.../include root (pulled in
  # transitively via augusta_input -> augusta_physics -> augusta_math)
  # happens to also carry a newer fmt than Falcor was built against, and
  # <fmt/format.h> in Falcor.h would otherwise resolve to that mismatched
  # copy. Force Falcor's own fmt to win for this target specifically.
  target_include_directories(augusta_renderer BEFORE PRIVATE "${FALCOR_DIR}/external/fmt/include")

  # Falcor's own sources build with this project-specific warning policy
  # (Source/Falcor/CMakeLists.txt); only this target's one translation unit
  # consumes Falcor's public headers, so only it needs to match.
  target_compile_options(augusta_renderer PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/wd4819 /wd4251 /wd4244 /wd4267 /wd4100 /wd4201 /wd4245 /wd4189 /wd4127 /wd4701 /wd4703 /wd4324 /wd4505 /wd4702 /wd4389 /wd4459 /wd4268>
  )
endif()
