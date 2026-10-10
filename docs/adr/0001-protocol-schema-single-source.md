# ADR-0001: One protocol schema, generated code for every device

- **Status:** Accepted (owner, 2026-10-10). Work-order step 1 (`schema/rnet.toml`) drafted, awaiting schema review
- **Date:** 2026-10-09
- **Affects:** every project: weather_station (node firmware), smart-garden-system (node firmware), soil monitors, the ESPHome gateway, host tools (provisioning, sniffer)
- **Builds on:** `01-architecture.md` N-6 (frame format), N-9 (message set), N-10 (code layout), N-12 (Home Assistant representation)

## Context

The same message definitions are needed in at least five places:

1. **STM32 node firmware** (C99): weather, garden and soil; it packs uplinks and parses downlinks.
2. **The ESPHome gateway** (C++ inside the external component plus Python config validation): it parses uplinks, packs downlinks, and creates Home Assistant entities with the right units and device classes.
3. **Host tools** (Python): the provisioning script (O-4), a sniffer/decoder for RTL-SDR or a second CC1101, and test-vector generation.
4. **The docs:** the message tables in `01-architecture.md` §7 and each project's spec.
5. **Station constants the gateway needs** to turn raw values into units, e.g. rain per tip for AcuRite vs WS69.

Written by hand, these drift: a field added to the weather node but not to the gateway decodes as garbage, and nothing catches it until the numbers in Home Assistant look wrong. We want **one file that defines the protocol, with everything else generated from it and checked in CI.**

Constraints from the decided architecture:

- **N-6: the payload is a fixed little-endian struct per `msg_type`**, fixed-point, no floats, ≤ 47 bytes (61-byte FIFO − 14 bytes of frame overhead).
- C99 on the nodes: no malloc, small flash (64 KB on the STM32L051).
- Home Assistant needs per-field **metadata** (unit, scale, device_class, state_class, invalid/sentinel value), not just the wire layout.
- The project already generates hardware files with stdlib-only Python scripts (weather_station `hardware/kicad/tools/`).

## Options

| Option | Fit | Why not (or why) |
|---|---|---|
| **Protocol Buffers + nanopb** | Mature, C and Python codegen, schema evolution built in | The **varint/tag wire format** replaces N-6's fixed struct: a decided format would change, payload size would vary with the values, and every field costs a tag byte. There's no native place for scale/unit/HA metadata (custom options are possible, but then we write a protoc plugin anyway). nanopb adds ~2–5 KB of flash. **Not recommended**, but it's the strongest alternative if you'd rather have schema evolution than fixed layouts |
| FlatBuffers / Cap'n Proto | Zero-copy, codegen | Offset tables and alignment padding waste bytes in a 47-byte payload; heavy for an M0+ |
| Kaitai Struct | Good for describing fixed binary layouts | **Decode only**; its C/C++ targets need a runtime. No encoder for the nodes |
| ASN.1 (UPER) / CDDL + CBOR | Standards | Heavy toolchains; CBOR is self-describing, so it's bigger on air |
| C header as the source (structs, parsed for the other targets) | No new language | No home for units/HA metadata; packed structs aren't portable between M0+ and Xtensa without care; parsing C from Python is fragile |
| **Own schema file (TOML) + a small stdlib-only Python generator** | Matches N-6 exactly; carries the metadata; generates exactly the C, C++, Python and docs we need | We maintain ~500 lines of generator. **Recommended** |

## Decision

### D1: The schema is `schema/rnet.toml`, the single source of truth

TOML is chosen because Python 3.11+ reads it with the stdlib `tomllib`, so no dependencies are needed. It defines:

- **Protocol constants:** frame version, `ctrl` bits, `node_type` values, `msg_type` values, MIC length, header layout.
- **PHY settings:** 918.5 MHz, 38.4 kbps, deviation, sync word, plus the derived **CC1101 register values**. The nodes' driver and the gateway's `cc1101:` YAML block both come from here, so the two ends can't disagree on the channel.
- **Messages:** name, id, direction, which node types use it, and an ordered field list. Each field has a type (`u8/u16/u32/i16/i32`, `bits`, fixed arrays), `scale`, `unit`, an `invalid` sentinel, an optional `since` (schema version that added it), and Home Assistant metadata (`device_class`, `state_class`, `accuracy_decimals`, `entity_category`).
- **Node-type constants** needed by the gateway (e.g. `rain_um_per_tip` for weather-AcuRite vs weather-WS69).

Sketch:

