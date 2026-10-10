# ADR-0002: Firmware toolchain, debug probe and bench tools for the STM32L0 nodes

- **Status:** Proposed (2026-10-10)
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

## Decision (proposed)

### T1: Compiler: the distro `arm-none-eabi-gcc` 13.2, version-checked; Docker for CI

- Use the **installed Ubuntu package** (GCC 13.2.rel1, newlib-nano). It's current enough for C99/C11 on Cortex-M0+, and it updates only with the OS.
- The toolchain file in `lib/node_l0/cmake/` **fails the configure step on GCC < 13** and prints the compiler version into the build's size report, so a binary can always be traced to its compiler.
- **CI** (GitHub Actions, `ubuntu-24.04` runner) installs the same package, so local and CI builds match without a container. A Dockerfile is added only if a second machine or a distro upgrade makes builds drift.
- **Considered:** Arm's own GNU Toolchain tarball (14.x/15.x) pinned by checksum: newer, identical everywhere, but one more thing to download and keep on `PATH` for no gain on an M0+. Clang/LLVM-embedded: fine compiler, but ST's startup files and most L0 examples assume GCC.

### T2: Build: CMake ≥ 3.25 + Ninja, with presets

- `CMakePresets.json` in each firmware project: `host` (gcc, `-fsanitize=address,undefined`, runs ctest), `acurite`, `ws69` (and the garden's own), each `Debug` (`-Og -g3`) and `Release` (`-Os -g`, `-flto`).
- Warnings: `-Wall -Wextra -Wshadow -Wconversion -Werror` on `core/`, `lib/rnet` and project code; vendored ST code is built with warnings off.
- Every target build prints flash/RAM use against the L051's 64 KB / 8 KB and writes `.elf`, `.hex`, `.bin` and a `.map`.

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
- **Probe, recommended: ST-LINK/V3MINIE** (~US$15). It's ST's own probe for ST parts, it has a **virtual COM port**, so the same USB cable carries the USART1 log, and it adapts to the target's 3.3 V without powering it. It has an STDC14 connector, so it needs a cable to our headers (below).
- **Alternatives:** a **NUCLEO-L053R8** (~US$15): its on-board ST-LINK/V2-1 (with VCP) can be cut free to program our boards, and the board itself is an STM32L053, close enough to the L051 to run `lib/node_l0` (LPTIM in Stop, RTC, EEPROM ring) **before the first JLC boards arrive**. That partly reopens the dropped breadboard stage (weather_station ADR-0003 D3), but cheaply. A generic ST-LINK/V2 clone (~US$5) also works with OpenOCD on the L0; no VCP, and clones vary in quality.
- **Connection to the boards:**
  - **J5 (2.54 mm 1×5):** five female-female jumper wires; works with any probe. Default for bring-up.
  - **J3 (Tag-Connect TC2030-NL):** a **TC2030-CTX-NL** cable (10-pin 1.27 mm Cortex plug, ~US$40) plus a retaining clip or hand pressure. For flashing finished boards in the housing. *To check before buying:* that the 10-pin Cortex plug seats in the V3MINIE's STDC14 socket (ST documents the 10-pin as a subset of STDC14); otherwise add ST's or Tag-Connect's adapter.
- **Provisioning (O-4):** the script writes `node_id` + key into data EEPROM through OpenOCD (its Tcl `mww` after unlocking the EEPROM with the PEKEY sequence, or `stm32lx` driver support if present in 0.12). Which method works is proven on the first board; `st-flash` is the fallback.

### T6: Serial log and bench-mode capture

- USART1 at 115200 8N1, through the V3MINIE's VCP (or any 3.3 V USB-UART such as a CP2102 module if another probe is chosen).
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
| **µA current (Stop current with LPTIM running, CC1101 sleep, average over a report cycle)** | **Nordic Power Profiler Kit II** (~US$100) | Logs current from sub-µA to mA over time, so a whole 60 s cycle (sleep + 3 TX bursts) integrates to an average. A multimeter's µA range shows only a flickering average and adds burden voltage. Joulescope is better and ~US$1000 |
| Adjustable supply 1.8–3.3 V (battery sweep, brown-out, charger FSM) | The PPK2 in source mode (0.8–5 V), or any bench supply | PPK2 covers it for the board alone; a separate supply is needed for the solar input (~5–6 V, current-limited) |
| Wind pulses 0.5–100 Hz | A NUCLEO's timer or a Raspberry Pi Pico producing open-drain pulses, or a function generator | Real cups afterwards |
| RF on air at 918.5 MHz | RTL-SDR (spec R-7) + **SDR++** or **URH** to see bursts and timing; later our own Python sniffer (`tools/rnet_py`) | `rtl_433` can be taught the frame later; not needed for bring-up |
| UART | V3MINIE VCP | See T6 |

### Install commands (proposed, owner runs them; they need sudo)

```bash
sudo apt install ninja-build openocd gdb-multiarch stlink-tools cppcheck tio pipx
```

OpenOCD's Ubuntu package installs udev rules for ST-LINK probes; replug the probe afterwards. ESPHome: `pipx install esphome` when the gateway work starts.

## Consequences

- `lib/node_l0` gets `cmake/arm-none-eabi.cmake` (with the GCC ≥ 13 check), the vendored ST files with `VERSIONS.md`, the linker script and OpenOCD configs; `.clang-format` and Unity land in this repo first and are reused by the node repos.
- GitHub Actions workflows are added to home-radio-net (generator check, `lib/rnet` tests) and to each node repo (host tests + target builds).
- Purchases for the owner (none bought yet): a probe (V3MINIE, or a NUCLEO-L053R8 if early silicon testing is wanted), jumper wires, later the TC2030-CTX-NL cable, and a PPK2 before the power-budget step.
- weather_station spec section 9 (Tooling) points here.
