// acronym  meaning
// csn      chip select, active low
//
// each line in turn: 3 s at 3.3 V, then 3 s at 0 V; chip select held high, so the
// CC1101 leaves MISO released and the ESP32 may drive it
//
//   line  ESP32 pin  module pin
//   SCK   18         15
//   MOSI  23         13
//   CSN   5          14
//   MISO  19         12
#include <Arduino.h>

namespace
{

struct driven_line {
    int esp32_pin;
    int module_pin;
    const char *name;
};

constexpr int csn_pin = 5;
constexpr uint32_t level_hold_ms = 3000;
constexpr driven_line lines[] = {{18, 15, "SCK"}, {23, 13, "MOSI"}, {csn_pin, 14, "CSN"}, {19, 12, "MISO"}};

void hold(const driven_line &line, int level)
{
    digitalWrite(line.esp32_pin, level);
    Serial.printf("%s (ESP32 D%d, module pin %d) = %s\n", line.name, line.esp32_pin, line.module_pin,
                  level == HIGH ? "3.3 V" : "0 V");
    delay(level_hold_ms);
}

} // namespace

void setup()
{
    Serial.begin(115200);
    for (const driven_line &line : lines) {
        pinMode(line.esp32_pin, OUTPUT);
        digitalWrite(line.esp32_pin, line.esp32_pin == csn_pin ? HIGH : LOW);
    }
}

void loop()
{
    for (const driven_line &line : lines) {
        hold(line, HIGH);
        hold(line, LOW);
        digitalWrite(line.esp32_pin, line.esp32_pin == csn_pin ? HIGH : LOW);
    }
}
