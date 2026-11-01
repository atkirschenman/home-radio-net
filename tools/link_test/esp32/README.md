# ESP32 link-test receiver

ESP32 DevKitC + CC1101 module. Receives, verifies and decodes the weather node's frames and prints link statistics over USB serial.

## Wiring

**3.3 V only.** The CC1101 is not 5 V tolerant: VCC goes to the `3V3` pin, never `5V` / `VIN`.

| CC1101 module pin | Other labels seen on modules | ESP32 DevKitC pin |
|---|---|---|
| VCC | 3V3, VDD | `3V3` |
| GND | | `GND` |
| SCK | SCLK, CLK | `GPIO18` |
| MISO | SO, GDO1 | `GPIO19` |
| MOSI | SI | `GPIO23` |
| CSN | CS, SS | `GPIO5` |
| GDO0 | G0 | `GPIO21` |
| GDO2 | G2 | `GPIO22` |

```
  CC1101 module                         ESP32 DevKitC
  +-----------+                        +-------------+
  | VCC       |<-----------------------| 3V3         |
  | GND       |------------------------| GND         |
  | SCK       |<-----------------------| GPIO18      |
  | MISO / SO |----------------------->| GPIO19      |
  | MOSI / SI |<-----------------------| GPIO23      |
  | CSN / CS  |<-----------------------| GPIO5       |
  | GDO0      |----------------------->| GPIO21      |
  | GDO2      |----------------------->| GPIO22      |
  | ANT       |--- antenna             +-------------+
  +-----------+
```

Pin order differs between module makers (2×4 or 1×8 headers): wire by the silkscreen labels, not by position. Fit the antenna before powering up.

## Flash and watch

```bash
cd ~/Documents/home-radio-net/tools/link_test/esp32 && ~/.platformio/penv/bin/pio run -t upload -t monitor
```

| Symptom | Cause |
|---|---|
| no `/dev/ttyUSB0` when plugged in | charge-only USB cable, or the port |
| upload stuck at `Connecting...` | hold BOOT until the upload starts |
| radio status -2 (chip not found) | SPI wiring, CSN, or no 3.3 V |
| radio ok, nothing received | GDO0/GDO2 wiring, node not sending, antenna |

## Diagnostics

| Tool | Run (from `tools/link_test`) | Shows |
|---|---|---|
| wiring check | `cd esp32_wiring_check && pio run -t upload -t monitor` | chip version over SPI; whether chip select reaches the chip; which ESP32 pin each GDO output drives |
| raw read | `cd esp32_wiring_check && pio run -e raw_read -t upload -t monitor` | one register read clocked by hand, every bit printed; a command test showing whether the chip acts on commands |
| line toggle | `cd esp32_wiring_check && pio run -e line_toggle -t upload -t monitor` | SCK, MOSI, CSN and MISO driven 3 s high, 3 s low in turn, for a multimeter at the module pins |
| signal sweep | `cd esp32 && pio run -e rssi_scan -t upload -t monitor` | signal level 860-960 MHz, then a live meter on the node's channel (node on its link-test image) |

## Received signal strength at 1 m

| RSSI | Meaning |
|---|---|
| −45 to −50 dBm | module matched for 915 MHz |
| −65 dBm or lower | module matched for another band (433/868 MHz) |
