# lib/rnet: frames, keys, replay (C++17) + generated message code, on
# RadioLib's AES-128/CMAC. Builds for the host (tests) and any target.
# Consumers: add_subdirectory(<home-radio-net>/lib/rnet rnet)
set(RNET_DIR ${CMAKE_CURRENT_LIST_DIR})
set(RADIOLIB_DIR ${CMAKE_CURRENT_LIST_DIR}/../vendor/RadioLib/src)

# RadioLib's crypto utilities (the radio driver is in lib/node_l0 / the ESP32 build).
if(NOT TARGET radiolib_crypto)
  add_library(radiolib_crypto STATIC ${RADIOLIB_DIR}/utils/Cryptography.cpp)
  target_include_directories(radiolib_crypto SYSTEM PUBLIC ${RADIOLIB_DIR})
  target_compile_options(radiolib_crypto PRIVATE -w) # third-party: built as shipped
endif()

add_library(rnet STATIC src/rnet.cpp gen/rnet_messages.cpp)
target_include_directories(rnet PUBLIC include gen)
target_link_libraries(rnet PUBLIC radiolib_crypto)
target_compile_features(rnet PUBLIC cxx_std_17)
target_compile_options(rnet PRIVATE -Wall -Wextra -Wshadow -Wconversion -Werror)

if(NOT CMAKE_CROSSCOMPILING AND BUILD_TESTING)
  add_executable(test_rnet test/test_rnet.cpp ${HRN_UNITY_DIR}/unity.c)
  target_include_directories(test_rnet PRIVATE ${HRN_UNITY_DIR})
  target_link_libraries(test_rnet PRIVATE rnet)
  add_test(NAME rnet.frames COMMAND test_rnet)
endif()
