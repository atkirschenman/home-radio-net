# 01 — Home radio network: architecture

*Version 0.9, 2026-10-10. The shared 915 MHz radio architecture for every battery/solar node Andrew builds, and the one indoor gateway that bridges them to Home Assistant. Anything marked **Proposed** is not decided until the owner accepts it. Change log at the end.*

## 1. Who uses it

| Project | Repo | Node behaviour | Traffic |
|---|---|---|---|
| **Weather station** (AcuRite / WS69 brain transplants) | `~/Documents/weather_station` | Sensor uplink; NiMH + small solar; very tight energy budget (~1 mAh/day) | 1 report / 60 s, sent 2–3× |
| **Garden watering controller** | `~/Documents/smart-garden-system` | **Actuator**: polls for commands; LiFePO4 + 5 W solar | 1 poll / ~3 s; commands within ~5 s |
| **Soil moisture monitors** (not started) | — | Sensor uplink; **5+ nodes**; small solar (owner, 2026-10-03) | 1 report / 5–15 min (TBD) |
| **Gateway** | this repo (`esphome/`) | ESP32 + CC1101, ESPHome, mains powered, always receiving | All of the above |

## 2. Decisions so far

| ID | Decision | Status |
|---|---|---|
| N-1 | **One radio stack for all projects:** TI CC1101, 915 MHz, GFSK, one shared PHY configuration, one gateway | **Decided** (owner, 2026-10-03; carried over from weather_station spec 3.3 and garden spec D-5) |
| N-2 | **Shared repo** (`home-radio-net`) holds the protocol spec, a portable C library and the ESPHome gateway component. Projects consume it as a **git submodule** | **Decided** (owner, 2026-10-03) |
| N-3 | **Every frame from every node is authenticated** (AES-128-CMAC + replay counter), in both directions | **Decided** (owner, 2026-10-03) |
| N-4 | Gateway = ESP32 + 915 MHz CC1101 module + ESPHome's `cc1101` component (feasibility: `smart-garden-system/docs/03-radio-link.md`) | **Decided** (weather_station spec section 4; garden D-5) |
| N-5 – N-12 | PHY (918.5 MHz), frame format, keys, counters, MAC, message set, code layout, Home Assistant representation (sections 3–10) | **Decided** (owner, 2026-10-03) |

## 3. PHY (N-5, decided)

| Parameter | Value | Why |
|---|---|---|
| Band / channel | **915 MHz ISM, one fixed channel: 918.5 MHz** (O-1, accepted) | One channel keeps the gateway simple; low traffic (section 6.4) |
| Modulation | GFSK, **38.4 kbps**, deviation ~20 kHz, RX bandwidth ~100 kHz | weather_station spec R-2; ~10 ms per frame |
| Preamble / sync | 4 bytes / **30-of-32 sync, our own sync word** (not the CC1101 default 0xD391) | Rejects other CC1101 products on the default word |
| Packet | **Variable length** (length byte), ≤ 61 bytes payload (one FIFO), **CRC-16 on**, **whitening on** | Variable length lets every node type share one config; CRC drops corrupted frames in hardware |
| TX power | Capped per node for FCC 15.249 (~−1 dBm EIRP; the cap depends on each antenna) | Same rule as weather_station 3.2; the gateway is capped too |

## 4. Frame format (N-6, decided)

Inside the CC1101 payload, little-endian:

