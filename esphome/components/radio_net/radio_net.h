// acronym  meaning
// rnet     home radio network (this protocol)
// mic      message integrity code: truncated AES-CMAC
// lqi      link quality indicator (CC1101, lower is better)
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/uart/uart.h"
#include "esphome/core/component.h"
#include "rnet/rnet.hpp"

namespace esphome::radio_net
{

struct registered_node {
    uint16_t node_id = 0;
    uint8_t node_type = 0;
    std::string name;
    rnet::receive_counter counter;
};

struct received_radio_packet {
    std::vector<uint8_t> bytes;
    int rssi_dbm = 0;
    unsigned lqi = 0;
};

class radio_net_component : public Component, public uart::UARTDevice
{
  public:
    void set_master_key(const rnet::aes_key &master_key);
    void add_node(uint16_t node_id, uint8_t node_type, const std::string &name);
    void set_frames_received_sensor(sensor::Sensor *counter);
    void set_frames_rejected_sensor(sensor::Sensor *counter);
    void set_bridge_online_sensor(binary_sensor::BinarySensor *online);

    void setup() override;
    void loop() override;
    void dump_config() override;
    float get_setup_priority() const override;

  private:
    void read_bridge_characters();
    void handle_bridge_line(const std::string &line);
    void handle_received_packet(const received_radio_packet &packet);
    void handle_status_line(const std::vector<std::string> &words);
    void handle_verified_frame(registered_node &node, const rnet::parsed_frame &frame,
                               const received_radio_packet &packet);
    registered_node *find_node(uint16_t node_id, uint8_t node_type);
    void count_received_frame();
    void count_rejected_frame();
    void update_bridge_online();

    rnet::aes_key master_key_{};
    std::vector<registered_node> nodes_;
    std::string line_;
    uint32_t frames_received_ = 0;
    uint32_t frames_rejected_ = 0;
    uint32_t last_bridge_line_milliseconds_ = 0;
    bool bridge_heard_ = false;
    sensor::Sensor *frames_received_sensor_ = nullptr;
    sensor::Sensor *frames_rejected_sensor_ = nullptr;
    binary_sensor::BinarySensor *bridge_online_sensor_ = nullptr;
};

} // namespace esphome::radio_net