```toml
[node_types]
weather_acurite = { id = 0x01, consts = { rain_um_per_tip = 254 } }
weather_ws69    = { id = 0x02, consts = { rain_um_per_tip = 300 } }
garden          = { id = 0x10 }
soil            = { id = 0x20 }

[messages.WX_REPORT]
id = 0x10
dir = "up"
node_types = ["weather_acurite", "weather_ws69"]
fields = [
  { name = "wind_mean", type = "u16", scale = 0.01, unit = "m/s", ha = { device_class = "wind_speed", state_class = "measurement" } },
  { name = "wind_gust", type = "u16", scale = 0.01, unit = "m/s", ha = { device_class = "wind_speed", state_class = "measurement" } },
  { name = "wind_dir",  type = "u16", scale = 0.1,  unit = "°", invalid = 0xFFFF },
  { name = "rain_tips", type = "u32", convert = "rain_um_per_tip", unit = "mm", ha = { device_class = "precipitation", state_class = "total_increasing" } },
  { name = "flags",     type = "bits", bits = { rain_unreliable = 0, calm = 1 } },
  # … the real field list is weather_station ADR-0003 work-order step 0
]
```

### D2: A stdlib-only generator, `tools/rnetgen/`, emits every target

| Output | Path | Used by |
|---|---|---|
| C99 header + pack/unpack functions (explicit byte shifts, no packed structs, `_Static_assert` on sizes) | `lib/rnet/gen/rnet_msgs.h/.c`, `rnet_phy.h` | Every STM32 node, and the gateway's C++ |
| Gateway entity tables: per-message decoders → (field, scale, unit, HA metadata); Python config tables for `__init__.py` | `esphome/components/radio_net/gen/` | ESPHome component |
| Python codec | `tools/rnet_py/rnet_msgs.py` | Provisioning, sniffer, test-vector generation |
| Markdown message tables | `docs/generated/messages.md` | Linked from `01-architecture.md` §7 and the project specs instead of copied |
| **Golden test vectors** (JSON: field values → payload bytes → full frame with CMAC under a published test key) | `test/vectors/*.json` | C tests, Python tests, gateway tests: the cross-implementation check |

**Generated files are committed**, so a firmware build needs no Python step. CI (or a pre-commit check) runs the generator and fails if anything differs.

### D3: Evolution rules

- **Append-only within a message:** new fields go at the end with `since = N`. A receiver accepts a **longer** payload and ignores the tail. A **shorter** payload marks the missing fields unavailable. So an old node and a new gateway (or the reverse) keep working.
- Removing, reordering, or changing a field's type or scale is **breaking**: it needs a new `msg_type` or a frame-version bump (`ctrl` bits 7–6), per the rule in `CLAUDE.md`.
- The generator writes a **schema hash** into every target. Nodes report it in `HEALTH` (fw-version field), and the gateway logs a mismatch.

### D4: Projects consume it via the submodule

weather_station, smart-garden-system and the soil monitors already take home-radio-net as a git submodule (N-2). They include `lib/rnet/gen/` and never hand-write message structs. Project-specific *meaning* (how a gust is computed) stays in each project; only the *wire contract* lives here.

## ESPHome gateway stub (part of this work)

A first skeleton of `esphome/components/radio_net/`, so the gateway exists early and consumes the generated code from day one:

- `__init__.py`: config schema (`master_key`, `cc1101_id`, `nodes:` list with `node_id`, `type`, `device_id`), validation against the generated node-type table.
- `radio_net.h/.cpp`: attaches to the stock `cc1101` component's packet callback; parses the header with `lib/rnet`; logs node, type, counter and RSSI. **Stub level:** log only, then CMAC check + replay; entities and downlink queues come later.
- `example/gateway.yaml`: ESP32 + cc1101 with the generated PHY values, ESPHome version pinned (the `cc1101` component is new; smart-garden-system `docs/03-radio-link.md`).
- **Done when:** `esphome config example/gateway.yaml` validates, and `esphome compile` builds. ESPHome isn't installed on this machine yet: `pipx install esphome`.

## Work order (home-radio-net)

| # | Step | ✋ |
|---|---|---|
| 1 | `schema/rnet.toml` with the header, PHY, node types, `HEALTH`, `TIME`, `CONFIG_*`, and `WX_REPORT` (field list from weather_station ADR-0003 step 0). Garden/soil messages as placeholders until those projects define them | ✋ review the schema |
| 2 | `tools/rnetgen`: C + Python + Markdown outputs + test vectors; a check mode (`--check` fails on diff) | |
| 3 | `lib/rnet`: AES-128, CMAC (RFC 4493), key derivation, frame pack/parse, replay, counter persistence, using the generated message code; host ctest against the vectors | ✋ before submoduling |
| 4 | ESPHome stub (above) | |
| 5 | Python sniffer/decoder + provisioning script (O-4) on the same codec | |

## Consequences

- `01-architecture.md` §7 stops being a hand-maintained table and points at `docs/generated/messages.md`. §8's code layout gains `schema/`, `tools/rnetgen/`, `tools/rnet_py/`, `test/vectors/`.
- weather_station ADR-0003 work order: the `WX_REPORT` struct is written into this schema rather than as a hand-written C struct.
- smart-garden-system: its `GARDEN_*` payloads get defined in the schema when its firmware starts. Garden actuation stays timer-only: the schema has no on/open message, and a generator check can enforce that (any `GARDEN_*` downlink must carry a non-zero duration).
- Cost: we own a small generator. If we ever need richer evolution than append-only, switching to nanopb is a frame-version bump, and the schema's field list carries over.
