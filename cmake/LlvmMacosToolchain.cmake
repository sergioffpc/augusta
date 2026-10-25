# The macOS toolchain, Homebrew's LLVM (ADR-0008), for the development build
# there: clang compiles against LLVM's libc++, lld links, and LLVM's binary
# tools stand in for Apple's - not Xcode's or the Command Line Tools' Apple
# clang, which is not the project's compiler. LLVM 21 (llvm@21, lld@21), CI's
# and Windows' version: one LLVM to bump on every platform. The SDK alone comes
# from the Command Line Tools, through the -isysroot Homebrew's clang configures
# itself with. vcpkg's toolchain chainloads it twice, as
# cmake/LlvmToolchain.cmake on Linux: for this project (the macos presets) and
# for every port (triplets/arm64-osx.cmake).
#
# libc++ links dynamically, from Homebrew's prefix, as Homebrew's LLVM documents
# it: macOS's own libc++ is older than the headers clang compiles against.

# CMake reads a toolchain more than once in a configure; the flags below are
# appended once.
include_guard(GLOBAL)

# Homebrew's versioned LLVM and lld are keg-only, off PATH.
if(DEFINED ENV{HOMEBREW_PREFIX})
  set(augusta_homebrew "$ENV{HOMEBREW_PREFIX}")
else()
  set(augusta_homebrew /opt/homebrew)
endif()
set(augusta_llvm "${augusta_homebrew}/opt/llvm@21")

set(CMAKE_C_COMPILER "${augusta_llvm}/bin/clang")
set(CMAKE_CXX_COMPILER "${augusta_llvm}/bin/clang++")

foreach(tool IN ITEMS AR RANLIB NM STRIP)
  string(TOLOWER "${tool}" name)
  find_program(CMAKE_${tool} llvm-${name} HINTS "${augusta_llvm}/bin" NO_DEFAULT_PATH REQUIRED)
endforeach()
find_program(AUGUSTA_LD64_LLD ld64.lld HINTS "${augusta_homebrew}/opt/lld@21/bin" NO_DEFAULT_PATH REQUIRED)

# Homebrew's libc++ marks what macOS's own lacks unavailable, by the deployment
# target; it is Homebrew's that links, which has all of it.
foreach(lang IN ITEMS CXX OBJCXX)
  string(APPEND CMAKE_${lang}_FLAGS_INIT " -D_LIBCPP_DISABLE_AVAILABILITY")
endforeach()

foreach(kind IN ITEMS EXE SHARED MODULE)
  string(
    APPEND CMAKE_${kind}_LINKER_FLAGS_INIT
    " --ld-path=${AUGUSTA_LD64_LLD}"
    " -L${augusta_llvm}/lib/c++ -Wl,-rpath,${augusta_llvm}/lib/c++"
    " -L${augusta_llvm}/lib/unwind -lunwind"
  )
endforeach()

# A port's build (vcpkg passes it the triplet's architecture) also gets what
# vcpkg's own macOS toolchain, which this one stands in for, sets up: the
# target processor, position-independent code and the triplet's flags.
if(DEFINED VCPKG_TARGET_ARCHITECTURE)
  include("${CMAKE_CURRENT_LIST_DIR}/../third_party/vcpkg/scripts/toolchains/osx.cmake")
endif()
