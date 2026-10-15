/**
 * @file survey.cpp
 * @brief See survey.h and spec §7.9.
 */

#include "survey.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "aes128.h"
#include "board_pins.h"
#include "console.h"
#include "radio_config.h"

namespace {

SX1262    *g_radio = nullptr;
uint8_t    g_key[16];
SurveyRole g_role = SurveyRole::Idle;

SurveyConfig g_cfg = {
    LORA_FREQ_MHZ, LORA_BANDWIDTH_KHZ, LORA_SPREADING_FACTOR,
    LORA_TX_POWER_DBM, SURVEY_DEFAULT_ANT_DBI, SURVEY_BEACON_INTERVAL_MS,
    SURVEY_REQUIRED_MARGIN_DB,
};

SurveyStats g_stats{};
PerWindow   g_per;

volatile bool g_irq = false;
bool          g_transmitting = false;
uint32_t      g_nextBeaconAt = 0;
uint16_t      g_beaconSeq = 0;

SurveyMark g_marks[SURVEY_MAX_MARKS];
uint8_t    g_markCount = 0;

uint8_t g_txBuf[TLP_BEACON_LEN];

void startReceiving() {
    if (g_radio == nullptr) return;
    const int16_t st = g_radio->startReceive();
    g_transmitting = false;
    if (st != RADIOLIB_ERR_NONE) {
        console::logf("[survey] startReceive failed: %d", st);
    }
}

void sendBeacon() {
    if (g_radio == nullptr) return;

    TlpBeacon b{};
    b.seq          = ++g_beaconSeq;
    b.tx_dbm       = g_cfg.tx_dbm;
    b.ant_gain_qdb = tlp_gain_to_qdb(g_cfg.ant_dbi);
    b.noise_dbm    = static_cast<int8_t>(lroundf(g_stats.ourNoiseDbm));
    b.phy          = tlp_phy(g_cfg.sf, (g_cfg.bw_khz < 100.0f) ? TLP_BW_62_5 : TLP_BW_125);
    b.mins_left    = 255;   // survey mode here is manual; the base station enforces a limit

    tlp_encode_beacon(b, g_key, g_txBuf);

    const int16_t st = g_radio->startTransmit(g_txBuf, TLP_BEACON_LEN);
    if (st == RADIOLIB_ERR_NONE) {
        g_transmitting = true;
    } else {
        console::logf("[survey] startTransmit failed: %d", st);
        startReceiving();
    }
}

/// Link estimate for a given RSSI, against the far end we last heard from.
/// Used twice with different inputs: the last frame drives the live display,
/// the window mean drives what gets recorded by `mark`.
LinkEstimate estimateFrom(float rssi_dbm, float snr_db) {
    LinkInputs in{};
    in.rssi_dbm      = rssi_dbm;
    in.snr_db        = snr_db;
    in.far_tx_dbm    = static_cast<float>(g_stats.last.tx_dbm);
    in.far_ant_dbi   = tlp_gain_from_qdb(g_stats.last.ant_gain_qdb);
    in.far_noise_dbm = static_cast<float>(g_stats.last.noise_dbm);
    in.our_tx_dbm    = static_cast<float>(g_cfg.tx_dbm);
    in.our_ant_dbi   = g_cfg.ant_dbi;
    return link_estimate(in, g_cfg.required_margin_db);
}

void handleFrame() {
    uint8_t  buf[64];
    const size_t avail = g_radio->getPacketLength();
    const size_t len   = (avail > sizeof(buf)) ? sizeof(buf) : avail;

    const int16_t st = g_radio->readData(buf, len);
    if (st != RADIOLIB_ERR_NONE) {
        g_stats.rejected++;
        return;
    }

    TlpBeacon b{};
    if (!tlp_decode_beacon(buf, len, g_key, &b)) {
        g_stats.rejected++;   // wrong length, wrong version, or a failed MIC
        return;
    }

    // A long silence means you walked. Start a fresh window rather than booking
    // every beacon sent during the walk as a loss at the new site — the same
    // failure that setRole() resets guard against, except a range walk never
    // changes role, so the gap itself has to be the trigger.
    if (g_stats.haveBeacon) {
        const uint32_t gap = millis() - g_stats.lastHeardMs;
        if (gap > SURVEY_WINDOW_GAP_MS) {
            survey::resetStats();       // preserves ourNoiseDbm
            console::logf("-- new window: %lu s silence, previous site discarded --",
                          static_cast<unsigned long>(gap / 1000UL));
        }
    }

    g_stats.haveBeacon  = true;
    g_stats.last        = b;
    g_stats.lastRssi    = g_radio->getRSSI();
    g_stats.lastSnr     = g_radio->getSNR();
    g_stats.lastHeardMs = millis();
    g_per.onBeacon(b.seq);

    if (g_stats.rssiCount == 0) {
        g_stats.rssiMin = g_stats.lastRssi;
        g_stats.rssiMax = g_stats.lastRssi;
        g_stats.rssiMinAtMs = g_stats.lastHeardMs;
    } else {
        if (g_stats.lastRssi < g_stats.rssiMin) {
            g_stats.rssiMin = g_stats.lastRssi;
            g_stats.rssiMinAtMs = g_stats.lastHeardMs;
        }
        if (g_stats.lastRssi > g_stats.rssiMax) g_stats.rssiMax = g_stats.lastRssi;
    }
    g_stats.rssiSum += g_stats.lastRssi;
    g_stats.snrSum  += g_stats.lastSnr;
    g_stats.rssiCount++;

    // Deep fades are judged against the best frame seen so far. The reference
    // can only improve as the window fills, so an early frame is never counted
    // against a max it predates — the count is therefore a lower bound, which
    // is the right direction to be wrong in.
    if (g_stats.lastRssi <= (g_stats.rssiMax - SURVEY_DEEP_FADE_DB)) {
        g_stats.deepFades++;
        g_stats.fadeRunNow++;
        if (g_stats.fadeRunNow > g_stats.fadeRunMax) g_stats.fadeRunMax = g_stats.fadeRunNow;
    } else {
        g_stats.fadeRunNow = 0;
    }

    g_stats.est = estimateFrom(g_stats.lastRssi, g_stats.lastSnr);
}

int cmpFloat(const void *a, const void *b) {
    const float fa = *static_cast<const float *>(a);
    const float fb = *static_cast<const float *>(b);
    return (fa < fb) ? -1 : ((fa > fb) ? 1 : 0);
}

}  // namespace

