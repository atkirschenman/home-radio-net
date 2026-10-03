# 01 — Home radio network: architecture

*Version 0.1, 2026-10-03. The shared 915 MHz radio architecture for every battery/solar node Andrew builds, and the one indoor gateway that bridges them to Home Assistant. Anything marked **Proposed** is not decided until the owner accepts it. Change log at the end.*

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
| N-5 – N-12 | PHY parameters, frame format, keys, counters, MAC, message set, code layout (sections 3–8) | Proposed |

## 3. PHY (Proposed, N-5)

| Parameter | Value | Why |
|---|---|---|
| Band / channel | **915 MHz ISM, one fixed channel** (exact frequency TBD, avoiding the band centre that LoRaWAN/Z-Wave neighbours favour) | One channel keeps the gateway simple; low traffic (section 6.4) |
| Modulation | GFSK, **38.4 kbps**, deviation ~20 kHz, RX bandwidth ~100 kHz | weather_station spec R-2; ~10 ms per frame |
| Preamble / sync | 4 bytes / **30-of-32 sync, our own sync word** (not the CC1101 default 0xD391) | Rejects other CC1101 products on the default word |
| Packet | **Variable length** (length byte), ≤ 61 bytes payload (one FIFO), **CRC-16 on**, **whitening on** | Variable length lets every node type share one config; CRC drops corrupted frames in hardware |
| TX power | Capped per node for FCC 15.249 (~−1 dBm EIRP; the cap depends on each antenna) | Same rule as weather_station 3.2; the gateway is capped too |

## 4. Frame format (Proposed, N-6)

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

**MIC length (Proposed: 4 bytes):** the same as LoRaWAN. A forger gets one guess per frame on air (≈ 2⁻³² chance each). At this link's airtime, brute force takes centuries, and replays are stopped by the counter. 8 bytes would cost 0.8 ms more per frame. Owner to confirm (open question O-2).

**Duplicates:** the weather station's 2–3 repeats carry the **same** `ctr` and identical bytes. The gateway accepts the first and drops the rest silently.

## 5. Security (N-3 decided; details Proposed, N-7)

