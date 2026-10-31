// Bench receiver (bench only). ESP32 DevKitC + CC1101 module on VSPI.
//  - Link-test packets: RSSI/LQI each, lost sequence numbers, a summary
//    every 30 s.
//  - rnet frames: header, MIC checked under the key derived from the
//    published TEST master key (unprovisioned nodes use it), replay check per
//    node, WX_REPORT and HEALTH decoded into units. Anything else: hex dump.
//
// Wiring (module pin -> DevKitC), 3.3 V only:
//   VCC -> 3V3   GND -> GND   SCK -> 18   MISO/SO -> 19   MOSI/SI -> 23
//   CSN -> 5     GDO0 -> 21 (unused by the driver)   GDO2 -> 22 (unused)
#include <Arduino.h>
#include <SPI.h>

extern "C" {
#include "linktest.h"
#include "node_l0/cc1101.h"
#include "node_l0/rnet_node.h"
#include "rnet/rnet.h"
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

// ---- rnet frames ----
struct Peer {
    bool used;
    uint16_t node_id;
    uint8_t node_type;
    rnet_rx_state_t rx;
};
Peer g_peers[8];

Peer *peer(uint16_t id, uint8_t type)
{
    for (Peer &p : g_peers) {
        if (p.used && p.node_id == id && p.node_type == type) {
            return &p;
        }
    }
    for (Peer &p : g_peers) {
        if (!p.used) {
            p = Peer{true, id, type, {}};
            return &p;
        }
    }
    return nullptr;
}

uint32_t rain_nm_per_tip(uint8_t type)
{
    switch (type) {
    case RNET_NODE_WEATHER_ACURITE:
        return RNET_NODE_WEATHER_ACURITE_RAIN_NM_PER_TIP;
    case RNET_NODE_WEATHER_WS69:
        return RNET_NODE_WEATHER_WS69_RAIN_NM_PER_TIP;
    case RNET_NODE_WEATHER_MISOL:
        return RNET_NODE_WEATHER_MISOL_RAIN_NM_PER_TIP;
    default:
        return 0;
    }
}

void print_dir(const char *k, uint16_t d)
{
    if (d == 0xFFFF) {
        Serial.printf(" %s=-", k);
    } else {
        Serial.printf(" %s=%.1f", k, d / 10.0);
    }
}

void print_wx(const rnet_wx_report_t &m, uint8_t type)
{
    Serial.printf("  WX int=%us wind mean/gust/lull=%.2f/%.2f/%.2f m/s", m.interval_s,
                  m.wind_mean / 100.0, m.wind_gust / 100.0, m.wind_lull / 100.0);
    print_dir("dir", m.wind_dir);
    print_dir("dir_gust", m.wind_dir_gust);
    Serial.printf("\n     rain=%lu tips (%.2f mm)", (unsigned long)m.rain_tips,
                  m.rain_tips * (double)rain_nm_per_tip(type) / 1e6);
    if (m.temp == RNET_WX_REPORT_TEMP_INVALID) {
        Serial.print(" T=-");
    } else {
        Serial.printf(" T=%.2f C", m.temp / 100.0);
    }
    if (m.rh == RNET_WX_REPORT_RH_INVALID) {
        Serial.print(" RH=-");
    } else {
        Serial.printf(" RH=%.1f %%", m.rh / 100.0);
    }
    if (m.pressure == RNET_WX_REPORT_PRESSURE_INVALID) {
        Serial.print(" P=-");
    } else {
        Serial.printf(" P=%.2f hPa", m.pressure / 100.0);
    }
    Serial.printf(" vbat=%u mV flags=0x%02X", m.vbat, m.flags);
    if (m.flags & RNET_WX_REPORT_FLAGS_PREV_VALID) {
        Serial.printf(" prev=%.2f/%.2f/%.2f", m.prev_mean / 100.0, m.prev_gust / 100.0,
                      m.prev_lull / 100.0);
    }
    Serial.println();
}

void print_health(const rnet_health_t &m)
{
    Serial.printf("  HEALTH vbat=%u vpanel=%u mV board=%.1f C dl_rssi=%d up=%lus fw=%u.%u "
                  "schema=0x%04X%s reset=%u\n",
                  m.vbat, m.vpanel, m.board_temp / 10.0, m.dl_rssi, (unsigned long)m.uptime,
                  m.fw_version >> 8, m.fw_version & 0xFF, m.schema_hash,
                  m.schema_hash == RNET_SCHEMA_HASH ? "" : " (MISMATCH)", m.reset_cause);
}

// True if buf was an rnet frame (whatever its fate), false to hex-dump it.
bool handle_rnet(const uint8_t *buf, uint8_t len, const nl0_cc1101_rx_t &rx)
{
    rnet_hdr_t h;
    const uint8_t *pl;
    size_t pl_len;
    if (!rnet_frame_parse(buf, len, &h, &pl, &pl_len)) {
        return false;
    }
    uint8_t key[RNET_KEY_LEN];
    rnet_derive_key(nl0_test_master_key, h.node_id, h.node_type, key);
    Serial.printf("rnet node=0x%04X type=0x%02X net=0x%02X ctr=%lu msg=0x%02X ctrl=0x%02X "
                  "rssi=%d lqi=%u",
                  h.node_id, h.node_type, h.net_id, (unsigned long)h.ctr, h.msg_type, h.ctrl,
                  rx.rssi, rx.lqi);
    if (!rnet_frame_verify(key, buf, len)) {
        Serial.println(" BAD MIC (not the test key, or corrupt)");
        return true;
    }
    Peer *p = peer(h.node_id, h.node_type);
    rnet_replay_t r = p ? rnet_replay_check(&p->rx, h.ctr) : RNET_FRESH;
    if (r == RNET_DUPLICATE) {
        Serial.println(" (repeat)");
        return true;
    }
    if (r == RNET_REPLAY) {
        Serial.printf(" REPLAY/old counter (last %lu)\n", (unsigned long)p->rx.last);
        return true;
    }
    if (p) {
        rnet_replay_accept(&p->rx, h.ctr);
    }
    Serial.println(h.ctrl & RNET_CTRL_RX_OPEN ? " RX_OPEN" : "");
    if (h.msg_type == RNET_MSG_WX_REPORT) {
        rnet_wx_report_t m;
        rnet_wx_report_unpack(&m, pl, pl_len);
        print_wx(m, h.node_type);
    } else if (h.msg_type == RNET_MSG_HEALTH) {
        rnet_health_t m;
        rnet_health_unpack(&m, pl, pl_len);
        print_health(m);
    }
    return true;
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
        } else if (handle_rnet(buf, rx.len, rx)) {
            // printed
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
