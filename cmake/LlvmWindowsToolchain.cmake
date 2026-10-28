# The Windows toolchain, the pinned LLVM's (ADR-0008, ADR-0079): clang-cl
# compiles, lld-link links, llvm-lib archives, llvm-rc compiles resources and
# llvm-mt merges manifests. vcpkg's toolchain chainloads it twice, as
# cmake/LlvmToolchain.cmake on Linux: for this project (the windows presets)
# and for every port (triplets/x64-windows.cmake), so the dependencies are
# built by the compiler our code is. The Visual Studio Build Tools' developer
# environment still gives them the MSVC STL, the CRT and the Windows SDK.

# CMake reads a toolchain more than once in a configure; the lookups below are
# made once.
include_guard(GLOBAL)

# vcpkg builds each port in an environment of its own, whose PATH is not the
# user's. The configure that finds LLVM on PATH, the project's, names its
# directory in AUGUSTA_LLVM_BIN before vcpkg installs the ports, and the triplet
# passes that variable through to their builds: every port is built by the
# LLVM the project is. PATH alone otherwise, never CMake's own search paths:
# vcpkg's tools or another LLVM installed under Program Files.
find_program(
  AUGUSTA_CLANG_CL
  clang-cl
  HINTS
  ENV AUGUSTA_LLVM_BIN
  NO_CMAKE_PATH
  NO_CMAKE_ENVIRONMENT_PATH
  NO_CMAKE_SYSTEM_PATH
  REQUIRED
)
cmake_path(GET AUGUSTA_CLANG_CL PARENT_PATH augusta_llvm_bin)
set(ENV{AUGUSTA_LLVM_BIN} "${augusta_llvm_bin}")

set(CMAKE_C_COMPILER "${AUGUSTA_CLANG_CL}")
set(CMAKE_CXX_COMPILER "${AUGUSTA_CLANG_CL}")

# Found before CMake looks for MSVC's link, lib, rc and mt, which then keeps
# these: the ones beside clang-cl, of the same LLVM.
find_program(CMAKE_LINKER lld-link HINTS "${augusta_llvm_bin}" NO_DEFAULT_PATH REQUIRED)
find_program(CMAKE_AR llvm-lib HINTS "${augusta_llvm_bin}" NO_DEFAULT_PATH REQUIRED)
find_program(CMAKE_RC_COMPILER llvm-rc HINTS "${augusta_llvm_bin}" NO_DEFAULT_PATH REQUIRED)
find_program(CMAKE_MT llvm-mt HINTS "${augusta_llvm_bin}" NO_DEFAULT_PATH REQUIRED)

# A port's build (vcpkg passes it the triplet's architecture) also gets what
# vcpkg's own Windows toolchain, which this one stands in for, sets up: the
# CRT, the MSVC flags and the triplet's. It leaves out /MP, which clang-cl does
# not take, as the compiler's path ends in clang-cl.exe.
if(DEFINED VCPKG_TARGET_ARCHITECTURE)
  include("${CMAKE_CURRENT_LIST_DIR}/../third_party/vcpkg/scripts/toolchains/windows.cmake")
endif()
