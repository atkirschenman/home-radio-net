// Radio link-test receiver (bench only). ESP32 DevKitC + CC1101 module on
// VSPI. Prints every packet with RSSI/LQI, counts lost sequence numbers, and
// prints a summary every 30 s. Non-link-test packets are dumped in hex, so
// real rnet frames show up here too.
//
// Wiring (module pin -> DevKitC), 3.3 V only:
//   VCC -> 3V3   GND -> GND   SCK -> 18   MISO/SO -> 19   MOSI/SI -> 23
//   CSN -> 5     GDO0 -> 21 (unused by the driver)   GDO2 -> 22 (unused)
#include <Arduino.h>
#include <SPI.h>

extern "C" {
#include "linktest.h"
#include "node_l0/cc1101.h"
#include "rnet_phy.h"
}

namespace
{

constexpr int kSck = 18, kMiso = 19, kMosi = 23, kCsn = 5;
constexpr uint8_t kPatable = 0x1E; // -15 dBm; the receiver doesn't transmit here
constexpr uint32_t kSummaryMs = 30000;

SPISettings g_spi(1000000, MSBFIRST, SPI_MODE0);

void sel(void *, bool on)
{
    if (on) {
        SPI.beginTransaction(g_spi);
        digitalWrite(kCsn, LOW);
    } else {
        digitalWrite(kCsn, HIGH);
        SPI.endTransaction();
    }
}
uint8_t xfer(void *, uint8_t b)
{
    return SPI.transfer(b);
}
bool miso(void *)
{
    return digitalRead(kMiso) == HIGH;
}
void dly(void *, uint32_t us)
{
    delayMicroseconds(us);
}

const nl0_cc1101_bus_t kBus = {sel, xfer, miso, dly, nullptr};
nl0_cc1101_t g_radio;

struct Stats {
    uint32_t rx = 0, lost = 0, other = 0;
    int32_t rssi_sum = 0;
    int16_t rssi_min = 0, rssi_max = -200;
    bool have_seq = false;
    uint32_t last_seq = 0;
} g_st;

void verify_registers()
{
    int bad = 0;
    for (uint32_t i = 0; i < RNET_CC1101_NREGS; i++) {
        uint8_t a = rnet_cc1101_regs[i][0], want = rnet_cc1101_regs[i][1];
        uint8_t got = nl0_cc1101_read(&g_radio, a);
        if (got != want) {
            Serial.printf("  reg 0x%02X wrote 0x%02X read 0x%02X\n", a, want, got);
            bad++;
        }
    }
    Serial.printf("register readback: %s (%d of %u differ)\n", bad ? "FAIL" : "ok", bad,
                  (unsigned)RNET_CC1101_NREGS);
}

void summary()
{
    uint32_t total = g_st.rx + g_st.lost;
    Serial.printf(
        "== rx=%lu lost=%lu (%.1f%%) other=%lu rssi min/avg/max=%d/%ld/%d dBm\n",
        (unsigned long)g_st.rx, (unsigned long)g_st.lost, total ? 100.0 * g_st.lost / total : 0.0,
        (unsigned long)g_st.other, g_st.rx ? g_st.rssi_min : 0,
        g_st.rx ? (long)(g_st.rssi_sum / (int32_t)g_st.rx) : 0L, g_st.rx ? g_st.rssi_max : 0);
}

} // namespace

void setup()
{
    Serial.begin(115200);
    delay(500);
    pinMode(kCsn, OUTPUT);
    digitalWrite(kCsn, HIGH);
    SPI.begin(kSck, kMiso, kMosi, -1);

    Serial.printf("\nrnet link test receiver: %.4f MHz, %u bps, sync 0x%04X, schema 0x%04X\n",
                  RNET_PHY_FREQ_HZ / 1e6, (unsigned)RNET_PHY_DATA_RATE_BPS,
                  (unsigned)RNET_PHY_SYNC_WORD, (unsigned)RNET_SCHEMA_HASH);
    nl0_cc1101_err_t e = nl0_cc1101_init(&g_radio, &kBus, kPatable);
    Serial.printf("cc1101 init: %d (0 = ok), VERSION 0x%02X\n", (int)e, g_radio.version);
    if (e != NL0_CC1101_OK) {
        Serial.println("check wiring and 3.3 V; halted");
        for (;;) {
            delay(1000);
        }
    }
    verify_registers();
    Serial.println("listening...");
}

void loop()
{
    static uint32_t last_summary = millis();
    uint8_t buf[RNET_PHY_MAX_PAYLOAD];
    nl0_cc1101_rx_t rx;
    if (nl0_cc1101_recv(&g_radio, buf, &rx, 1000) == NL0_CC1101_OK) {
        uint32_t seq;
        uint8_t pt;
        uint16_t vbat;
        if (linktest_parse(buf, rx.len, &seq, &pt, &vbat)) {
            uint32_t gap = 0;
            if (g_st.have_seq && seq > g_st.last_seq + 1) {
                gap = seq - g_st.last_seq - 1;
            }
            if (g_st.have_seq && seq <= g_st.last_seq) {
                Serial.println("(sequence restarted: node reset)");
            }
            g_st.lost += gap;
            g_st.rx++;
            g_st.rssi_sum += rx.rssi;
            g_st.rssi_min = g_st.rx == 1 ? rx.rssi : min(g_st.rssi_min, rx.rssi);
            g_st.rssi_max = max(g_st.rssi_max, rx.rssi);
            g_st.have_seq = true;
            g_st.last_seq = seq;
            Serial.printf("seq=%lu rssi=%d lqi=%u pa=0x%02X vbat=%u%s\n", (unsigned long)seq,
                          rx.rssi, rx.lqi, pt, vbat, gap ? " (gap)" : "");
        } else {
            g_st.other++;
            Serial.printf("other len=%u rssi=%d:", rx.len, rx.rssi);
            for (uint8_t i = 0; i < rx.len; i++) {
                Serial.printf(" %02X", buf[i]);
            }
            Serial.println();
        }
    }
    if (millis() - last_summary >= kSummaryMs) {
        last_summary = millis();
        summary();
    }
}