| Offset | Size | Field | Notes |
|---|---|---|---|
| 0 | 1 | `ctrl` | bits 7–6 version (0); bit 5 `DIR` (0 = uplink, 1 = downlink); bit 4 `ACK_REQ`; bit 3 `RX_OPEN` (uplink: "I'm listening after this frame"); bit 2 `ACK` (this frame acknowledges the peer's frame `ack_ctr`); bit 1 `MORE` (downlink: more queued for you, poll again); bit 0 reserved |
| 1 | 1 | `net_id` | Network ID: separates this home from a neighbour running the same firmware |
| 2 | 2 | `node_id` | Uplink: source. Downlink: destination. 0xFFFF reserved |
| 4 | 1 | `node_type` | 0x01 weather-AcuRite, 0x02 weather-WS69, 0x10 garden controller, 0x20 soil monitor, … |
| 5 | 4 | `ctr` | Frame counter, **per node per direction**, never reused (section 5.2) |
| 9 | 1 | `msg_type` | Section 7 |
| 10 | 0–2 | `ack_ctr` | Present only if `ACK` set: low 16 bits of the counter being acknowledged |
| … | n | payload | Fixed little-endian struct per `msg_type` (+ version byte inside where needed); fixed-point, no floats |
| end − 4 | 4 | `mic` | AES-128-CMAC over `ctrl … payload`, **truncated to 4 bytes** |

**Overhead:** 14 bytes (16 with an ack) + CC1101's own 4 preamble + 4 sync + 1 length + 2 CRC. A 30-byte payload frame is ~55 bytes on air ≈ **11.5 ms** at 38.4 kbps.

**MIC length: 4 bytes** (O-2, accepted), the same as LoRaWAN. A forger gets one guess per frame on air (≈ 2⁻³² chance each). At this link's airtime, brute force takes centuries, and replays are stopped by the counter. 8 bytes would cost 0.8 ms more per frame.

**Duplicates:** the weather station's 2–3 repeats carry the **same** `ctr` and identical bytes. The gateway accepts the first and drops the rest silently.

## 5. Security (N-3, N-7, decided)

### 5.1 Keys
- **One 128-bit master key per home**, held only by the gateway (ESPHome `secrets.yaml`).
- **Per-node key** `K_node = AES-128(master, node_id ‖ node_type ‖ "rnet" ‖ zero padding)`. The gateway derives it on the fly; each node is flashed with only **its own** key and `node_id` (a provisioning step in the build, stored in the STM32's EEPROM/option area). One leaked node key doesn't expose the others.
- **No payload encryption** (authenticity only, per N-3). Readings and valve states aren't secret. Adding AES-CTR later would be a version bump.

### 5.2 Counters and replay
- Each node keeps an **uplink counter**; the gateway keeps a **downlink counter per node**. A receiver accepts a frame only if `ctr` > last accepted (duplicates with the same counter are dropped, not errors).
- **Persistence without flash wear:** the node writes its counter to EEPROM every 256 frames. On boot it resumes at stored + 256, so a reboot never reuses a value. The gateway does the same in ESP32 NVS per node.
- At the garden's 1 frame / 3 s, a 32-bit counter lasts > 400 years.

## 6. Medium access (N-8, decided)

### 6.1 Uplink: unslotted ALOHA with jitter
Nodes transmit on their own schedule with ±10 % random jitter on the period, so two nodes never stay synchronised. No listen-before-talk; the load is too low to need it (6.4).

### 6.2 Downlink: only into a receive window the node opened ("class A" style)
- A node that sets `RX_OPEN` listens for **W = 120 ms** after its uplink (tunable per node type). The window ends early if no sync word arrives.
- The gateway keeps a **small downlink queue per node** (depth 4). It transmits only inside that node's window. One downlink per window; `MORE` pending is signalled in the downlink so the node can poll again immediately.
- This means **battery nodes choose when they can be reached**:

| Node | `RX_OPEN` | Reachable within |
|---|---|---|
| Garden controller | every poll (3 s) | ~3–5 s |
| Soil monitor | every report (O-3) | one report interval |
| Weather station | **one frame per hour**: the hourly `HEALTH` frame (weather_station ADR-0003). Its 1 mAh/day budget can't pay for a window every minute (~30 µA average) | ~1 h (config/time only) |

### 6.3 Acknowledgements
- Uplink with `ACK_REQ` → the gateway answers in the window (an ack can ride on a queued downlink). Used by the garden for state changes and faults. Sensor reports don't request acks; the weather station's repeats do the job.
- Downlink with `ACK_REQ` (every garden command) → the node sets `ACK` + `ack_ctr` in its next uplink. The gateway retries in later windows until acknowledged. After ~30 s of retries it marks the node **unreachable** in Home Assistant.

### 6.4 Channel load (sanity check)
Garden 1 × 11 ms / 3 s + weather 2 stations × 3 × 11 ms / 60 s + 10 soil nodes × 11 ms / 300 s ≈ **0.5 % channel occupancy**. Pure-ALOHA collision probability per frame ≈ 1 %. Retries and repeats absorb that. Fine up to dozens of nodes.

## 7. Message set (N-9, decided)

The authoritative layouts are in `schema/rnet.toml` (ADR-0001); this table is an overview, and `docs/generated/messages.md` will replace it once the generator runs.

| `msg_type` | Dir | Who | Payload (sketch) |
|---|---|---|---|
| 0x01 `HEALTH` | up | all | VBAT (mV), V_panel (mV), board temp (0.1 °C), last downlink RSSI, uptime, fw version, reset cause. Can be appended to any report |
| 0x02 `TIME` | down | any | Unix time (s); nodes that care use it, others ignore it |
| 0x03 `CONFIG_SET` / 0x04 `CONFIG_GET` / 0x05 `CONFIG_VAL` | down/down/up | all | TLV list (report interval, TX power cap, RX_OPEN cadence, calibration) |
| 0x10 `WX_REPORT` | up | weather | weather_station's measurement set (wind, gust, direction, rain counter, T/H, pressure) |
| 0x20 `GARDEN_POLL` | up | garden | Compact status: per-zone state, remaining run time, main state, faults, battery |
| 0x21 `GARDEN_RUN` | down | garden | zone, **duration (s, required, capped by the node)**. **No on/open message exists** (garden R-3) |
| 0x22 `GARDEN_STOP` | down | garden | zone or all |
| 0x23 `GARDEN_EVENT` | up | garden | State change / fault, `ACK_REQ` |
| 0x30 `SOIL_REPORT` | up | soil | Moisture raw + calibrated, soil temperature, … (defined with the soil project) |

## 8. Code layout (N-10, decided)

```
home-radio-net/
  docs/                 this spec + ADRs
  schema/rnet.toml      the protocol definition (ADR-0001)
  tools/rnetgen/        generator: C, gateway tables, Python codec, docs, test vectors
  tools/rnet_py/        generated Python codec, sniffer, provisioning script (O-4)
  test/vectors/         golden test vectors (JSON)
  lib/rnet/             portable C99, no malloc, no OS:
                        frame pack/parse, AES-128 + CMAC, key derivation,
                        counter/replay window, message structs
  lib/rnet/test/        host unit tests: RFC 4493 CMAC vectors, frame round-trips,
                        replay cases (runs in CI / on the PC)
  lib/node_l0/          shared STM32L0 node platform (garden ADR-0004 D2): CMake toolchain,
                        vendored ST LL/CMSIS, clocks (MSI + LSE), Stop entry/exit, RTC wakeup,
                        IWDG, data-EEPROM wear-levelled ring, CC1101 driver (generated PHY
                        registers), rnet glue (counter persistence, RX_OPEN window)
  esphome/components/radio_net/
                        ESPHome external component: wraps the cc1101 component,
                        uses lib/rnet, holds per-node state and downlink queues,
                        creates HA entities per node type, exposes actions
                        (garden_run, garden_stop, node_config)
```

- **ADR-0001 (accepted 2026-10-10):** message definitions, PHY constants and node types live in one schema (`schema/rnet.toml`). A stdlib-only generator (`tools/rnetgen/`) emits the C, gateway, Python, Markdown and test-vector code from it.
- **The same C library runs on the STM32 nodes and inside the ESP32 component**, so both ends share one implementation and one set of test vectors. Software AES costs ~1 ms per frame on a 32 MHz Cortex-M0+, which is fine.
- The component **replies from its own packet listener**, not via a YAML `on_packet` automation, to keep the downlink inside the node's 120 ms window (`smart-garden-system/docs/03-radio-link.md`).
- **Node registry** in the gateway YAML: `node_id`, `node_type`, name. Entities are created per type. Unknown nodes are logged, not exposed.

## 9. Hardware commonality (N-11, decided)

- **"Radio core"** = STM32L0 (L051C8T6 or family) + 32.768 kHz LSE + E07-900MM10S + π-network to U.FL/wire pad + Tag-Connect SWD. weather_station's core sheet already has it, and the garden board (ADR-0002) uses the same parts.
- **Proposed:** extract it as a **shared KiCad hierarchical sheet** in this repo once the soil monitor starts, so all three boards share one radio layout and one fix point. Not urgent: the weather and garden schematics can draw it now and converge later.
- Soil monitors: same radio core; MCU possibly a smaller L0 if pins allow; small solar + storage like weather_station. Defined in their own project.

## 10. How the nodes appear in Home Assistant (N-12, decided)

**One receiver, many devices: ESPHome sub-devices.** ESPHome lets one node declare several logical devices (`esphome: devices:`). Each entity is attached to one of them with `device_id:`. Home Assistant then shows **one device per node**, each with its own entities and area, all reached through the single gateway. This is the pattern ESPHome documents for RF bridges.

```yaml
esphome:
  name: radio-gateway
  devices:
    - id: garden_ctrl
      name: "Garden Controller"
      area_id: garden
    - id: wx_backyard
      name: "Weather Station"
      area_id: backyard
    - id: soil_bed1
      name: "Soil Monitor – Bed 1"
      area_id: garden

radio_net:                       # our external component
  master_key: !secret rnet_master_key
  nodes:
    - node_id: 0x0010
      type: garden
      device_id: garden_ctrl     # entities for this node are created under that device
    - node_id: 0x0001
      type: weather_acurite
      device_id: wx_backyard
    - node_id: 0x0101
      type: soil
      device_id: soil_bed1
```

In Home Assistant:

| Device | Entities (examples) |
|---|---|
| **Radio Gateway** (the ESP32 itself) | Wi-Fi signal, uptime, frames received/rejected (bad MIC, replay), channel activity |
| **Garden Controller** | Zone 1–3 state, remaining time, main valve, fault, battery, solar, RSSI, last seen. **Actions:** run zone N for M minutes, stop |
| **Weather Station** | Wind, gust, direction, rain, temperature, humidity, pressure, battery, RSSI, last seen |
| **Soil Monitor – Bed 1 … n** | Moisture, soil temperature, battery, RSSI, last seen |

- **Every node gets the same "health" entities** (battery, RSSI, last seen, `unreachable` binary sensor) from the common `HEALTH` message. One Home Assistant automation can watch them all.
- **Garden actions** are ESPHome API actions (`radio_gateway.garden_run`, `garden_stop`) with parameters. They show up in Home Assistant as services/actions, so a dashboard button or script calls "run zone 2 for 15 min". There is still no on/off switch (garden R-3).
- **Limitation:** sub-devices are defined **at compile time**. Adding a soil monitor means adding a `nodes:` + `devices:` entry and an OTA update of the gateway (a minute's work). Frames from unregistered nodes are counted and logged, never exposed.
- **Alternative if runtime discovery is ever wanted:** the gateway publishes MQTT discovery messages per node, which creates devices at runtime. That needs an MQTT broker. Not proposed now; the native API with compile-time devices is simpler and has no broker.

## 11. What this changes in the existing projects

| Project | Change |
|---|---|
| weather_station | Packet format → this frame format (section 4) + CMAC + persisted counter. Adds an hourly `RX_OPEN` frame (time/config). The receiver work moves to `home-radio-net/esphome`. Its spec R-1/R-3 point here; `WX_REPORT` is defined in `schema/rnet.toml` (**done**, weather_station spec v2.19, 2026-10-10) |
| smart-garden-system | Spec 4.4 and `docs/03-radio-link.md` point here for the protocol; ADR-0002 is unaffected (same radio, same GDO0 use). Its node firmware (ADR-0004, accepted 2026-10-10) links `lib/rnet` and `lib/node_l0` and needs the garden message fields in the schema |
| Soil monitors | Start from this spec |

## 12. Prior art: is there an open standard we could just use?

No open standard fits **CC1101 + years-on-small-solar + an ESPHome gateway** off the shelf:

| Standard | Why not as-is |
|---|---|
| **LoRaWAN** (US915) | The closest in spirit, but it needs LoRa radios (SX126x) on every node and a LoRaWAN gateway + network server (ChirpStack/TTN). The CC1101 was chosen over LoRa for cost (weather_station 3.3) |
| **Zigbee / Thread / Matter** | 2.4 GHz 802.15.4, heavy stacks; rejected for the garden in favour of sub-GHz range and low power. (Zigbee stays the garden's fallback) |
| **802.15.4g sub-GHz / Wi-SUN** | Mesh, IPv6 and certification-grade stacks; huge for a few sensors |
| **Wireless M-Bus / OMS** | Meter-reading standard for the EU 868 MHz band, not US 915 |
| **TI SimpliciTI** | TI's own lightweight CC1101 protocol, but abandoned and without real security |
| **RadioHead / rtl_433-style** | Packet formats, no authentication or replay protection |

**What we do instead: copy LoRaWAN's proven ideas onto our PHY.** The frame counter per direction, the 4-byte AES-CMAC MIC, per-device keys derived from a root key, and "class A" receive windows after an uplink all come from LoRaWAN, which has a decade of field and security review behind them. We skip its join procedure, adaptive data rate, multi-channel hopping and network server, none of which a one-gateway home network needs. If the CC1101's range ever falls short, the upgrade path is SX1262 + real LoRaWAN, and the message set (section 7) carries over unchanged.

## 13. Former open questions (all accepted 2026-10-03)

| ID | Question | Proposal |
|---|---|---|
| O-1 | Channel frequency | **Accepted: 918.5 MHz.** It sits in the gap between LoRaWAN US915 uplinks (902.3–914.9 MHz) and downlinks (923.3–927.5 MHz), and between Z-Wave's 916.0 MHz classic and 920 MHz Long-Range channels. Keep it a single config constant so it can move if a neighbour turns out to sit on it |
| O-2 | MIC length | **Accepted: 4 bytes**, as LoRaWAN. Replays are blocked by the counter; a forger gets one 2⁻³² guess per frame on air |
| O-3 | Soil monitors | **Accepted: every 10 min, `RX_OPEN` on every report**, 5–10 nodes, small solar (owner). Sensor and distances defined in their own project. Channel load stays < 1 % |
| O-4 | Key provisioning | **Accepted:** a **host script** (in this repo) reads a node manifest (`node_id`, type, name), derives the node key from the master key, and writes `node_id` + key into the STM32's **data EEPROM** over SWD (`st-flash` / OpenOCD) after flashing firmware. The firmware image stays identical for every node of a type. The same manifest generates the gateway's `nodes:` / `devices:` YAML, so the two can't drift |
| O-5 | Second gateway | **Accepted: not now.** The frame format already allows it: dedup is by `(node_id, ctr)`, so two gateways receiving the same frame is harmless. Only downlinks would need coordination (one gateway answers). Revisit if coverage ever needs it |

## Change log

- **0.1 (2026-10-03):** First draft. N-1–N-4 decided by the owner; N-5–N-11 proposed.
- **0.2 (2026-10-03):** Added section 10 (Home Assistant: one device per node via ESPHome sub-devices), section 12 (prior art / open standards: LoRaWAN ideas on a CC1101 PHY), proposals for O-1–O-5 (918.5 MHz, 4-byte MIC, soil every 10 min, manifest-driven key provisioning, second gateway later). weather_station noted in its repo (its spec v2.12).
- **0.3 (2026-10-03):** Owner accepted all proposals: N-5–N-12 decided; O-1–O-5 accepted (918.5 MHz, 4-byte MIC, soil every 10 min, manifest-driven provisioning, single gateway).
- **0.4 (2026-10-09):** ADR-0001 **Proposed**: one protocol schema (TOML) with generated code for nodes, gateway, host tools and docs; ESPHome gateway stub added to its work order.
- **0.5 (2026-10-10):** `lib/node_l0/` added to the code layout (section 8): one STM32L0 platform and CC1101 driver for every node, from smart-garden-system ADR-0004 D2 (accepted). A change affecting every node project.
- **0.6 (2026-10-10):** ADR-0001 **accepted** (owner). `schema/rnet.toml` drafted: frame, PHY, node types, `HEALTH`, `TIME`, `CONFIG_*`, and `WX_REPORT` (weather_station ADR-0003 step 0, accepted: 35-byte payload; weather housekeeping in an hourly `HEALTH` frame that sets `RX_OPEN`). Garden and soil messages are placeholders. Sync word 0xB54E proposed, pending schema review. Stale "proposed" wording for O-1–O-3 (accepted in 0.3) removed; section 7 points at the schema; section 8 lists `schema/`, `tools/`, `test/vectors/`.
- **0.7 (2026-10-10):** ADR-0002 **Proposed**: firmware toolchain, debug probe and bench tools for every STM32L0 node (distro GCC 13.2, CMake/Ninja presets, vendored ST LL/CMSIS, Unity + ctest, ST-LINK + OpenOCD). Current measurement deferred; the owner's INA219 module (10 Ω / 100 Ω shunt) is the fallback if a power problem shows up. Affects all node projects.
- **0.8 (2026-10-10):** ADR-0002: probe = the owner's HiLetgo ST-LINK/V2 clone (SWDIO/SWCLK/GND/RST on the 2.54 mm header, no 3V3); USART1 log through a 3.3 V USB-UART, RTT as fallback; firmware rule: `DBG_STOP` in Debug builds and a ~2 s delay before the first Stop entry so SWD can always attach.
- **0.9 (2026-10-10):** ADR-0002 **accepted**. `lib/node_l0` started: toolchain file, L051 linker script, vendored ST LL/CMSIS (pinned), EEPROM wear-levelled ring with host tests, clocks/RTC/Stop/IWDG/reset-cause/EEPROM/UART log (compile-checked), smoke image (2.3 KB). Root CMake presets and GitHub Actions CI added. CC1101 driver waits for the generator.
