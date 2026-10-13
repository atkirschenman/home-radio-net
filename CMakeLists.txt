# home-radio-net: host tests for the shared libraries, and a target smoke build
# of lib/node_l0 (ADR-0002 T2). Presets: CMakePresets.json.
cmake_minimum_required(VERSION 3.25)
project(home_radio_net C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(HRN_UNITY_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/unity CACHE INTERNAL "")

if(NOT CMAKE_CROSSCOMPILING)
  include(CTest)
  add_compile_options(-Wall -Wextra -Wshadow -Wconversion -Werror -g
                      -fsanitize=address,undefined -fno-sanitize-recover=all)
  add_link_options(-fsanitize=address,undefined)
endif()

add_subdirectory(lib/node_l0)

if(CMAKE_CROSSCOMPILING)
  add_executable(node_l0_smoke lib/node_l0/examples/smoke/main.c)
  node_l0_firmware(node_l0_smoke)
endif()
