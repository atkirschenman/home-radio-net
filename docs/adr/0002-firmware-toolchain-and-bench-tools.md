# ADR-0002: Firmware toolchain, debug probe and bench tools for the STM32L0 nodes

- **Status:** Accepted (owner, 2026-10-10: "let's start on the node code" after the review). Tools installed on the dev machine the same day; probe (HiLetgo ST-LINK/V2) in transit
- **Date:** 2026-10-10
- **Affects:** every node project that builds on `lib/node_l0` (weather_station, smart-garden-system, soil monitors) and this repo's generator, `lib/rnet` and gateway
- **Builds on:** weather_station ADR-0003 D1 and smart-garden-system ADR-0004 D1 (both accepted: CMake + arm-none-eabi-gcc + ST LL/CMSIS, no HAL, no RTOS), this repo's ADR-0001 (schema + stdlib-only generator) and O-4 (provisioning over SWD)

## Context

The stack is decided; what isn't decided is the concrete set of tools that build, test, flash, debug and measure it. The toolchain file and the vendored ST code live in `lib/node_l0`, so the choice is shared by every node and belongs here.

What's on the development machine (Linux Mint 22 / Ubuntu 24.04, x86-64) on 2026-10-10:

| Tool | State |
|---|---|
| `arm-none-eabi-gcc` 13.2.rel1 + newlib 4.4 (Ubuntu packages) | Installed; a Cortex-M0+ image with `nano.specs` builds and links |
| CMake 3.28, GNU Make, host gcc 13.3, Python 3.12, clang-format 18, Docker 29, git, VS Code, minicom | Installed |
| Ninja, OpenOCD, gdb-multiarch, stlink-tools, cppcheck, pipx/ESPHome | **Missing** (all in the Ubuntu 24.04 archive except ESPHome) |
| User groups | `dialout` and `plugdev` already: serial ports and USB probes work without root once udev rules exist |
| Remotes | Both repos are on GitHub (`atkirschenman/…`), so GitHub Actions CI is available |

Constraints: no breadboard stage (weather_station ADR-0003 D3), so the host tests and the first-board bring-up carry the risk; the µA budget has to be **measured**, not estimated; the boards expose SWD on a **Tag-Connect TC2030-NL** footprint (J3) and a **2.54 mm 1×5 header** (J5: 3V3, SWDIO, SWCLK, NRST, GND), plus a USART1 log header.

## Decision

### T1: Compiler: the distro `arm-none-eabi-gcc` 13.2, version-checked; Docker for CI

- Use the **installed Ubuntu package** (GCC 13.2.rel1, newlib-nano). It's current enough for C99/C11 on Cortex-M0+, and it updates only with the OS.
- The toolchain file in `lib/node_l0/cmake/` **fails the configure step on GCC < 13** and prints the compiler version into the build's size report, so a binary can always be traced to its compiler.
- **CI** (GitHub Actions, `ubuntu-24.04` runner) installs the same package, so local and CI builds match without a container. A Dockerfile is added only if a second machine or a distro upgrade makes builds drift.
- **Considered:** Arm's own GNU Toolchain tarball (14.x/15.x) pinned by checksum: newer, identical everywhere, but one more thing to download and keep on `PATH` for no gain on an M0+. Clang/LLVM-embedded: fine compiler, but ST's startup files and most L0 examples assume GCC.

### T2: Build: CMake ≥ 3.25 + Ninja, with presets

