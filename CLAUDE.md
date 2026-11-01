# CLAUDE.md — home-radio-net

Context for Claude Code working in this repo. Read `docs/01-architecture.md` first; it is the source of truth.

## What this is

The **shared radio protocol, C library and ESPHome gateway** for three projects: `weather_station`, `smart-garden-system`, and future soil moisture monitors. One CC1101 915 MHz PHY, one frame format, AES-128-CMAC on every frame, one gateway (ESP32 + CC1101 + ESPHome).

## Code style (owner, 2026-10-31; applies to every file a change touches)

- **The code is the documentation.** Go out of the way to make code readable: descriptive names, small functions, named constants, types that say what a value is.
- **No narrative or subjective comments.** The only acceptable comments are diagrams or charts where necessary (a register or memory layout, a timing diagram, a state table).
- **snake_case is the naming standard** for functions, variables, constants, types, namespaces and files. Exceptions only where an outside interface fixes the name (e.g. RadioLib's `RadioLibHal::pinMode` overrides, ISR names from the vendor startup file).
- **Avoid acronyms that aren't standardized.** Spell the word out (`counter`, not `ctr`; `downlink`, not `dl`). Where a non-standard acronym stays (a wire field name, a library prefix such as `nl0`), put a table at the top of the file defining each one. Standardized ones (CRC, AES, CMAC, SPI, GPIO, EEPROM, RTC, RSSI, ADC, UART, PHY, FIFO, MCU) need no table.
- Third-party code (`lib/vendor/`, `lib/node_l0/vendor/`) is kept as shipped.

## Rules

- **A change here affects every project.** Bump the frame version for incompatible changes, and note in the change log which projects need updating.
- **Every frame is authenticated** (N-3). No unauthenticated message types, ever.
- **Garden actuation is timer-only:** there is no "on/open" message, only `GARDEN_RUN(zone, duration)` and `GARDEN_STOP`. Never add one.
- The shared libraries are portable **C++17** (owner, 2026-10-31: C++ wherever it can be; `platform.c`, `log.c`, `ee_ring.c` are C99 until next touched): no heap, no OS, host-testable. They run on the STM32 nodes and inside the ESP32 gateway.
- **Use known libraries over our own code** (owner, 2026-10-31): RadioLib drives the CC1101 and provides AES-128/CMAC; vendored at a pinned version.
- **Andrew makes the calls on architecture.** Proposed items stay Proposed until he accepts them; record decisions in the spec's table and change log.
