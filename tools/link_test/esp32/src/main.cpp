// acronym  meaning
// rnet     home radio network (this protocol)
// mic      message integrity code: truncated AES-CMAC
// lqi      link quality indicator (CC1101)
// csn      chip select, active low
// gdo      CC1101 general digital output pin
//
// wiring, module pin -> ESP32 DevKitC, 3.3 V only
//
//   module  VCC  GND  SCK  MISO  MOSI  CSN  GDO0  GDO2
//   esp32   3V3  GND  18   19    23    5    21    22
#include <Arduino.h>
#include <RadioLib.h>
#include <SPI.h>

#include "link_test_packet.hpp"
#include "rnet/cc1101_config.hpp"
#include "rnet/rnet.hpp"

namespace
{

constexpr int sck_pin = 18, miso_pin = 19, mosi_pin = 23, csn_pin = 5, gdo0_pin = 21, gdo2_pin = 22;
constexpr int8_t receiver_power_dbm = -15;
constexpr uint32_t receive_timeout_ms = 1000;
constexpr uint32_t summary_period_ms = 30000;
constexpr uint16_t no_direction = 0xFFFF;
constexpr size_t max_known_nodes = 8;
constexpr float crystal_hz = 26e6f;
constexpr float frequency_estimate_step_khz = crystal_hz / (1u << 14) / 1000.0f;

CC1101 radio = new Module(csn_pin, gdo0_pin, RADIOLIB_NC, gdo2_pin);

struct link_statistics {
    uint32_t received = 0;
    uint32_t lost = 0;
    uint32_t unrecognised = 0;
    float rssi_sum = 0;
    float rssi_min = 0;
    float rssi_max = -200;
    bool have_sequence = false;
    uint32_t last_sequence = 0;
};
link_statistics link_stats;

struct known_node {
    bool used = false;
    uint16_t node_id = 0;
    uint8_t node_type = 0;
    rnet::receive_counter counter;
};
known_node known_nodes[max_known_nodes];

struct packet_quality {
    float rssi_dbm;
    uint8_t lqi;
    float frequency_offset_khz;
};

float frequency_offset_khz()
{
    const auto estimate = static_cast<int8_t>(radio.SPIreadRegister(RADIOLIB_CC1101_REG_FREQEST));
    return estimate * frequency_estimate_step_khz;
}

void print_link_summary()
{
    const uint32_t expected = link_stats.received + link_stats.lost;
    const float loss_percent = expected ? 100.f * link_stats.lost / expected : 0.f;
    const float rssi_average =
        link_stats.received ? link_stats.rssi_sum / link_stats.received : 0.f;
    Serial.printf(
        "== received=%lu lost=%lu (%.1f%%) unrecognised=%lu rssi min/avg/max=%.1f/%.1f/%.1f dBm\n",
        (unsigned long)link_stats.received, (unsigned long)link_stats.lost, loss_percent,
        (unsigned long)link_stats.unrecognised, link_stats.rssi_min, rssi_average,
        link_stats.rssi_max);
}

void record_link_test_packet(uint32_t sequence, uint8_t power_setting, uint16_t battery_mv,
                             packet_quality quality)
{
    if (link_stats.have_sequence && sequence <= link_stats.last_sequence) {
        Serial.println("(sequence restarted: node reset)");
    }
    const bool gap = link_stats.have_sequence && sequence > link_stats.last_sequence + 1;
    if (gap) {
        link_stats.lost += sequence - link_stats.last_sequence - 1;
    }
    link_stats.received++;
    link_stats.rssi_sum += quality.rssi_dbm;
    link_stats.rssi_min =
        link_stats.received == 1 ? quality.rssi_dbm : min(link_stats.rssi_min, quality.rssi_dbm);
    link_stats.rssi_max = max(link_stats.rssi_max, quality.rssi_dbm);
    link_stats.have_sequence = true;
    link_stats.last_sequence = sequence;
    Serial.printf("sequence=%lu rssi=%.1f lqi=%u frequency_offset=%.1f kHz power_setting=0x%02X "
                  "battery=%u mV%s\n",
                  (unsigned long)sequence, quality.rssi_dbm, quality.lqi, quality.frequency_offset_khz,
                  power_setting, battery_mv, gap ? " (gap)" : "");
}

known_node *find_or_add_node(uint16_t node_id, uint8_t node_type)
{
    for (known_node &node : known_nodes) {
        if (node.used && node.node_id == node_id && node.node_type == node_type) {
            return &node;
        }
    }
    for (known_node &node : known_nodes) {
        if (!node.used) {
            node = known_node{};
            node.used = true;
            node.node_id = node_id;
            node.node_type = node_type;
            return &node;
        }
    }
    return nullptr;
}

uint32_t rain_nm_per_tip(uint8_t node_type)
{
    switch (node_type) {
    case rnet::node::weather_acurite:
        return rnet::node::weather_acurite_rain_nm_per_tip;
    case rnet::node::weather_ws69:
        return rnet::node::weather_ws69_rain_nm_per_tip;
    case rnet::node::weather_misol:
        return rnet::node::weather_misol_rain_nm_per_tip;
    default:
        return 0;
    }
}

void print_direction(const char *label, uint16_t tenths_of_degree)
{
    if (tenths_of_degree == no_direction) {
        Serial.printf(" %s=-", label);
    } else {
        Serial.printf(" %s=%.1f", label, tenths_of_degree / 10.0);
    }
}

void print_weather_report(const rnet::weather_report &report, uint8_t node_type)
{
    Serial.printf("  weather interval=%us wind mean/gust/lull=%.2f/%.2f/%.2f m/s",
                  report.interval_seconds, report.wind_mean / 100.0, report.wind_gust / 100.0,
                  report.wind_lull / 100.0);
    print_direction("direction", report.wind_direction);
    print_direction("direction_at_gust", report.wind_direction_at_gust);
    const double rain_mm = report.rain_tips * (double)rain_nm_per_tip(node_type) / 1e6;
    Serial.printf("\n     rain=%lu tips (%.2f mm)", (unsigned long)report.rain_tips, rain_mm);
    if (report.temperature == rnet::weather_report::temperature_invalid) {
        Serial.print(" temperature=-");
    } else {
        Serial.printf(" temperature=%.2f C", report.temperature / 100.0);
    }
    if (report.relative_humidity == rnet::weather_report::relative_humidity_invalid) {
        Serial.print(" humidity=-");
    } else {
        Serial.printf(" humidity=%.1f %%", report.relative_humidity / 100.0);
    }
    if (report.pressure == rnet::weather_report::pressure_invalid) {
        Serial.print(" pressure=-");
    } else {
        Serial.printf(" pressure=%.2f hPa", report.pressure / 100.0);
    }
    Serial.printf(" battery=%u mV flags=0x%02X", report.battery_voltage, report.flags);
    if (report.flags & rnet::weather_report::flags_previous_interval_valid) {
        Serial.printf(" previous=%.2f/%.2f/%.2f", report.previous_wind_mean / 100.0,
                      report.previous_wind_gust / 100.0, report.previous_wind_lull / 100.0);
    }
    Serial.println();
}

void print_health(const rnet::health &health)
{
    const bool schema_matches = health.schema_hash == rnet::phy::schema_hash;
    Serial.printf("  health battery=%u panel=%u mV board=%.1f C downlink_rssi=%d uptime=%lus "
                  "firmware=%u.%u schema=0x%04X%s reset_cause=%u\n",
                  health.battery_voltage, health.panel_voltage, health.board_temperature / 10.0, health.downlink_rssi,
                  (unsigned long)health.uptime, health.firmware_version >> 8, health.firmware_version & 0xFF,
                  health.schema_hash, schema_matches ? "" : " (MISMATCH)", health.reset_cause);
}

void print_payload(const rnet::parsed_frame &frame)
{
    const uint8_t node_type = frame.header.node_type;
    if (frame.header.message_type == rnet::weather_report::id) {
        rnet::weather_report report;
        report.unpack(frame.payload, frame.payload_length);
        print_weather_report(report, node_type);
    } else if (frame.header.message_type == rnet::health::id) {
        rnet::health health;
        health.unpack(frame.payload, frame.payload_length);
        print_health(health);
    }
}

bool handle_rnet_frame(const uint8_t *bytes, size_t length, packet_quality quality)
{
    rnet::parsed_frame frame;
    if (!rnet::parse_frame_without_mic_check(bytes, length, frame)) {
        return false;
    }
    const rnet::frame_header &header = frame.header;
    Serial.printf("rnet node=0x%04X type=0x%02X net=0x%02X counter=%lu message=0x%02X "
                  "control=0x%02X rssi=%.1f lqi=%u frequency_offset=%.1f kHz",
                  header.node_id, header.node_type, header.net_id, (unsigned long)header.counter, header.message_type,
                  header.control, quality.rssi_dbm, quality.lqi, quality.frequency_offset_khz);

    const rnet::aes_key key = rnet::derive_node_key(rnet::test_master_key, header.node_id, header.node_type);
    if (!rnet::mic_is_valid(key, bytes, length)) {
        Serial.println(" MIC INVALID (not the test key, or corrupt)");
        return true;
    }
    known_node *node = find_or_add_node(header.node_id, header.node_type);
    const rnet::replay_verdict verdict =
        node ? node->counter.judge(header.counter) : rnet::replay_verdict::fresh;
    if (verdict == rnet::replay_verdict::duplicate) {
        Serial.println(" (repeat)");
        return true;
    }
    if (verdict == rnet::replay_verdict::old) {
        Serial.printf(" REPLAY: counter not above %lu\n",
                      (unsigned long)node->counter.last_accepted());
        return true;
    }
    if (node) {
        node->counter.accept(header.counter);
    }
    Serial.println(header.control & rnet::control_bits::rx_open ? " listening for a downlink" : "");
    print_payload(frame);
    return true;
}

void print_unrecognised(const uint8_t *bytes, size_t length, packet_quality quality)
{
    link_stats.unrecognised++;
    Serial.printf("unrecognised length=%u rssi=%.1f:", (unsigned)length, quality.rssi_dbm);
    for (size_t i = 0; i < length; i++) {
        Serial.printf(" %02X", bytes[i]);
    }
    Serial.println();
}

void handle_packet(const uint8_t *bytes, size_t length, packet_quality quality)
{
    if (const auto link = link_test::read_packet(bytes, length)) {
        record_link_test_packet(link->sequence, link->power_setting, link->battery_mv, quality);
    } else if (!handle_rnet_frame(bytes, length, quality)) {
        print_unrecognised(bytes, length, quality);
    }
}

} // namespace

