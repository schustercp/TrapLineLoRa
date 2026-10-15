/**
 * @file survey.h
 * @brief Survey mode — spec §7.9, and the Phase 0 tool.
 *
 * One firmware, two roles. Put one handheld in `beacon` and carry the other in
 * `listen`; walk the line measuring adjacent trap sites. Roles switch over BLE,
 * so you never need a laptop in the field and you can swap ends without
 * reflashing.
 *
 * What matters here is that we measure **packet error rate**, not just RSSI.
 * A site at -118 dBm with 94 % delivery is usable; the same RSSI at 60 %
 * delivery is not, and RSSI alone cannot tell them apart.
 */

#pragma once

#include <Arduino.h>
#include <RadioLib.h>

#include "link_math.h"
#include "radio_config.h"   // SURVEY_NOISE_FAST_MS, used as a default argument
#include "tlp.h"

enum class SurveyRole : uint8_t { Idle, Beacon, Listen };

#define SURVEY_MAX_MARKS 48
#define SURVEY_LABEL_LEN 24

struct SurveyConfig {
    float    freq_mhz;
    float    bw_khz;
    uint8_t  sf;
    int8_t   tx_dbm;
    float    ant_dbi;             ///< this unit's antenna gain
    uint32_t beacon_interval_ms;
    float    required_margin_db;  ///< fade margin demanded of a "usable" link
};

struct SurveyStats {
    bool         haveBeacon;
    TlpBeacon    last;
    float        lastRssi;
    float        lastSnr;
    uint32_t     lastHeardMs;
    float        ourNoiseDbm;
    LinkEstimate est;        ///< from the LAST frame — for the live display
    uint32_t     rejected;   ///< frames that failed length/version/MIC

    // Window summary. One packet's RSSI swings about 10 dB on multipath even
    // across a still desk — a bench run recorded -66.0 dBm and -56.0 dBm three
    // seconds apart at a fixed separation — so a site is characterised by the
    // whole window, never by whichever frame happened to land last. Delivery
    // rate stayed at 100 % across that same swing, which is the entire reason
    // packet error rate is the headline and RSSI is the supporting detail.
    float    rssiSum;
    float    snrSum;
    float    rssiMin;
    float    rssiMax;
    uint32_t rssiCount;

    // Fade DEPTH alone does not identify a fault. A first two-way desk run
    // measured a 19.0 dB spread one way and 2.0 dB the other, across the same
    // pair of boards minutes apart — about nine frames in sixty-four sitting
    // ~15 dB down. Whether those nine were consecutive (something crossed the
    // path: a person, a door, a branch) or scattered through the window (an
    // intermittent u.FL, a marginal RF switch) is the whole diagnosis, and
    // min/max cannot tell them apart. These two can.
    uint32_t deepFades;     ///< frames >= SURVEY_DEEP_FADE_DB below the window best
    uint32_t fadeRunMax;    ///< longest CONSECUTIVE run of them
    uint32_t fadeRunNow;
    uint32_t rssiMinAtMs;   ///< uptime of the deepest frame
    uint32_t windowStartMs; ///< when this measurement began — fades are timed from here
};

/// Mean RSSI over the window, or the last frame's if nothing accumulated yet.
float surveyMeanRssi(const SurveyStats &s);
float surveyMeanSnr(const SurveyStats &s);

/// Seconds from the start of THIS measurement to the deepest frame — not raw
/// uptime, which produced "worst at t+253 s" inside a 130-second window.
uint32_t surveyFadeAtS(const SurveyStats &s);

struct SurveyMark {
    char     label[SURVEY_LABEL_LEN];
    uint32_t uptimeS;
    float    rssi;        ///< MEAN over the window, not the last frame
    float    rssiMin;     ///< deepest fade seen — the one that decides the link
    float    rssiMax;
    float    rssiLast;
    uint32_t deepFades;   ///< how many frames were >=10 dB down
    uint32_t fadeRunMax;  ///< and whether they came in a block or scattered
    uint32_t worstAtS;
    float    snr;         ///< mean
    float    pathLoss;
    float    predSnr;
    int      deliveryPct;
    uint32_t heard;
    uint32_t expected;
    uint8_t  sf;
    float    bwKhz;
    float    ourNoise;
    float    farNoise;
    uint8_t  recommendedSf;
};

namespace survey {

/// Bind to the radio and network key. Does not touch the radio yet.
void begin(SX1262 *radio, const uint8_t key[16]);

SurveyConfig &config();

/// Push the current config to the radio. Returns a RadioLib status code.
int16_t applyRadio();

void       setRole(SurveyRole role);
SurveyRole role();

/// Call from loop(). Drives beacon timing and beacon reception.
void loop();

/// Called from the DIO1 ISR context — just sets a flag.
void onRadioIrq();

/// Link estimate from the WINDOW MEAN — the same numbers `mark` records.
/// The live display must not show a verdict derived from the last frame
/// while the CSV stores one derived from the mean; that way a site reads
/// GOOD on the phone and MARGINAL in the data.
LinkEstimate windowEstimate();

const SurveyStats &stats();
const PerWindow   &per();
void               resetStats();

/// Sample the channel, median of SURVEY_NOISE_SAMPLES reads, in dBm.
///
/// `window_ms` is how long the samples are spread over, and it matters more
/// than it looks. The SX1262 reports *instantaneous* RSSI, so a partner's
/// transmission raises every sample taken during it. One SF10 packet lasts
/// 289 ms — ten times longer than the original 30 ms sampling burst — so a
/// burst falling inside a packet has ALL of its samples contaminated, and the
/// median cannot rescue it: the reading silently becomes a measurement of the
/// beacon instead of the noise. Spreading samples across more than one beacon
/// interval keeps the contaminated ones a minority, which is the condition a
/// median needs to work at all.
///
/// Radio interrupts are serviced between samples, so a long window does not
/// cost received beacons. Pass SURVEY_NOISE_FAST_MS only where the channel is
/// known quiet — the beacon unit refreshing its figure between its own
/// transmissions.
float measureNoiseFloor(uint32_t window_ms = SURVEY_NOISE_FAST_MS);

/// A sampling window wide enough to straddle one whole beacon interval.
uint32_t noiseWindowMs();

/// Measure with the slow window AND store the result, so the figure on screen
/// is the one that reaches the CSV and the outgoing beacon. What `noise` runs.
float remeasureNoiseFloor();

/// Capture the current measurement under a label. Returns false if full.
bool     mark(const char *label);
uint8_t  markCount();
const SurveyMark &markAt(uint8_t i);
void     clearMarks();

/// Minimum buffer for csvHeader()/csvLine(). The header is the long one — it
/// is now 212 bytes and grew past a 200-byte caller buffer the moment three
/// fade columns were added, losing "recommended_sf" off the end and leaving a
/// header that still looked plausible. A static_assert in survey.cpp now ties
/// this constant to the actual literal, so adding a column fails the build
/// instead of quietly corrupting every CSV a survey produces.
#define SURVEY_CSV_MIN_BUF 320

/// One CSV header line, and one line per mark. `buf` must be >= SURVEY_CSV_MIN_BUF.
void csvHeader(char *buf, size_t len);
void csvLine(const SurveyMark &m, char *buf, size_t len);

}  // namespace survey
