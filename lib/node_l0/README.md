# lib/node_l0: shared STM32L0 node platform

Used by every node project (weather_station, smart-garden-system, soil monitors) through the home-radio-net submodule. Decided in smart-garden-system ADR-0004 D2; toolchain in `docs/adr/0002-firmware-toolchain-and-bench-tools.md`.

| Part | Files | Tested |
|---|---|---|
| Toolchain file, firmware helper (`node_l0_firmware()`: linker script, .map/.hex/.bin, size, compiler check) | `cmake/` | Target build |
| Linker script: 64 KB / 8 KB, data EEPROM as a NOLOAD region | `ld/stm32l051x8.ld` | Target build |
| EEPROM wear-levelled ring (pure logic, torn-write safe) | `include/node_l0/ee_ring.h`, `src/ee_ring.c` | Host: `test/test_ee_ring.c` |
| Clocks (MSI + LSE), RTC 1 Hz wakeup + 30.5 µs timestamps, Stop, IWDG, reset cause, data EEPROM backend, delay | `include/node_l0/platform.h`, `src/platform.c` | Compile only until a board/probe exists |
| TX-only UART log | `include/node_l0/log.h`, `src/log.c` | Compile only |
| ST LL / CMSIS / startup (vendored, pinned) | `vendor/` (`VERSIONS.md`) | — |
| **To come:** CC1101 driver with the generated PHY registers, rnet glue (counter persistence, `RX_OPEN` window) | | Waits for `tools/rnetgen` |

The libraries are C99; every public header has `extern "C"` guards, so C++ node applications (weather_station) include them directly. The toolchain file also sets the C++ flags (`-fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-use-cxa-atexit`).

Rules every image follows (ADR-0002 T5): Debug builds call `nl0_lowpower_init(true)` (SWD alive in Stop), and `main` waits ~2 s before the first Stop so a probe can attach after a power cycle.

Build from the repo root:

```bash
cmake --preset host && cmake --build --preset host && ctest --preset host
cmake --preset l0-release && cmake --build --preset l0-release
```
