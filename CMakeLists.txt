cmake_minimum_required(VERSION 3.25)
project(augusta LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (MSVC)
  add_compile_options(/W4 /WX)
else()
  add_compile_options(-Wall -Wextra -Werror)
endif()

find_package(spdlog CONFIG REQUIRED)

enable_testing()
find_package(GTest CONFIG REQUIRED)

add_subdirectory(src/shared)
add_subdirectory(src/server)
if (WIN32)
  add_subdirectory(src/client)
endif()
add_subdirectory(tests)
