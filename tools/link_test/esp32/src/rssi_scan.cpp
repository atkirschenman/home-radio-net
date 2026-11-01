// acronym  meaning
// rssi     received signal strength indicator
// csn      chip select, active low
// gdo      CC1101 general digital output pin
//
// sweep plan
//
//   pass       frequencies                dwell   receive bandwidth
//   sweep      860 .. 960 MHz, 0.5 MHz    1.1 s   812 kHz (overlapping steps)
//   on_channel 918.5 MHz, repeated        1 s     101.6 kHz (the protocol's)
//
// 860-960 MHz brackets 918.5 MHz with either radio on a 27 MHz crystal instead of
// 26 MHz (918.5 x 27/26 = 953.8, 918.5 x 26/27 = 884.5); the build disables
// RadioLib's 779-928 MHz range check so the synthesizer is asked anyway
//
// the node must be on the link-test image (one packet per second)
#include <Arduino.h>
#include <RadioLib.h>
#include <SPI.h>

#include "rnet/cc1101_config.hpp"
#include "rnet_phy.hpp"

namespace
{

constexpr int sck_pin = 18, miso_pin = 19, mosi_pin = 23, csn_pin = 5, gdo0_pin = 21, gdo2_pin = 22;
constexpr int8_t receiver_power_dbm = -15;
constexpr float sweep_start_mhz = 860.0f;
constexpr float sweep_end_mhz = 960.0f;
constexpr float sweep_step_mhz = 0.5f;
constexpr float sweep_bandwidth_khz = 812.0f;
constexpr uint32_t on_channel_dwell_ms = 1000;
constexpr float protocol_bandwidth_khz = rnet::phy::rx_bandwidth_khz;
constexpr uint32_t sweep_dwell_ms = 1100;
constexpr float peak_above_floor_db = 15.0f;

CC1101 radio = new Module(csn_pin, gdo0_pin, RADIOLIB_NC, gdo2_pin);

struct signal_levels {
    float floor_dbm;
    float peak_dbm;
};

float rssi_register_dbm()
{
    const uint8_t raw = radio.SPIreadRegister(RADIOLIB_CC1101_REG_RSSI);
    const int signed_raw = raw >= 128 ? raw - 256 : raw;
    return signed_raw / 2.0f - 74.0f;
}

signal_levels listen_at(float frequency_mhz, uint32_t dwell_ms)
{
    radio.standby();
    radio.setFrequency(frequency_mhz);
    radio.startReceive();
    delay(2);
    signal_levels levels{0.0f, -200.0f};
    float floor_sum = 0.0f;
    uint32_t samples = 0;
    const uint32_t start = millis();
    while (millis() - start < dwell_ms) {
        const float dbm = rssi_register_dbm();
        levels.peak_dbm = max(levels.peak_dbm, dbm);
        floor_sum += dbm;
        samples++;
    }
    levels.floor_dbm = floor_sum / static_cast<float>(samples);
    return levels;
}

void print_levels(float frequency_mhz, const signal_levels &levels)
{
    const bool signal_seen = levels.peak_dbm - levels.floor_dbm >= peak_above_floor_db;
    Serial.printf("frequency=%.1f MHz floor=%.1f dBm peak=%.1f dBm%s\n", frequency_mhz,
                  levels.floor_dbm, levels.peak_dbm, signal_seen ? "  <-- signal" : "");
}

} // namespace

void setup()
{
    Serial.begin(115200);
    delay(500);
    SPI.begin(sck_pin, miso_pin, mosi_pin, csn_pin);
    const int16_t status = rnet::configure_cc1101(radio, receiver_power_dbm);
    Serial.printf("\nrssi scan: cc1101 (RadioLib) %d (0 = ok)\n", status);

    radio.setRxBandwidth(sweep_bandwidth_khz);
    Serial.println("sweep:");
    for (float frequency = sweep_start_mhz; frequency <= sweep_end_mhz; frequency += sweep_step_mhz) {
        print_levels(frequency, listen_at(frequency, sweep_dwell_ms));
    }
    radio.setRxBandwidth(protocol_bandwidth_khz);
    Serial.println("on_channel:");
}

void loop()
{
    print_levels(rnet::phy::frequency_mhz, listen_at(rnet::phy::frequency_mhz, on_channel_dwell_ms));
}
