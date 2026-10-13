# lib/node_l0: shared STM32L0 node platform (garden ADR-0004 D2, ADR-0002).
# Consumers: add_subdirectory(<home-radio-net>/lib/node_l0 node_l0)
#   host build   -> node_l0_portable (pure logic) + tests
#   target build -> node_l0_vendor (ST code) + node_l0 (platform) + node_l0_firmware()

set(NODE_L0_DIR ${CMAKE_CURRENT_LIST_DIR} CACHE INTERNAL "")
include(${CMAKE_CURRENT_LIST_DIR}/cmake/node_l0.cmake)

add_library(node_l0_portable STATIC src/ee_ring.c)
target_include_directories(node_l0_portable PUBLIC include)

if(CMAKE_CROSSCOMPILING)
  node_l0_check_compiler()
  enable_language(ASM)

  set(V ${CMAKE_CURRENT_LIST_DIR}/vendor)
  file(GLOB ll_src ${V}/ll/Src/*.c)
  add_library(node_l0_vendor STATIC
    ${ll_src}
    ${V}/device/Source/system_stm32l0xx.c
    ${V}/device/Source/startup_stm32l051xx.s)
  target_include_directories(node_l0_vendor SYSTEM PUBLIC
    ${V}/cmsis/Include ${V}/device/Include ${V}/ll/Inc)
  target_compile_definitions(node_l0_vendor PUBLIC STM32L051xx USE_FULL_LL_DRIVER)
  target_compile_options(node_l0_vendor PRIVATE -w)

  add_library(node_l0 STATIC src/platform.c src/log.c)
  # Linked as an object into every image, so it wins over libnosys.
  target_sources(node_l0 INTERFACE ${CMAKE_CURRENT_LIST_DIR}/src/syscalls.c)
  target_link_libraries(node_l0 PUBLIC node_l0_portable node_l0_vendor)
  target_compile_options(node_l0 PRIVATE -Wall -Wextra -Wshadow -Werror)
elseif(BUILD_TESTING)
  add_executable(test_ee_ring test/test_ee_ring.c ${HRN_UNITY_DIR}/unity.c)
  target_include_directories(test_ee_ring PRIVATE ${HRN_UNITY_DIR})
  target_link_libraries(test_ee_ring PRIVATE node_l0_portable)
  add_test(NAME node_l0.ee_ring COMMAND test_ee_ring)
endif()
