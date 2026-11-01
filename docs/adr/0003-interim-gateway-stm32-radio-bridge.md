# ADR-0003: Interim gateway: a weather board as the radio, ESPHome over UART

- **Status:** Proposed. Hardware direction from the owner (2026-11-01): "CC1101 → STM32 → ESP32 for now, until we have a purpose-made board". The details below wait for acceptance
- **Date:** 2026-11-01
- **Affects:** the gateway (`esphome/components/radio_net/`), weather_station (the bridge image runs on its board), smart-garden-system and soil monitors (their nodes talk to this gateway)
- **Builds on:** `01-architecture.md` N-4 (ESP32 + CC1101 + ESPHome), N-10 (code layout), N-12 (Home Assistant sub-devices); ADR-0001 (generated code, the gateway stub); ADR-0002 (toolchain, bench tools)
- **Changes:** N-4 for the interim gateway: the ESP32 no longer drives the CC1101. ADR-0001's stub attached to ESPHome's stock `cc1101` component; this ADR replaces that with a UART transport

## Context

Two ESP32-wired CC1101 modules failed on the bench: the first was matched for the wrong band (−96 dBm at 1 m), and the E07 wired to the ESP32 ended up with a damaged SCK input. The second assembled weather board (STM32L051 + E07-900MM10S + `lib/node_l0` driver) has received **91 of 91** link-test packets since the preamble quality threshold fix (architecture v0.14). The proven path is the weather board's radio, so the gateway uses it until a purpose-made gateway board exists.

```
  node ))) 918.5 MHz ))) E07 CC1101 ──SPI── STM32L051 ──UART 115200── ESP32 ──Wi-Fi── Home Assistant
                         └──────── weather board, radio_bridge image ───┘ └── ESPHome, radio_net ──┘
```

## Decision

### B1: The bridge is a radio modem only; crypto and state stay on the ESP32

- The STM32 forwards **every CRC-good packet** with its RSSI and LQI, and sends packets the ESP32 hands it. It never sees the master key, verifies no MIC, and keeps no per-node state.
- The ESP32 does everything section 5 and 6 give the gateway: key derivation, MIC check, replay windows, downlink queues, Home Assistant entities. When the purpose-made board replaces the bridge, only the transport changes.

### B2: UART link: text lines, hex payload

One line per event, `\n`-terminated, 115200 8N1, 3.3 V logic. Readable in any serial terminal, which matters while the gateway is on the bench.

| Direction | Line | Meaning |
|---|---|---|
| bridge → ESP32 | `received <rssi_dbm> <lqi> <hex bytes>` | one CRC-good packet |
| bridge → ESP32 | `crc_error <rssi_dbm>` | a packet failed the hardware CRC (counted on the gateway device) |
| bridge → ESP32 | `status <uptime_s> <received> <crc_errors> <schema_hash>` | every 30 s; the ESP32 marks the bridge offline after 3 missed |
| bridge → ESP32 | `ready <schema_hash>` | after reset |
| ESP32 → bridge | `send <hex bytes>` | transmit one frame now, then return to receive (downlinks, step 4) |
| bridge → ESP32 | `sent <status>` | 0 = sent; otherwise the RadioLib error code |

- No checksum on the line: the CMAC already rejects any corrupted frame, and a bad hex line is dropped and counted.
- Hex doubles the bytes; a 61-byte frame takes ≈ 11 ms at 115200 baud, inside the 120 ms downlink window (section 6.2).
- `schema_hash` in `ready`/`status` lets the ESP32 log a bridge built from a different schema.

### B3: Bridge firmware lives in weather_station, next to `receive_test`

- New image `weather_node_acurite_radio_bridge` (`firmware/node/src/radio_bridge/`). It uses the weather board's pin map and the existing `drv::radio()` driver, so it belongs with the board, not in this repo. The line format (B2) is defined here, in `docs/`, as the contract.
- Receives continuously, with the IWDG running. No Stop mode: the gateway is mains powered.

### B4: ESPHome `radio_net` component on a swappable transport

- `esphome/components/radio_net/` as in N-10, with a small `radio_transport` interface: `uart_bridge_transport` now (an ESPHome `uart:` device), a direct `cc1101` transport later.
- `lib/rnet` builds inside ESPHome as a PlatformIO library (a `library.json` in `lib/rnet`, referenced by path), with RadioLib 7.8.1 for AES/CMAC, the same version as the nodes. No copied sources.
- Entities come from the generated tables (ADR-0001: `home_assistant` metadata in the schema → `esphome/components/radio_net/gen/`).
- ESPHome pinned at **2026.10.1** (installed 2026-11-01 with `pipx`).

### B5: Wiring and power

| Weather board (bridge) | ESP32 DevKitC |
|---|---|
| USART1 TX (PA9, log header) | `GPIO16` (UART2 RX) |
| USART1 RX (PA10, log header) | `GPIO17` (UART2 TX) |
| GND | `GND` |

- **Power: open question Q1 below.** Until it's answered, the board stays on its own batteries.
- The board's **BOOT0 fault** (it starts ST's ROM bootloader after power-up, so it only runs when started over SWD) must be fixed first. An unattended gateway has to start on its own after a power cut.

## Work order

| # | Step | ✋ |
|---|---|---|
| 1 | Fix BOOT0 on the bridge board (pin 44 → 10 kΩ R11 → GND) | owner, bench |
| 2 | `radio_bridge` image (B2, B3); check it by hand in a serial terminal with the node on its link test and on the real image | |
| 3 | ESPHome component stub: UART transport, parse, MIC, replay, log; gateway device entities (frames received, rejected, bridge online). Done when `esphome compile` builds and the log on the bench ESP32 shows verified frames | ✋ before entities |
| 4 | Entities from the generated tables for `HEALTH` and `WEATHER_REPORT`; one weather station shows up in Home Assistant | |
| 5 | Downlinks: `RX_OPEN` window, `TIME`, `CONFIG_SET`, using `send` | |

## Open questions

| # | Question | Leaning |
|---|---|---|
| Q1 | How is the bridge board powered? (a) its own 2-cell pack; (b) the ESP32's 3V3 into the board's 3V3 (J5 pin 1) with no batteries fitted; (c) a USB supply into the battery pads | (b), once the schematic confirms that driving 3V3 with the buck-boost unpowered back-feeds nothing; (a) on the bench until then |
| Q2 | Master key for the bench: the published test key until provisioning (O-4) exists? | Yes; the real key goes in `secrets.yaml` with provisioning |

## Considered

- **ESP32 + CC1101 module, stock ESPHome `cc1101` component** (ADR-0001's stub): still the target for a purpose-made board. The component supports our PHY (GFSK, 30/32 sync, variable length, whitening, CRC, and PQT stays at the reset value 0). But two bench modules failed, and its register set would need checking against RadioLib's on air.
- **Binary framing (COBS/SLIP) on the UART:** compact, but unreadable in a terminal; bandwidth isn't the limit.
- **Bridge verifies MICs:** it would need the master key on a second device for no gain.

## Consequences

- `01-architecture.md` N-4 notes the interim bridge; section 8 adds the transport interface and `lib/rnet/library.json`.
- weather_station gains one more bench image and a wiring note; nothing changes on air.
