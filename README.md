# home-radio-net

Shared 915 MHz radio architecture for Andrew's battery/solar nodes and the one ESPHome gateway that bridges them to Home Assistant. No cloud.

Used by:
- `~/Documents/weather_station`: weather station brain transplants (sensor uplink)
- `~/Documents/smart-garden-system`: solar watering controller (actuator, timer-only commands)
- soil moisture monitors (not started)

## Layout

```
docs/                       architecture spec (start with docs/01-architecture.md) and ADRs
schema/                     (proposed, ADR-0001) rnet.toml: the single protocol definition
tools/rnetgen/              (proposed) generator: C, gateway, Python, docs, test vectors
lib/rnet/                   (to come) portable C library: frames, AES-CMAC, counters
esphome/components/         (to come) ESPHome gateway component
```
