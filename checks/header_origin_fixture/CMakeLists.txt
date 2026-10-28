# A stand-in for a build, for header_origin_fixture.cmake to check with
# header_origin.cmake: one object, compiled by the build's own compiler and
# generator, whose headers come from the fixture's own directory and from
# SYSROOT, a stand-in for the MSVC sysroot. VISUAL_STUDIO, when given, adds a
# header from there too, a stand-in for a Visual Studio install. It compiles
# with no system include path, so the compiler adds no header of its own, and
# builds an object library, so it links nothing and needs no runtime.
cmake_minimum_required(VERSION 3.25)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
project(augusta_header_origin_fixture CXX)

add_library(fixture OBJECT fixture.cpp)
target_include_directories(fixture PRIVATE "${SYSROOT}")
# clang-cl's /X leaves out %INCLUDE% and the Visual Studio it finds itself.
target_compile_options(fixture PRIVATE $<IF:$<BOOL:${MSVC}>,/X,-nostdinc>)
if(VISUAL_STUDIO)
  target_include_directories(fixture PRIVATE "${VISUAL_STUDIO}")
  target_compile_definitions(fixture PRIVATE FIXTURE_VISUAL_STUDIO)
endif()
