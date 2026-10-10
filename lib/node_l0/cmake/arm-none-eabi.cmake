# Toolchain file for the STM32L0 nodes (home-radio-net ADR-0002 T1).
# Use: cmake -DCMAKE_TOOLCHAIN_FILE=<home-radio-net>/lib/node_l0/cmake/arm-none-eabi.cmake
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(CMAKE_C_COMPILER   arm-none-eabi-gcc)
set(CMAKE_CXX_COMPILER arm-none-eabi-g++)
set(CMAKE_ASM_COMPILER arm-none-eabi-gcc)
set(CMAKE_OBJCOPY      arm-none-eabi-objcopy CACHE FILEPATH "")
set(CMAKE_SIZE         arm-none-eabi-size    CACHE FILEPATH "")

# The compiler can't link a test program without our linker script.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(NODE_L0_CPU_FLAGS "-mcpu=cortex-m0plus -mthumb")
set(CMAKE_C_FLAGS_INIT   "${NODE_L0_CPU_FLAGS} -ffunction-sections -fdata-sections -fno-common")
# C++ for node application code (weather_station ADR-0003 D1 amendment): no
# exceptions, no RTTI, no locking around function-local statics, no atexit
# registration (nothing is ever destroyed on a node). Shared libraries stay C99.
set(CMAKE_CXX_FLAGS_INIT
    "${NODE_L0_CPU_FLAGS} -ffunction-sections -fdata-sections -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-use-cxa-atexit")
set(CMAKE_ASM_FLAGS_INIT "${NODE_L0_CPU_FLAGS} -x assembler-with-cpp")
set(CMAKE_EXE_LINKER_FLAGS_INIT
    "${NODE_L0_CPU_FLAGS} --specs=nano.specs --specs=nosys.specs -Wl,--gc-sections -Wl,--print-memory-usage")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
