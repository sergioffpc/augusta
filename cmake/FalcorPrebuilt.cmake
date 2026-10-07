# A prebuilt vendored Falcor (ADR-0009, ADR-0025): Falcor built once, by
# .github/workflows/falcor-prebuilt.yml, and published as an asset of this
# repository's release <tag>, which a build downloads instead of building
# Falcor itself. The tag names everything the package's contents depend on: the
# submodule's commit, cmake/patches/falcor.patch, and the FALCOR_HAS_* features
# augusta_renderer compiles Falcor's headers with (an Aftermath-enabled build,
# which CI can't make, never finds one). The asset is one per build type.
#
# Layout of a package:
#   bin/                    Falcor's runtime output directory, without .pdb/.ilk
#   lib/                    Falcor.lib and Falcor's own fmt library
#   packman/                the packman headers and import libraries augusta_renderer
#                           builds against, at their paths under external/packman
#   compile_commands.json   Falcor's, for cmake/CheckFalcorFeatures.cmake
#
# Included, it defines the functions below. Run as a script (cmake -P), it makes
# a package from a Falcor built from source:
#   -DBIN_DIR=<Falcor's runtime output directory>
#   -DLIBS=<Falcor.lib and fmt's library, ';'-separated>
#   -DPACKMAN_DIR=<Falcor's external/packman>
#   -DCOMPILE_COMMANDS=<Falcor's compile_commands.json>
#   -DOUTPUT=<the .zip to write>
include_guard(GLOBAL)

# Bumped when the layout above changes, so no build reads a package of the old
# one.
set(AUGUSTA_FALCOR_PREBUILT_FORMAT 1)

# What packman/ holds, relative to external/packman.
set(
  AUGUSTA_FALCOR_PREBUILT_PACKMAN
  nanovdb/include
  rtxdi/rtxdi-sdk/include
  python/Include
  python/libs/python310.lib
  slang/include
  slang/lib/slang.lib
  slang/lib/gfx.lib
)

# Sets <out_tag> to the release tag of the package matching this Falcor
# checkout, falcor.patch and <features>, and <out_asset> to its asset name for
# <build_type>.
function(
  augusta_falcor_prebuilt_name
  falcor_dir
  patch
  features
  build_type
  out_tag
  out_asset
)
  execute_process(
    COMMAND git -C "${falcor_dir}" rev-parse HEAD
    OUTPUT_VARIABLE commit
    OUTPUT_STRIP_TRAILING_WHITESPACE
    COMMAND_ERROR_IS_FATAL ANY
  )
  file(SHA256 "${patch}" patch_hash)
  list(SORT features)
  string(SHA256 inputs "${AUGUSTA_FALCOR_PREBUILT_FORMAT};${patch_hash};${features}")
  string(SUBSTRING "${commit}" 0 12 commit)
  string(SUBSTRING "${inputs}" 0 12 inputs)
  set(${out_tag} "falcor-${commit}-${inputs}" PARENT_SCOPE)
  set(${out_asset} "falcor-${build_type}.zip" PARENT_SCOPE)
endfunction()

# Downloads <url> and extracts it into <dest>, unless an earlier configure
# already did. Sets <out_found> to whether <dest> now holds a package: false
# when there is none at <url> (or no network), and the caller builds Falcor
# from source instead.
function(augusta_falcor_prebuilt_fetch url dest out_found)
  if(IS_DIRECTORY "${dest}")
    set(${out_found} TRUE PARENT_SCOPE)
    return()
  endif()

  set(zip "${dest}.zip.part")
  file(DOWNLOAD "${url}" "${zip}" STATUS status INACTIVITY_TIMEOUT 30 TLS_VERIFY ON)
  list(GET status 0 code)
  if(NOT code EQUAL 0)
    list(GET status 1 reason)
    message(STATUS "No prebuilt Falcor at ${url} (${reason}) - building it from source")
    file(REMOVE "${zip}")
    set(${out_found} FALSE PARENT_SCOPE)
    return()
  endif()

  # Extracted beside <dest> and renamed into place, so an interrupted
  # configure leaves no <dest> that the check above would take as complete.
  file(REMOVE_RECURSE "${dest}.part")
  file(ARCHIVE_EXTRACT INPUT "${zip}" DESTINATION "${dest}.part")
  file(REMOVE "${zip}")
  file(RENAME "${dest}.part" "${dest}")
  message(STATUS "Using prebuilt Falcor from ${url}")
  set(${out_found} TRUE PARENT_SCOPE)
endfunction()

if(CMAKE_SCRIPT_MODE_FILE STREQUAL CMAKE_CURRENT_LIST_FILE)
  set(stage "${OUTPUT}.stage")
  file(REMOVE_RECURSE "${stage}")

  file(COPY "${BIN_DIR}/" DESTINATION "${stage}/bin" PATTERN "*.pdb" EXCLUDE PATTERN "*.ilk" EXCLUDE)
  file(COPY ${LIBS} DESTINATION "${stage}/lib")
  foreach(entry ${AUGUSTA_FALCOR_PREBUILT_PACKMAN})
    get_filename_component(parent "${entry}" DIRECTORY)
    file(COPY "${PACKMAN_DIR}/${entry}" DESTINATION "${stage}/packman/${parent}")
  endforeach()
  file(COPY "${COMPILE_COMMANDS}" DESTINATION "${stage}")

  file(REMOVE "${OUTPUT}")
  execute_process(
    COMMAND ${CMAKE_COMMAND} -E tar cf "${OUTPUT}" --format=zip bin lib packman compile_commands.json
    WORKING_DIRECTORY "${stage}"
    COMMAND_ERROR_IS_FATAL ANY
  )
  file(REMOVE_RECURSE "${stage}")
endif()