- `CMakePresets.json` in each firmware project: `host` (gcc, `-fsanitize=address,undefined`, runs ctest), `acurite`, `ws69` (and the garden's own), each `Debug` (`-Og -g3`) and `Release` (`-Os -g`, `-flto`).
- Warnings: `-Wall -Wextra -Wshadow -Wconversion -Werror` on `core/`, `lib/rnet` and project code; vendored ST code is built with warnings off.
- Every target build prints flash/RAM use against the L051's 64 KB / 8 KB and writes `.elf`, `.hex`, `.bin` and a `.map`.

**Amended 2026-10-10 (weather_station ADR-0003 D1 amendment, owner):** node *application* code may be C++17 (weather_station is). The toolchain file sets `-fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-use-cxa-atexit` for C++; no heap, no iostream. `lib/rnet` and `lib/node_l0` stay C99 (they also run in the ESP32 gateway and the garden/soil nodes), with `extern "C"` guards in every public header.

### T3: ST code: vendored from ST's per-component GitHub repos, pinned by tag

Vendored as plain files into `lib/node_l0/vendor/` with a `VERSIONS.md` (repo, tag, licence), not as submodules, so a build never needs network access:

| What | Source | Licence |
|---|---|---|
| CMSIS-Core (Cortex-M0+ headers) | `ARM-software/CMSIS_5` `CMSIS/Core/Include` | Apache-2.0 |
| STM32L0 device headers, GCC startup file | `STMicroelectronics/cmsis_device_l0` | Apache-2.0 |
| LL drivers (`stm32l0xx_ll_*.c/.h`) | `STMicroelectronics/stm32l0xx_hal_driver` (LL files only; HAL not copied) | BSD-3-Clause |

The **linker script is our own** (64 KB flash, 8 KB RAM, data EEPROM at 0x0808_0000 as a separate `NOLOAD` region so a firmware flash never erases provisioning or counters).

### T4: Host tests: Unity + ctest, sanitizers on

- **Unity** (ThrowTheSwitch, MIT, one `.c` + two headers, vendored) for assertions; each test file is one ctest executable.
- Host builds run under AddressSanitizer + UBSan, so out-of-range indexing and signed overflow in fixed-point code fail the test rather than passing silently.
- Python side (`tools/rnetgen`, `tools/rnet_py`): stdlib `unittest`, so the generator stays dependency-free (ADR-0001).
- **CI:** generator `--check`, host ctest for `lib/rnet`, and in each node repo, host ctest plus target builds of every preset.
- **Considered:** CppUTest/GoogleTest (C++ harness for C code; heavier); a home-made assert macro (fine, but Unity's failure messages and fixtures cost nothing).

### T5: Debug probe: ST-LINK, via OpenOCD + GDB

- **Software:** **OpenOCD 0.12** (Ubuntu package; `interface/stlink.cfg` + `target/stm32l0.cfg`) and **gdb-multiarch**, with VS Code **Cortex-Debug** as the optional front end (launch configs committed). `stlink-tools` (`st-flash`, `st-info`) installed as a fallback and for quick probe checks.
- **Probe: the owner's HiLetgo ST-LINK/V2 (clone), already on hand** (owner, 2026-10-10). OpenOCD (`interface/stlink.cfg`) and `st-flash` support it on the L0. No purchase.
- **What a clone means in practice:**
  - **No virtual COM port:** the USART1 log needs a separate 3.3 V USB-UART (T6).
  - **Don't connect its 3.3V pin** while the board runs from its battery: it's a supply output, not a sense input, and would fight the TPS63900 output. Wire **SWDIO, SWCLK, GND and RST** only.
  - **Its RST pin is not reliable on every clone** (on some it is only wired for STM8 SWIM). If "connect under reset" fails, OpenOCD falls back to `reset_config none`, and the firmware covers it (next bullet).
  - **Low-power firmware vs SWD:** a running L0 that is in Stop mode drops the debug connection. So **Debug builds set `DBGMCU_CR.DBG_STOP`** (debug stays alive in Stop; costs current, irrelevant on the bench), and **every build waits ~2 s after reset before its first Stop entry**, so the probe can always attach by power-cycling the board.
  - ST's firmware updater may refuse a clone, or may brick it; **don't update it** unless OpenOCD says the firmware is too old.
- **Connection to the boards:**
  - **J5 (2.54 mm 1×5):** four female-female jumper wires from the clone's 2×5 header (pins as printed on its case: SWDIO, SWCLK, GND, RST; 3.3V left off). Default for bring-up and for the provisioning script.
  - **J3 (Tag-Connect TC2030-NL):** not used with the clone (its 2×5 2.54 mm header has a non-standard pinout, so no Tag-Connect cable fits). Revisit only if flashing boards in their housings gets awkward.
- **Upgrade path if the clone misbehaves:** an ST-LINK/V3MINIE (~US$15, with a virtual COM port) or a NUCLEO-L053R8 (~US$15; its on-board ST-LINK/V2-1 can program external boards, and its STM32L053 can run `lib/node_l0` before the JLC boards arrive).
- **Provisioning (O-4):** the script writes `node_id` + key into data EEPROM through OpenOCD (its Tcl `mww` after unlocking the EEPROM with the PEKEY sequence, or `stm32lx` driver support if present in 0.12). Which method works is proven on the first board; `st-flash` is the fallback.

### T6: Serial log and bench-mode capture

- USART1 at 115200 8N1 through a **3.3 V USB-UART adapter** (CP2102, CH340 or FT232 module, ~US$5; **3.3 V logic**, TX/RX/GND only, its power pin left off). **Still open:** whether the owner has one.
- **Fallback with no adapter: SEGGER RTT** through the ST-LINK. OpenOCD 0.12 serves RTT channels over a TCP port with any probe; it needs the debugger attached and `DBG_STOP` set, so it suits bring-up, not long bench-mode runs.
- Terminal: **`tio`** (auto-reconnect when the board resets, timestamps); minicom is already installed as a fallback.
- Bench-mode streams (weather_station ADR-0003 D9) are captured with a small Python script using **pyserial** in the project venv.

### T7: Python environment

- One `venv` per repo (`python3 -m venv .venv`), not checked in, with a `requirements-dev.txt`: `pyserial`, `pytest` (optional; the generator itself stays stdlib-only).
- **ESPHome** for the gateway via **pipx** (`pipx install esphome`, version pinned in this repo's README), kept out of the firmware venvs.

### T8: Code style and static checks

- One `.clang-format` (LLVM base, 4-space indent, 100 columns) in this repo, copied into each node repo; vendored code excluded.
- **cppcheck** run in CI on `core/`, `lib/rnet` and `lib/node_l0` (not vendored code); warnings reviewed, not blocking at first.

### T9: Bench instruments for bring-up (weather_station ADR-0003 step 5, garden bench stage)

| Need | Recommended | Why / alternative |
|---|---|---|
| **µA current** | **Deferred** (owner, 2026-10-10): not measured during bring-up. If battery life or charging looks wrong, use the owner's INA219 module with its shunt changed (fallback plan below) | A Nordic PPK2 (~US$100) is parked: it would only be bought if the INA219 method proves too coarse |
| Adjustable supply 1.8–3.3 V (battery sweep, brown-out, charger FSM) | Any bench supply, or the NiMH pack itself | A separate current-limited ~5–6 V supply for the solar input |
| Wind pulses 0.5–100 Hz | A NUCLEO's timer or a Raspberry Pi Pico producing open-drain pulses, or a function generator | Real cups afterwards |
| RF on air at 918.5 MHz | RTL-SDR (spec R-7) + **SDR++** or **URH** to see bursts and timing; later our own Python sniffer (`tools/rnet_py`) | `rtl_433` can be taught the frame later; not needed for bring-up |
| UART | 3.3 V USB-UART adapter (RTT through the ST-LINK as fallback) | See T6 |

#### Fallback: current measurement with the INA219 (only if a power problem shows up)

The INA219's shunt-voltage register has a fixed 10 µV LSB. With the module's stock 0.1 Ω shunt that is **100 µA per count**, useless for a µA budget. Changing the shunt fixes the resolution:

| Shunt | LSB | Full scale | Uncalibrated offset (10–100 µV) | Drop at ~18 mA (TX burst) | Use |
|---|---|---|---|---|---|
| 0.1 Ω (stock) | 100 µA | 3.2 A | 0.1–1 mA | 2 mV | Nothing here |
| **10 Ω**, PGA ±320 mV | **1 µA** | 32 mA | 1–10 µA | 0.18 V (fine: TPS63900 runs down to 1.8 V) | **Whole report cycle, radio on** |
| **100 Ω**, PGA ±40 mV | **0.1 µA** | 0.4 mA | 0.1–1 µA | would collapse the rail | **Sleep floor, radio disabled** |

Method:

- **Rework the module:** remove its 0.1 Ω (R100) and wire the shunt across VIN+/VIN−, high side in the battery lead to J101. A jumper (or a switch) **shorts the shunt during power-up**: the LSE start and boot draw mA, which would sag the rail through 100 Ω.
- **Zero the offset before each run:** read the shunt register with the load disconnected and subtract it. That takes the 10 Ω range to about ±1 µA and the 100 Ω range to about ±0.1 µA.
- **Sample continuously with 128× hardware averaging** (68 ms per result). Conversions run back to back, so summing the results over a whole 60 s cycle integrates the TX bursts (~11 ms each) as well as the sleep current. Bus voltage comes from the same chip, giving energy as well as charge.
- **Two runs per firmware build:**
  - 10 Ω, normal firmware → average current over several cycles (the budget number);
  - 100 Ω, a bench build with the radio kept asleep → the Stop-mode floor with LPTIM and the 1 Hz wakeup (the "single-digit µA" check).
- **What this can't do:** show the shape of a single burst (68 ms windows). Burst length comes from the RTL-SDR, and the CC1101 TX current from the datasheet cross-checked against the 10 Ω average.
- **Reader:** any I²C host logging to a CSV. A small script lands in `tools/` with the bring-up work. Which host reads it is chosen if the fallback is ever used.
- **Parts:** 10 Ω and 100 Ω 0.1 % resistors (1 % is fine once the offset is zeroed; the tolerance is a gain error of the same size), a 2-pin jumper.

### Install commands (done 2026-10-10)

```bash
sudo apt install ninja-build openocd gdb-multiarch stlink-tools cppcheck tio pipx
```

OpenOCD's Ubuntu package installs udev rules for ST-LINK probes; replug the probe afterwards. ESPHome: `pipx install esphome` when the gateway work starts.

## Consequences

- `lib/node_l0` gets `cmake/arm-none-eabi.cmake` (with the GCC ≥ 13 check), the vendored ST files with `VERSIONS.md`, the linker script and OpenOCD configs; `.clang-format` and Unity land in this repo first and are reused by the node repos.
- GitHub Actions workflows are added to home-radio-net (generator check, `lib/rnet` tests) and to each node repo (host tests + target builds).
- Purchases for the owner: **none required.** The HiLetgo ST-LINK/V2 is the probe; a 3.3 V USB-UART adapter (~US$5) only if the owner doesn't have one; jumper wires. No current meter (deferred, see T9).
- weather_station spec section 9 (Tooling) points here.