void setup()
{
    Serial.begin(115200);
    delay(500);
    SPI.begin(sck_pin, miso_pin, mosi_pin, csn_pin);
    Serial.printf("\nrnet bench receiver: %.4f MHz, %.1f kbps, sync 0x%02X%02X, schema 0x%04X\n",
                  rnet::phy::frequency_mhz, rnet::phy::bit_rate_kbps, rnet::phy::sync_word_high,
                  rnet::phy::sync_word_low, rnet::phy::schema_hash);
    const int16_t status = rnet::configure_cc1101(radio, receiver_power_dbm);
    Serial.printf("cc1101 (RadioLib): %d (0 = ok)\n", status);
    if (status != RADIOLIB_ERR_NONE) {
        Serial.println("check wiring and 3.3 V; halted");
        for (;;) {
            delay(1000);
        }
    }
    Serial.println("listening...");
}

void loop()
{
    static uint32_t last_summary_ms = millis();
    uint8_t bytes[rnet::phy::max_packet_length];
    const int16_t status = radio.receive(bytes, sizeof bytes, receive_timeout_ms);
    if (status == RADIOLIB_ERR_NONE) {
        handle_packet(bytes, radio.getPacketLength(false),
                      packet_quality{radio.getRSSI(), radio.getLQI(), frequency_offset_khz()});
    } else if (status != RADIOLIB_ERR_RX_TIMEOUT) {
        Serial.printf("receive error %d\n", status);
    }
    if (millis() - last_summary_ms >= summary_period_ms) {
        last_summary_ms = millis();
        print_link_summary();
    }
}
