# Fuzz targets for what arrives from outside (ADR-0013); tests/fuzz/README.md
# has the layout, how to run one, and what a crash becomes. Every target is
# built twice: with libFuzzer's driver under the linux-fuzz preset, and on every
# preset with replay_main.cpp, for ctest to replay its seeds and regression
# fixtures without the fuzzer.
option(AUGUSTA_FUZZ "Build the libFuzzer fuzz targets (the linux-fuzz preset sets it, with clang)" OFF)

# protocol::Decode, the NFR-05 attack surface.
add_executable(augusta_protocol_decode_replay protocol_decode_fuzz.cpp replay_main.cpp)
target_link_libraries(augusta_protocol_decode_replay PRIVATE augusta_protocol)
file(GLOB augusta_protocol_decode_inputs CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/corpus/protocol_decode/*
  ${CMAKE_CURRENT_SOURCE_DIR}/regressions/protocol_decode/*)
add_test(NAME augusta_protocol_decode_replay
  COMMAND augusta_protocol_decode_replay ${augusta_protocol_decode_inputs})

add_executable(augusta_protocol_decode_seeds_tests protocol_decode_seeds_test.cpp)
target_link_libraries(augusta_protocol_decode_seeds_tests PRIVATE augusta_protocol GTest::gtest GTest::gtest_main)
target_compile_definitions(augusta_protocol_decode_seeds_tests PRIVATE
  AUGUSTA_PROTOCOL_DECODE_CORPUS="${CMAKE_CURRENT_SOURCE_DIR}/corpus/protocol_decode")
gtest_discover_tests(augusta_protocol_decode_seeds_tests)
# When the wire changes on purpose, one step rewrites the seeds for review and commit:
#   cmake --build --preset <preset> --target augusta_protocol_decode_seeds
add_custom_target(augusta_protocol_decode_seeds
  COMMAND augusta_protocol_decode_seeds_tests --gtest_also_run_disabled_tests
    --gtest_filter=ProtocolDecodeSeedsTest.DISABLED_RegenerateSeeds
  COMMENT "Rewriting tests/fuzz/corpus/protocol_decode"
  VERBATIM)

# assets::Pack::Load, a pack file from outside. The target signs its input with
# the golden packs' test key itself (pack_load_fuzz.cpp), so it hashes and signs
# directly, as only augusta_assets does elsewhere.
find_package(blake3 CONFIG REQUIRED)
find_package(unofficial-sodium CONFIG REQUIRED)
set(augusta_pack_load_example_packs AUGUSTA_EXAMPLE_PACKS="${CMAKE_SOURCE_DIR}/tests/fixtures/example-packs")

add_executable(augusta_pack_load_replay pack_load_fuzz.cpp replay_main.cpp)
target_link_libraries(augusta_pack_load_replay PRIVATE augusta_assets BLAKE3::blake3 unofficial-sodium::sodium)
target_compile_definitions(augusta_pack_load_replay PRIVATE ${augusta_pack_load_example_packs})
file(GLOB augusta_pack_load_inputs CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/corpus/pack_load/*
  ${CMAKE_CURRENT_SOURCE_DIR}/regressions/pack_load/*)
add_test(NAME augusta_pack_load_replay
  COMMAND augusta_pack_load_replay ${augusta_pack_load_inputs})

add_executable(augusta_pack_load_seeds_tests pack_load_seeds_test.cpp)
target_link_libraries(augusta_pack_load_seeds_tests PRIVATE augusta_assets GTest::gtest GTest::gtest_main)
target_compile_definitions(augusta_pack_load_seeds_tests PRIVATE
  ${augusta_pack_load_example_packs}
  AUGUSTA_PACK_LOAD_CORPUS="${CMAKE_CURRENT_SOURCE_DIR}/corpus/pack_load")
gtest_discover_tests(augusta_pack_load_seeds_tests)
# After the golden packs are regenerated, one step rewrites the seeds from them:
#   cmake --build --preset <preset> --target augusta_pack_load_seeds
add_custom_target(augusta_pack_load_seeds
  COMMAND augusta_pack_load_seeds_tests --gtest_also_run_disabled_tests
    --gtest_filter=PackLoadSeedsTest.DISABLED_RegenerateSeeds
  COMMENT "Rewriting tests/fuzz/corpus/pack_load"
  VERBATIM)

if (AUGUSTA_FUZZ)
  # The preset instruments everything with -fsanitize=fuzzer-no-link; only a
  # target's own link takes libFuzzer's main.
  add_executable(augusta_protocol_decode_fuzz protocol_decode_fuzz.cpp)
  target_link_libraries(augusta_protocol_decode_fuzz PRIVATE augusta_protocol)
  target_link_options(augusta_protocol_decode_fuzz PRIVATE -fsanitize=fuzzer)

  add_executable(augusta_pack_load_fuzz pack_load_fuzz.cpp)
  target_link_libraries(augusta_pack_load_fuzz PRIVATE augusta_assets BLAKE3::blake3 unofficial-sodium::sodium)
  target_compile_definitions(augusta_pack_load_fuzz PRIVATE ${augusta_pack_load_example_packs})
  target_link_options(augusta_pack_load_fuzz PRIVATE -fsanitize=fuzzer)
endif()
