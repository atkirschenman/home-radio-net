# CLAUDE.md — home-radio-net

Context for Claude Code working in this repo. Read `docs/01-architecture.md` first; it is the source of truth.

## What this is

The **shared radio protocol, C library and ESPHome gateway** for three projects: `weather_station`, `smart-garden-system`, and future soil moisture monitors. One CC1101 915 MHz PHY, one frame format, AES-128-CMAC on every frame, one gateway (ESP32 + CC1101 + ESPHome).

## Rules

- **A change here affects every project.** Bump the frame version for incompatible changes, and note in the change log which projects need updating.
- **Every frame is authenticated** (N-3). No unauthenticated message types, ever.
- **Garden actuation is timer-only:** there is no "on/open" message, only `GARDEN_RUN(zone, duration)` and `GARDEN_STOP`. Never add one.
- The C library is portable C99: no malloc, no OS, host-testable. It runs on the STM32 nodes and inside the ESP32 component.
- **Andrew makes the calls on architecture.** Proposed items stay Proposed until he accepts them; record decisions in the spec's table and change log.
