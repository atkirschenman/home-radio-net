# lib/rnet: frames, keys, replay (portable C99) + generated message code +
# vendored TinyCrypt (AES-128, AES-CMAC). Builds for the host (tests) and
# for any target; consumers: add_subdirectory(<home-radio-net>/lib/rnet rnet)
set(RNET_DIR ${CMAKE_CURRENT_LIST_DIR})

add_library(rnet_tinycrypt STATIC
  vendor/tinycrypt/source/aes_encrypt.c
  vendor/tinycrypt/source/cmac_mode.c
  vendor/tinycrypt/source/utils.c)
target_include_directories(rnet_tinycrypt SYSTEM PUBLIC vendor/tinycrypt/include)
target_compile_options(rnet_tinycrypt PRIVATE -w) # third-party: built as shipped

add_library(rnet STATIC src/rnet.c gen/rnet_msgs.c)
target_include_directories(rnet PUBLIC include gen)
target_link_libraries(rnet PUBLIC rnet_tinycrypt)
target_compile_options(rnet PRIVATE -Wall -Wextra -Wshadow -Werror)

if(NOT CMAKE_CROSSCOMPILING AND BUILD_TESTING)
  add_executable(test_rnet test/test_rnet.c ${HRN_UNITY_DIR}/unity.c)
  target_include_directories(test_rnet PRIVATE ${HRN_UNITY_DIR})
  target_link_libraries(test_rnet PRIVATE rnet)
  add_test(NAME rnet.frames COMMAND test_rnet)
endif()
