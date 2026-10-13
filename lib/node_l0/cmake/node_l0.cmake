# Helpers for node firmware projects (home-radio-net ADR-0002 T1/T2).

# The distro arm-none-eabi-gcc 13.2 is the reference compiler; refuse older ones.
function(node_l0_check_compiler)
  if(CMAKE_C_COMPILER_ID STREQUAL "GNU" AND CMAKE_C_COMPILER_VERSION VERSION_LESS 13)
    message(FATAL_ERROR "arm-none-eabi-gcc >= 13 required, found ${CMAKE_C_COMPILER_VERSION}")
  endif()
endfunction()

# node_l0_firmware(<target>): link with the L051 script, write .map/.hex/.bin,
# and print flash/RAM use plus the compiler version.
function(node_l0_firmware target)
  set(ld ${NODE_L0_DIR}/ld/stm32l051x8.ld)
  set_target_properties(${target} PROPERTIES SUFFIX ".elf" LINK_DEPENDS ${ld})
  target_link_options(${target} PRIVATE -T${ld} -Wl,-Map=$<TARGET_FILE_DIR:${target}>/${target}.map)
  target_link_libraries(${target} PRIVATE node_l0)
  add_custom_command(TARGET ${target} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex   $<TARGET_FILE:${target}> $<TARGET_FILE_DIR:${target}>/${target}.hex
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${target}> $<TARGET_FILE_DIR:${target}>/${target}.bin
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${target}>
    COMMAND ${CMAKE_COMMAND} -E echo "compiler: ${CMAKE_C_COMPILER_ID} ${CMAKE_C_COMPILER_VERSION}"
    VERBATIM)
endfunction()
