# home-radio-net

Shared 915 MHz radio architecture for Andrew's battery/solar nodes and the one ESPHome gateway that bridges them to Home Assistant. No cloud.

Used by:
- `~/Documents/weather_station`: weather station brain transplants (sensor uplink)
- `~/Documents/smart-garden-system`: solar watering controller (actuator, timer-only commands)
- soil moisture monitors (not started)

## Layout

```
docs/                       architecture spec (start with docs/01-architecture.md) and ADRs
schema/                     rnet.toml: the single protocol definition (ADR-0001)
tools/rnetgen/              (to come) generator: C, gateway, Python, docs, test vectors
lib/node_l0/                shared STM32L0 platform: toolchain, ST LL/CMSIS, clocks, RTC, Stop, IWDG, EEPROM ring
lib/rnet/                   (to come) portable C library: frames, AES-CMAC, counters
third_party/unity/          Unity test framework (vendored)
esphome/components/         (to come) ESPHome gateway component
```