### 5.1 Keys
- **One 128-bit master key per home**, held only by the gateway (ESPHome `secrets.yaml`).
- **Per-node key** `K_node = AES-128(master, node_id ‖ node_type ‖ "rnet" ‖ zero padding)`. The gateway derives it on the fly; each node is flashed with only **its own** key and `node_id` (a provisioning step in the build, stored in the STM32's EEPROM/option area). One leaked node key doesn't expose the others.
- **No payload encryption** (authenticity only, per N-3). Readings and valve states aren't secret. Adding AES-CTR later would be a version bump.

### 5.2 Counters and replay
- Each node keeps an **uplink counter**; the gateway keeps a **downlink counter per node**. A receiver accepts a frame only if `ctr` > last accepted (duplicates with the same counter are dropped, not errors).
- **Persistence without flash wear:** the node writes its counter to EEPROM every 256 frames. On boot it resumes at stored + 256, so a reboot never reuses a value. The gateway does the same in ESP32 NVS per node.
- At the garden's 1 frame / 3 s, a 32-bit counter lasts > 400 years.

## 6. Medium access (Proposed, N-8)

### 6.1 Uplink: unslotted ALOHA with jitter
Nodes transmit on their own schedule with ±10 % random jitter on the period, so two nodes never stay synchronised. No listen-before-talk; the load is too low to need it (6.4).

### 6.2 Downlink: only into a receive window the node opened ("class A" style)
- A node that sets `RX_OPEN` listens for **W = 120 ms** after its uplink (tunable per node type). The window ends early if no sync word arrives.
- The gateway keeps a **small downlink queue per node** (depth 4). It transmits only inside that node's window. One downlink per window; `MORE` pending is signalled in the downlink so the node can poll again immediately.
- This means **battery nodes choose when they can be reached**:

| Node | `RX_OPEN` | Reachable within |
|---|---|---|
| Garden controller | every poll (3 s) | ~3–5 s |
| Soil monitor | every report (proposed) | one report interval |
| Weather station | **one frame per hour** (proposed). Its 1 mAh/day budget can't pay for a window every minute (~30 µA average) | ~1 h (config/time only) |

### 6.3 Acknowledgements
- Uplink with `ACK_REQ` → the gateway answers in the window (an ack can ride on a queued downlink). Used by the garden for state changes and faults. Sensor reports don't request acks; the weather station's repeats do the job.
- Downlink with `ACK_REQ` (every garden command) → the node sets `ACK` + `ack_ctr` in its next uplink. The gateway retries in later windows until acknowledged. After ~30 s of retries it marks the node **unreachable** in Home Assistant.

### 6.4 Channel load (sanity check)
Garden 1 × 11 ms / 3 s + weather 2 stations × 3 × 11 ms / 60 s + 10 soil nodes × 11 ms / 300 s ≈ **0.5 % channel occupancy**. Pure-ALOHA collision probability per frame ≈ 1 %. Retries and repeats absorb that. Fine up to dozens of nodes.

## 7. Message set (Proposed, N-9)

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

## 8. Code layout (Proposed, N-10)

```
home-radio-net/
  docs/                 this spec + ADRs
  lib/rnet/             portable C99, no malloc, no OS:
                        frame pack/parse, AES-128 + CMAC, key derivation,
                        counter/replay window, message structs
  lib/rnet/test/        host unit tests: RFC 4493 CMAC vectors, frame round-trips,
                        replay cases (runs in CI / on the PC)
  esphome/components/radio_net/
                        ESPHome external component: wraps the cc1101 component,
                        uses lib/rnet, holds per-node state and downlink queues,
                        creates HA entities per node type, exposes actions
                        (garden_run, garden_stop, node_config)
```

- **The same C library runs on the STM32 nodes and inside the ESP32 component**, so both ends share one implementation and one set of test vectors. Software AES costs ~1 ms per frame on a 32 MHz Cortex-M0+, which is fine.
- The component **replies from its own packet listener**, not via a YAML `on_packet` automation, to keep the downlink inside the node's 120 ms window (`smart-garden-system/docs/03-radio-link.md`).
- **Node registry** in the gateway YAML: `node_id`, `node_type`, name. Entities are created per type. Unknown nodes are logged, not exposed.

## 9. Hardware commonality (Proposed, N-11)

- **"Radio core"** = STM32L0 (L051C8T6 or family) + 32.768 kHz LSE + E07-900MM10S + π-network to U.FL/wire pad + Tag-Connect SWD. weather_station's core sheet already has it, and the garden board (ADR-0002) uses the same parts.
- **Proposed:** extract it as a **shared KiCad hierarchical sheet** in this repo once the soil monitor starts, so all three boards share one radio layout and one fix point. Not urgent: the weather and garden schematics can draw it now and converge later.
- Soil monitors: same radio core; MCU possibly a smaller L0 if pins allow; small solar + storage like weather_station. Defined in their own project.

## 10. What this changes in the existing projects

| Project | Change |
|---|---|
| weather_station | Packet format → this frame format (section 4) + CMAC + persisted counter. Adds an hourly `RX_OPEN` frame (time/config). The receiver work moves to `home-radio-net/esphome`. Its spec R-1 ("our own packet format") points here. **Not edited yet: awaiting the owner's OK** |
| smart-garden-system | Spec 4.4 and `docs/03-radio-link.md` point here for the protocol; ADR-0002 is unaffected (same radio, same GDO0 use) |
| Soil monitors | Start from this spec |

## 11. Open questions

| ID | Question |
|---|---|
| O-1 | Exact channel frequency |
| O-2 | MIC 4 bytes (proposed) or 8 |
| O-3 | Soil monitors: report interval, sensor type, expected count, distance to gateway |
| O-4 | Key provisioning workflow (script that writes `node_id` + key into the node's EEPROM over SWD?) |
| O-5 | Ever a second gateway (coverage/redundancy)? The counters and dedup would need to handle two receivers |

## Change log

- **0.1 (2026-10-03):** First draft. N-1–N-4 decided by the owner; N-5–N-11 proposed.
