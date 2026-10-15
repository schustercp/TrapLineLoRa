/**
 * @file link_math.h
 * @brief Turning a received beacon into a go/no-go answer — spec §7.9.
 *
 * Arduino-free so it unit-tests natively.
 *
 * The chain of reasoning, all of it reversible because path loss is reciprocal:
 *
 *   path_loss   = tx_dbm + tx_ant_gain + rx_ant_gain - rssi_measured
 *   pred_rssi   = our_tx_dbm + our_ant_gain + far_ant_gain - path_loss
 *   pred_snr    = pred_rssi - far_noise_floor
 *   margin(SF)  = pred_snr - snr_required(SF)
 *
 * The asymmetry that does NOT cancel is noise: a hut full of chargers and WiFi
 * can be 5-15 dB noisier than a quiet bush site, which is why the beacon
 * carries the transmitter's own measured noise floor and margins are computed
 * against that rather than a datasheet sensitivity figure.
 */

#pragma once

#include <stdint.h>

#define LINK_SF_MIN 7
#define LINK_SF_MAX 12
#define LINK_SF_COUNT (LINK_SF_MAX - LINK_SF_MIN + 1)

/// SX1262 LoRa demodulator SNR limit for a spreading factor, in dB.
/// Returns 0 for an out-of-range SF.
float link_snr_limit_db(uint8_t sf);

/// Sensitivity in dBm at BW125 for a spreading factor. Returns 0 out of range.
float link_sensitivity_dbm(uint8_t sf);

/// Inputs to one link estimate. Gains are dBi, powers dBm.
struct LinkInputs {
    float rssi_dbm;      ///< RSSI we measured on the received beacon
    float snr_db;        ///< SNR we measured on the received beacon
    float far_tx_dbm;    ///< transmitter's conducted power (from the beacon)
    float far_ant_dbi;   ///< transmitter's antenna gain (from the beacon)
    float far_noise_dbm; ///< transmitter's noise floor (from the beacon)
    float our_tx_dbm;    ///< our conducted power
    float our_ant_dbi;   ///< our antenna gain
};

struct LinkEstimate {
    float   path_loss_db;        ///< isotropic path loss between the two ends
    float   predicted_rssi_dbm;  ///< what the far end will see when we transmit
    float   predicted_snr_db;    ///< ... relative to the far end's noise floor
    float   margin_db[LINK_SF_COUNT];  ///< index 0 = SF7 ... 5 = SF12
    uint8_t recommended_sf;      ///< fastest SF meeting `required_margin_db`, else LINK_SF_MAX
    bool    any_sf_usable;       ///< false when even SF12 falls short
};

/// Compute the estimate. `required_margin_db` is the fade margin you insist on
/// (the spec uses 15 dB); it only affects `recommended_sf` and `any_sf_usable`.
LinkEstimate link_estimate(const LinkInputs &in, float required_margin_db);

/// Packet error rate helper: a rolling window of expected-versus-heard beacons.
class PerWindow {
  public:
    void reset();

    /// Feed each successfully decoded beacon sequence number in arrival order.
    void onBeacon(uint16_t seq);

    uint32_t heard() const { return heard_; }
    uint32_t expected() const { return expected_; }

    /// Delivery rate 0..100. Returns -1 before the first two beacons, when
    /// there is not yet enough information to say anything honest.
    int deliveryPercent() const;

    bool started() const { return started_; }

  private:
    bool     started_ = false;
    uint16_t lastSeq_ = 0;
    uint32_t heard_ = 0;
    uint32_t expected_ = 0;
};