float surveyMeanRssi(const SurveyStats &s) {
    return (s.rssiCount > 0) ? (s.rssiSum / static_cast<float>(s.rssiCount)) : s.lastRssi;
}

/// Seconds from the START OF THIS MEASUREMENT to the deepest frame. Recording
/// raw uptime instead put "worst at t+253 s" on a 130-second window, which is
/// not a number anyone can act on.
uint32_t surveyFadeAtS(const SurveyStats &s) {
    if (s.rssiCount == 0) return 0;
    const uint32_t d = s.rssiMinAtMs - s.windowStartMs;   // unsigned wrap is fine
    return d / 1000UL;
}

float surveyMeanSnr(const SurveyStats &s) {
    return (s.rssiCount > 0) ? (s.snrSum / static_cast<float>(s.rssiCount)) : s.lastSnr;
}

namespace survey {

void begin(SX1262 *radio, const uint8_t key[16]) {
    g_radio = radio;
    memcpy(g_key, key, 16);
    resetStats();
}

SurveyConfig &config() { return g_cfg; }

int16_t applyRadio() {
    if (g_radio == nullptr) return RADIOLIB_ERR_UNKNOWN;

    int16_t st = g_radio->setFrequency(g_cfg.freq_mhz);
    if (st != RADIOLIB_ERR_NONE) return st;
    st = g_radio->setBandwidth(g_cfg.bw_khz);
    if (st != RADIOLIB_ERR_NONE) return st;
    st = g_radio->setSpreadingFactor(g_cfg.sf);
    if (st != RADIOLIB_ERR_NONE) return st;
    st = g_radio->setOutputPower(g_cfg.tx_dbm);
    if (st != RADIOLIB_ERR_NONE) return st;

    startReceiving();
    return RADIOLIB_ERR_NONE;
}

/// Entering a role starts a NEW measurement, at this site, now.
///
/// This reset is not a nicety. Without it, walking the line and typing
/// `role listen` at each trap accumulates the previous site's counters — and
/// worse, every beacon transmitted while you were walking between sites counts
/// as missed. `PerWindow::onBeacon` widens `expected_` by the whole sequence
/// gap, so arriving at trap 8 after a ten-minute walk from trap 7 books ~300
/// losses against the new site before it has heard anything. Delivery rate is
/// the headline number of the entire survey, and every site after the first
/// would have reported a fiction.
void setRole(SurveyRole role) {
    g_role = role;
    g_transmitting = false;
    if (g_radio == nullptr) return;

    if (role == SurveyRole::Idle) {
        g_radio->standby();
        return;
    }
    startReceiving();

    // Measure before resetting: resetStats() preserves the noise figure, and
    // both roles need one. A listener that skipped this carried ourNoiseDbm =
    // 0 until it had once been a beacon, and then carried the *previous site's*
    // floor for the rest of the day.
    g_stats.ourNoiseDbm = measureNoiseFloor(noiseWindowMs());
    resetStats();

    if (role == SurveyRole::Beacon) {
        g_nextBeaconAt = millis() + 500;
    }
}

SurveyRole role() { return g_role; }

void onRadioIrq() { g_irq = true; }

namespace {
/// The radio half of loop(), so noise sampling can pump it between reads.
void serviceRadio() {
    if (!g_irq) return;
    g_irq = false;
    if (g_transmitting) {
        g_radio->finishTransmit();
    } else {
        handleFrame();
    }
    startReceiving();
}
}  // namespace

void loop() {
    if (g_radio == nullptr || g_role == SurveyRole::Idle) return;

    serviceRadio();

    if (g_role == SurveyRole::Beacon && !g_transmitting) {
        const uint32_t now = millis();
        if (static_cast<int32_t>(now - g_nextBeaconAt) >= 0) {
            // Refresh our own noise floor occasionally so the beacon carries a
            // current figure rather than one measured at power-up. The fast
            // window is right here and only here: we are between our own
            // transmissions, and during a survey this is the only transmitter
            // on the channel, so there is nothing for a long window to dodge.
            if ((g_beaconSeq % 16) == 0) {
                g_stats.ourNoiseDbm = measureNoiseFloor(SURVEY_NOISE_FAST_MS);
            }
            g_nextBeaconAt = now + g_cfg.beacon_interval_ms;
            sendBeacon();
        }
    }
}

LinkEstimate windowEstimate() {
    return estimateFrom(surveyMeanRssi(g_stats), surveyMeanSnr(g_stats));
}

const SurveyStats &stats() { return g_stats; }
const PerWindow   &per() { return g_per; }

void resetStats() {
    const float keepNoise = g_stats.ourNoiseDbm;
    g_stats = SurveyStats{};
    g_stats.ourNoiseDbm = keepNoise;
    g_stats.windowStartMs = millis();
    g_per.reset();
    g_beaconSeq = 0;
}

float remeasureNoiseFloor() {
    g_stats.ourNoiseDbm = measureNoiseFloor(noiseWindowMs());
    return g_stats.ourNoiseDbm;
}

uint32_t noiseWindowMs() {
    uint32_t w = g_cfg.beacon_interval_ms + SURVEY_NOISE_WINDOW_GUARD_MS;
    if (w < SURVEY_NOISE_WINDOW_MIN_MS) w = SURVEY_NOISE_WINDOW_MIN_MS;
    if (w > SURVEY_NOISE_WINDOW_MAX_MS) w = SURVEY_NOISE_WINDOW_MAX_MS;
    return w;
}

float measureNoiseFloor(uint32_t window_ms) {
    if (g_radio == nullptr) return SURVEY_NOISE_UNMEASURED_DBM;

    if (g_transmitting) return g_stats.ourNoiseDbm;

    // serviceRadio() below can reach handleFrame(); keep it out of here.
    static bool busy = false;
    if (busy) return g_stats.ourNoiseDbm;
    busy = true;

    // GetRssiInst is only meaningful in receive. In `role idle` the radio sits
    // in standby, the register rails at 0xFF, and RadioLib faithfully converts
    // that to -127.5 dBm — which reads as an impossibly quiet site rather than
    // as the non-measurement it is. Drop into RX for the duration and put it
    // back afterwards.
    const bool wasIdle = (g_role == SurveyRole::Idle);
    if (wasIdle) startReceiving();

    const uint32_t gap = window_ms / SURVEY_NOISE_SAMPLES;

    float s[SURVEY_NOISE_SAMPLES];
    for (int i = 0; i < SURVEY_NOISE_SAMPLES; i++) {
        s[i] = g_radio->getRSSI(false);   // false = instantaneous channel RSSI
        // Keep receiving while we sample, so a long window costs no beacons.
        // A beacon that arrives mid-window raises one or two samples; that is
        // exactly the minority case the median is there to discard.
        const uint32_t until = millis() + gap;
        while (static_cast<int32_t>(millis() - until) < 0) {
            serviceRadio();
            delay(1);
        }
    }
    qsort(s, SURVEY_NOISE_SAMPLES, sizeof(float), cmpFloat);
    const float median = s[SURVEY_NOISE_SAMPLES / 2];

    if (wasIdle) g_radio->standby();
    busy = false;

    if (median <= SURVEY_NOISE_RAILED_DBM) {
        // Railed. Keep whatever we last knew rather than poisoning the record
        // and the outgoing beacon with a number that means "not measured".
        console::log("[survey] noise read railed — radio was not receiving");
        return g_stats.ourNoiseDbm;
    }
    return median;
}

bool mark(const char *label) {
    if (g_markCount >= SURVEY_MAX_MARKS) return false;
    if (!g_stats.haveBeacon) return false;

    SurveyMark &m = g_marks[g_markCount];
    memset(&m, 0, sizeof(m));
    snprintf(m.label, sizeof(m.label), "%s", (label != nullptr) ? label : "unlabelled");

    // Record the WINDOW, not the last frame, and derive path loss and margins
    // from the mean too — otherwise a site's whole record is decided by which
    // packet happened to arrive in the second before someone typed `mark`.
    const float meanRssi = surveyMeanRssi(g_stats);
    const float meanSnr  = surveyMeanSnr(g_stats);
    const LinkEstimate est = windowEstimate();

    m.uptimeS       = millis() / 1000UL;
    m.rssi          = meanRssi;
    m.rssiMin       = (g_stats.rssiCount > 0) ? g_stats.rssiMin : g_stats.lastRssi;
    m.rssiMax       = (g_stats.rssiCount > 0) ? g_stats.rssiMax : g_stats.lastRssi;
    m.rssiLast      = g_stats.lastRssi;
    m.deepFades     = g_stats.deepFades;
    m.fadeRunMax    = g_stats.fadeRunMax;
    m.worstAtS      = surveyFadeAtS(g_stats);
    m.snr           = meanSnr;
    m.pathLoss      = est.path_loss_db;
    m.predSnr       = est.predicted_snr_db;
    m.deliveryPct   = g_per.deliveryPercent();
    m.heard         = g_per.heard();
    m.expected      = g_per.expected();
    m.sf            = g_cfg.sf;
    m.bwKhz         = g_cfg.bw_khz;
    m.ourNoise      = g_stats.ourNoiseDbm;
    m.farNoise      = static_cast<float>(g_stats.last.noise_dbm);
    m.recommendedSf = est.any_sf_usable ? est.recommended_sf : 0;
    g_markCount++;
    return true;
}

uint8_t markCount() { return g_markCount; }

const SurveyMark &markAt(uint8_t i) { return g_marks[i < g_markCount ? i : 0]; }

void clearMarks() { g_markCount = 0; }

#define SURVEY_CSV_HEADER                                                            \
    "label,uptime_s,rssi_dbm,rssi_min_dbm,rssi_max_dbm,rssi_last_dbm,snr_db,"        \
    "path_loss_db,pred_snr_db,delivery_pct,heard,expected,deep_fades,fade_run_max,"  \
    "worst_at_s,sf,bw_khz,our_noise_dbm,far_noise_dbm,recommended_sf"

// Add a column and this fails the build, rather than silently shearing the end
// off the header and producing a CSV whose last column is mislabelled.
static_assert(sizeof(SURVEY_CSV_HEADER) <= SURVEY_CSV_MIN_BUF,
              "SURVEY_CSV_MIN_BUF is too small for SURVEY_CSV_HEADER");

void csvHeader(char *buf, size_t len) {
    snprintf(buf, len, "%s", SURVEY_CSV_HEADER);
}

void csvLine(const SurveyMark &m, char *buf, size_t len) {
    // No %f — newlib-nano on this core omits float support from printf.
    auto t10 = [](float v) {
        if (!(v > -99999.0f && v < 99999.0f)) return 0L;   // also catches NaN
        return static_cast<long>(lroundf(v * 10.0f));
    };
    char rssi[12], rmin[12], rmax[12], rlast[12], snr[12], pl[12], psnr[12], onoise[12], fnoise[12], bw[12];
    // %c for the fraction (exactly one character), and the whole part is
    // clamped here rather than relying on the caller — otherwise the compiler
    // must assume a full-width long and warns that 12 bytes may not be enough.
    auto fmt = [](long tenths, char *out, size_t n) {
        if (tenths > 999999L) tenths = 999999L;
        if (tenths < -999999L) tenths = -999999L;
        const char *sign = tenths < 0 ? "-" : "";
        const long mag = tenths < 0 ? -tenths : tenths;
        snprintf(out, n, "%s%ld.%c", sign, mag / 10, static_cast<char>('0' + (mag % 10)));
    };
    fmt(t10(m.rssi), rssi, sizeof(rssi));
    fmt(t10(m.rssiMin), rmin, sizeof(rmin));
    fmt(t10(m.rssiMax), rmax, sizeof(rmax));
    fmt(t10(m.rssiLast), rlast, sizeof(rlast));
    fmt(t10(m.snr), snr, sizeof(snr));
    fmt(t10(m.pathLoss), pl, sizeof(pl));
    fmt(t10(m.predSnr), psnr, sizeof(psnr));
    fmt(t10(m.ourNoise), onoise, sizeof(onoise));
    fmt(t10(m.farNoise), fnoise, sizeof(fnoise));
    fmt(t10(m.bwKhz), bw, sizeof(bw));

    snprintf(buf, len, "%s,%lu,%s,%s,%s,%s,%s,%s,%s,%d,%lu,%lu,%lu,%lu,%lu,%u,%s,%s,%s,%u", m.label,
             static_cast<unsigned long>(m.uptimeS), rssi, rmin, rmax, rlast, snr, pl, psnr,
             m.deliveryPct,
             static_cast<unsigned long>(m.heard), static_cast<unsigned long>(m.expected),
             static_cast<unsigned long>(m.deepFades),
             static_cast<unsigned long>(m.fadeRunMax),
             static_cast<unsigned long>(m.worstAtS),
             m.sf, bw, onoise, fnoise, m.recommendedSf);
}

}  // namespace survey
