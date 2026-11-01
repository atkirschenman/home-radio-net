// acronym  meaning
// rnet     home radio network (this protocol)
// mic      message integrity code: truncated AES-CMAC
// lqi      link quality indicator (CC1101, lower is better)
//
// bridge lines (ADR-0003 B2)
//
//   ready <schema_hash>
//   received <rssi_dbm> <lqi> <hex bytes>
//   crc_error <rssi_dbm>
//   receive_error <status>
//   sent <status>
//   unknown_line
//   status <uptime_s> <received> <crc_errors> <schema_hash>
#include "radio_net.h"

#include <cstdlib>
#include <optional>

#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include "rnet_phy.hpp"

namespace esphome::radio_net
{

namespace
{

constexpr const char *tag = "radio_net";
constexpr std::size_t max_bridge_line_length = 200;
constexpr uint32_t bridge_status_period_milliseconds = 30000;
constexpr uint32_t bridge_offline_after_milliseconds = 3 * bridge_status_period_milliseconds + 5000;
constexpr uint8_t link_test_marker_first = 'L';
constexpr uint8_t link_test_marker_second = 'T';

std::vector<std::string> split_words(const std::string &line)
{
    std::vector<std::string> words;
    std::size_t start = 0;
    while (start < line.size()) {
        const std::size_t end = line.find(' ', start);
        const std::size_t word_end = end == std::string::npos ? line.size() : end;
        if (word_end > start) {
            words.push_back(line.substr(start, word_end - start));
        }
        start = word_end + 1;
    }
    return words;
}

std::optional<long> parse_integer(const std::string &word, int base)
{
    char *end = nullptr;
    const long value = std::strtol(word.c_str(), &end, base);
    if (word.empty() || end == nullptr || *end != '\0') {
        return std::nullopt;
    }
    return value;
}

std::optional<std::vector<uint8_t>> parse_hex_bytes(const std::string &hex)
{
    if (hex.empty() || hex.size() % 2 != 0) {
        return std::nullopt;
    }
    std::vector<uint8_t> bytes;
    bytes.reserve(hex.size() / 2);
    for (std::size_t index = 0; index < hex.size(); index += 2) {
        const auto value = parse_integer(hex.substr(index, 2), 16);
        if (!value) {
            return std::nullopt;
        }
        bytes.push_back(static_cast<uint8_t>(*value));
    }
    return bytes;
}

bool is_link_test_packet(const std::vector<uint8_t> &bytes)
{
    return bytes.size() >= 2 && bytes[0] == link_test_marker_first && bytes[1] == link_test_marker_second;
}

const char *verdict_name(rnet::replay_verdict verdict)
{
    switch (verdict) {
    case rnet::replay_verdict::fresh:
        return "fresh";
    case rnet::replay_verdict::duplicate:
        return "duplicate";
    case rnet::replay_verdict::old:
        return "old";
    }
    return "";
}

void log_weather_report(const registered_node &node, const rnet::parsed_frame &frame)
{
    rnet::weather_report report;
    report.unpack(frame.payload, frame.payload_length);
    ESP_LOGI(tag,
             "%s weather_report interval_seconds=%u wind_mean=%u wind_gust=%u wind_lull=%u wind_direction=%u "
             "rain_tips=%u temperature=%d relative_humidity=%u pressure=%u battery_voltage=%u flags=0x%02X",
             node.name.c_str(), report.interval_seconds, report.wind_mean, report.wind_gust, report.wind_lull,
             report.wind_direction, static_cast<unsigned>(report.rain_tips), report.temperature,
             report.relative_humidity, static_cast<unsigned>(report.pressure), report.battery_voltage,
             report.flags);
}

void log_health(const registered_node &node, const rnet::parsed_frame &frame)
{
    rnet::health health;
    health.unpack(frame.payload, frame.payload_length);
    ESP_LOGI(tag,
             "%s health battery_voltage=%u panel_voltage=%u board_temperature=%d downlink_rssi=%d uptime=%u "
             "firmware=%u.%u schema_hash=0x%04X%s reset_cause=%u",
             node.name.c_str(), health.battery_voltage, health.panel_voltage, health.board_temperature,
             health.downlink_rssi, static_cast<unsigned>(health.uptime), health.firmware_version >> 8,
             health.firmware_version & 0xFF, health.schema_hash,
             health.schema_hash == rnet::phy::schema_hash ? "" : " (MISMATCH)", health.reset_cause);
}

} // namespace

void radio_net_component::set_master_key(const rnet::aes_key &master_key)
{
    master_key_ = master_key;
}

void radio_net_component::add_node(uint16_t node_id, uint8_t node_type, const std::string &name)
{
    registered_node node;
    node.node_id = node_id;
    node.node_type = node_type;
    node.name = name;
    nodes_.push_back(node);
}

void radio_net_component::set_frames_received_sensor(sensor::Sensor *counter)
{
    frames_received_sensor_ = counter;
}

void radio_net_component::set_frames_rejected_sensor(sensor::Sensor *counter)
{
    frames_rejected_sensor_ = counter;
}

void radio_net_component::set_bridge_online_sensor(binary_sensor::BinarySensor *online)
{
    bridge_online_sensor_ = online;
}

float radio_net_component::get_setup_priority() const
{
    return setup_priority::DATA;
}

void radio_net_component::setup()
{
    if (frames_received_sensor_ != nullptr) {
        frames_received_sensor_->publish_state(0);
    }
    if (frames_rejected_sensor_ != nullptr) {
        frames_rejected_sensor_->publish_state(0);
    }
    if (bridge_online_sensor_ != nullptr) {
        bridge_online_sensor_->publish_state(false);
    }
}

void radio_net_component::loop()
{
    read_bridge_characters();
    update_bridge_online();
}

void radio_net_component::dump_config()
{
    ESP_LOGCONFIG(tag, "radio_net (bridge on UART, schema_hash 0x%04X)", rnet::phy::schema_hash);
    for (const registered_node &node : nodes_) {
        ESP_LOGCONFIG(tag, "  node 0x%04X type 0x%02X: %s", node.node_id, node.node_type, node.name.c_str());
    }
}

void radio_net_component::read_bridge_characters()
{
    uint8_t character = 0;
    while (available() > 0 && read_byte(&character)) {
        if (character == '\n') {
            handle_bridge_line(line_);
            line_.clear();
        } else if (character != '\r') {
            line_.push_back(static_cast<char>(character));
            if (line_.size() > max_bridge_line_length) {
                ESP_LOGW(tag, "bridge line too long, dropped");
                line_.clear();
            }
        }
    }
}

void radio_net_component::handle_bridge_line(const std::string &line)
{
    last_bridge_line_milliseconds_ = millis();
    bridge_heard_ = true;
    const std::vector<std::string> words = split_words(line);
    if (words.empty()) {
        return;
    }
    const std::string &kind = words[0];
    if (kind == "received" && words.size() == 4) {
        const auto rssi = parse_integer(words[1], 10);
        const auto lqi = parse_integer(words[2], 10);
        const auto bytes = parse_hex_bytes(words[3]);
        if (rssi && lqi && bytes) {
            handle_received_packet({*bytes, static_cast<int>(*rssi), static_cast<unsigned>(*lqi)});
            return;
        }
    } else if (kind == "status") {
        handle_status_line(words);
        return;
    } else if (kind == "crc_error") {
        count_rejected_frame();
        ESP_LOGD(tag, "bridge: %s", line.c_str());
        return;
    } else if (kind == "ready" || kind == "sent" || kind == "receive_error" || kind == "unknown_line") {
        ESP_LOGI(tag, "bridge: %s", line.c_str());
        return;
    }
    ESP_LOGW(tag, "bridge line not understood: %s", line.c_str());
}

void radio_net_component::handle_status_line(const std::vector<std::string> &words)
{
    const auto schema_hash = words.size() == 5 ? parse_integer(words[4], 16) : std::nullopt;
    if (schema_hash && *schema_hash != rnet::phy::schema_hash) {
        ESP_LOGW(tag, "bridge schema_hash 0x%04lX, gateway 0x%04X: rebuild them together", *schema_hash,
                 rnet::phy::schema_hash);
    }
    ESP_LOGD(tag, "bridge status uptime=%s received=%s crc_errors=%s", words.size() > 1 ? words[1].c_str() : "?",
             words.size() > 2 ? words[2].c_str() : "?", words.size() > 3 ? words[3].c_str() : "?");
}

void radio_net_component::handle_received_packet(const received_radio_packet &packet)
{
    if (is_link_test_packet(packet.bytes)) {
        ESP_LOGV(tag, "link test packet rssi=%d", packet.rssi_dbm);
        return;
    }
    rnet::parsed_frame frame;
    if (!rnet::parse_frame_without_mic_check(packet.bytes.data(), packet.bytes.size(), frame)) {
        count_rejected_frame();
        ESP_LOGW(tag, "not an rnet frame: %u bytes rssi=%d", static_cast<unsigned>(packet.bytes.size()),
                 packet.rssi_dbm);
        return;
    }
    const rnet::frame_header &header = frame.header;
    const rnet::aes_key node_key = rnet::derive_node_key(master_key_, header.node_id, header.node_type);
    if (!rnet::mic_is_valid(node_key, packet.bytes.data(), packet.bytes.size())) {
        count_rejected_frame();
        ESP_LOGW(tag, "mic INVALID: node 0x%04X type 0x%02X counter %u", header.node_id, header.node_type,
                 static_cast<unsigned>(header.counter));
        return;
    }
    registered_node *node = find_node(header.node_id, header.node_type);
    if (node == nullptr) {
        ESP_LOGI(tag, "unregistered node 0x%04X type 0x%02X (mic ok), not exposed", header.node_id,
                 header.node_type);
        return;
    }
    const rnet::replay_verdict verdict = node->counter.judge(header.counter);
    if (verdict == rnet::replay_verdict::duplicate) {
        ESP_LOGV(tag, "%s repeat of counter %u", node->name.c_str(), static_cast<unsigned>(header.counter));
        return;
    }
    if (verdict == rnet::replay_verdict::old) {
        count_rejected_frame();
        ESP_LOGW(tag, "%s counter %u is %s (last %u): replay rejected", node->name.c_str(),
                 static_cast<unsigned>(header.counter), verdict_name(verdict),
                 static_cast<unsigned>(node->counter.last_accepted()));
        return;
    }
    node->counter.accept(header.counter);
    handle_verified_frame(*node, frame, packet);
}

void radio_net_component::handle_verified_frame(registered_node &node, const rnet::parsed_frame &frame,
                                                const received_radio_packet &packet)
{
    count_received_frame();
    const rnet::frame_header &header = frame.header;
    ESP_LOGD(tag, "%s counter=%u message=0x%02X rssi=%d lqi=%u", node.name.c_str(),
             static_cast<unsigned>(header.counter), header.message_type, packet.rssi_dbm, packet.lqi);
    if (header.message_type == rnet::weather_report::id) {
        log_weather_report(node, frame);
    } else if (header.message_type == rnet::health::id) {
        log_health(node, frame);
    }
}

registered_node *radio_net_component::find_node(uint16_t node_id, uint8_t node_type)
{
    for (registered_node &node : nodes_) {
        if (node.node_id == node_id && node.node_type == node_type) {
            return &node;
        }
    }
    return nullptr;
}

void radio_net_component::count_received_frame()
{
    frames_received_++;
    if (frames_received_sensor_ != nullptr) {
        frames_received_sensor_->publish_state(static_cast<float>(frames_received_));
    }
}

void radio_net_component::count_rejected_frame()
{
    frames_rejected_++;
    if (frames_rejected_sensor_ != nullptr) {
        frames_rejected_sensor_->publish_state(static_cast<float>(frames_rejected_));
    }
}

void radio_net_component::update_bridge_online()
{
    if (bridge_online_sensor_ == nullptr) {
        return;
    }
    const bool online = bridge_heard_ && millis() - last_bridge_line_milliseconds_ < bridge_offline_after_milliseconds;
    if (!bridge_online_sensor_->has_state() || bridge_online_sensor_->state != online) {
        bridge_online_sensor_->publish_state(online);
    }
}

} // namespace esphome::radio_net
