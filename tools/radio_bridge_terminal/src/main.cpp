// ESP32 DevKitC pin   bridge (weather board, USART1)
// GPIO16 (UART2 RX)   PA9  TX
// GPIO17 (UART2 TX)   PA10 RX
// GND                 GND
#include <Arduino.h>

namespace
{

constexpr uint32_t baud = 115200;
constexpr int bridge_receive_pin = 16;
constexpr int bridge_transmit_pin = 17;

void copy_available(Stream &from, Stream &to)
{
    while (from.available()) {
        to.write(static_cast<uint8_t>(from.read()));
    }
}

} // namespace

void setup()
{
    Serial.begin(baud);
    Serial2.begin(baud, SERIAL_8N1, bridge_receive_pin, bridge_transmit_pin);
    Serial.println("radio_bridge_terminal: bridge on GPIO16/17");
}

void loop()
{
    copy_available(Serial2, Serial);
    copy_available(Serial, Serial2);
}
