/**
 * @file link_math.cpp
 * @brief See link_math.h and spec §7.9.
 */

#include "link_math.h"

#include "tlp.h"

namespace {

// SX1262 LoRa demodulator SNR limits, dB, SF7..SF12.
const float kSnrLimit[LINK_SF_COUNT] = {-7.5f, -10.0f, -12.5f, -15.0f, -17.5f, -20.0f};

// Sensitivity at BW125, dBm, SF7..SF12.
const float kSensitivity[LINK_SF_COUNT] = {-124.0f, -127.0f, -129.0f,
                                           -132.0f, -134.5f, -137.0f};

inline bool sfInRange(uint8_t sf) { return sf >= LINK_SF_MIN && sf <= LINK_SF_MAX; }

}  // namespace

float link_snr_limit_db(uint8_t sf) {
    if (!sfInRange(sf)) return 0.0f;
    return kSnrLimit[sf - LINK_SF_MIN];
}

float link_sensitivity_dbm(uint8_t sf) {
    if (!sfInRange(sf)) return 0.0f;
    return kSensitivity[sf - LINK_SF_MIN];
}

LinkEstimate link_estimate(const LinkInputs &in, float required_margin_db) {
    LinkEstimate e{};

    // What the path costs, independent of which end is talking.
    e.path_loss_db = in.far_tx_dbm + in.far_ant_dbi + in.our_ant_dbi - in.rssi_dbm;

    // Reverse it to predict what the far end hears from us. When both ends run
    // the same power and antenna this collapses to predicted == measured, which
    // is the property that makes a one-way beacon a two-way measurement.
    e.predicted_rssi_dbm = in.our_tx_dbm + in.our_ant_dbi + in.far_ant_dbi - e.path_loss_db;
    e.predicted_snr_db   = e.predicted_rssi_dbm - in.far_noise_dbm;

    e.recommended_sf = LINK_SF_MAX;
    e.any_sf_usable  = false;

    bool found = false;
    for (int i = 0; i < LINK_SF_COUNT; i++) {
        const uint8_t sf = static_cast<uint8_t>(LINK_SF_MIN + i);
        e.margin_db[i] = e.predicted_snr_db - link_snr_limit_db(sf);
        if (e.margin_db[i] >= required_margin_db) {
            e.any_sf_usable = true;
            if (!found) {          // SFs are ordered fastest first
                e.recommended_sf = sf;
                found = true;
            }
        }
    }
    return e;
}

void PerWindow::reset() {
    started_  = false;
    lastSeq_  = 0;
    heard_    = 0;
    expected_ = 0;
}

void PerWindow::onBeacon(uint16_t seq) {
    if (!started_) {
        started_  = true;
        lastSeq_  = seq;
        heard_    = 1;
        expected_ = 1;
        return;
    }

    const uint16_t delta = tlp_seq_delta(lastSeq_, seq);
    if (delta == 0) {
        // Duplicate or a beacon that restarted its counter. Count it as heard
        // but do not let it inflate the expected count.
        heard_++;
        expected_++;
        lastSeq_ = seq;
        return;
    }

    expected_ += delta;   // delta - 1 missed, plus this one
    heard_++;
    lastSeq_ = seq;
}

int PerWindow::deliveryPercent() const {
    if (!started_ || expected_ < 2) return -1;
    const uint32_t pct = (heard_ * 100u) / expected_;
    return static_cast<int>(pct > 100u ? 100u : pct);
}
