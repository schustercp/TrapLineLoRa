/**
 * @file tlp.h
 * @brief Trap Line Protocol frame encoding — spec §7.
 *
 * Arduino-free so it unit-tests natively. Little-endian on the wire; the
 * encoder writes bytes explicitly rather than memcpy-ing structs, so the format
 * does not depend on the compiler's padding or the host's endianness.
 *
 * Only BEACON is implemented so far — it is what survey mode needs (§7.9).
 * The other types are declared so the type byte is settled from the start.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#define TLP_VERSION      1
#define TLP_MIC_LEN      4
#define TLP_BASE_ID      0xFFFF

/// Message types (low nibble of `ver_type`). Spec §7.1, §7.2.
enum TlpType : uint8_t {
    TLP_EVENT     = 0x1,
    TLP_HEARTBEAT = 0x2,
    TLP_ALERT     = 0x3,
    TLP_IDENT     = 0x4,
    TLP_ACK       = 0x8,
    TLP_BEACON    = 0x9,
    TLP_WAKE      = 0xA,
};

/// Bandwidth codes for the `phy` byte.
enum TlpBw : uint8_t {
    TLP_BW_125 = 0,
    TLP_BW_62_5 = 1,
    TLP_BW_250 = 2,
};

#define TLP_BEACON_LEN 14

/// Survey beacon — spec §7.2. Everything a listener needs to characterise the
/// path without being told anything out of band.
struct TlpBeacon {
    uint16_t seq;           ///< increments every beacon; gaps are the packet error rate
    int8_t   tx_dbm;        ///< transmitter conducted power
    uint8_t  ant_gain_qdb;  ///< transmitter antenna gain, quarter-dB, unsigned
    int8_t   noise_dbm;     ///< transmitter's measured noise floor
    uint8_t  phy;           ///< high nibble SF (7..12), low nibble TlpBw
    uint8_t  mins_left;     ///< minutes of survey mode remaining, saturating
};

inline uint8_t tlp_phy(uint8_t sf, TlpBw bw) {
    return static_cast<uint8_t>((sf << 4) | (bw & 0x0F));
}
inline uint8_t tlp_phy_sf(uint8_t phy) { return static_cast<uint8_t>(phy >> 4); }
inline TlpBw   tlp_phy_bw(uint8_t phy) { return static_cast<TlpBw>(phy & 0x0F); }

/// Antenna gain helpers — quarter-dB keeps 0.25 dB resolution in one byte.
inline uint8_t tlp_gain_to_qdb(float dbi) {
    const float q = dbi * 4.0f + 0.5f;
    if (q <= 0.0f) return 0;
    if (q >= 255.0f) return 255;
    return static_cast<uint8_t>(q);
}
inline float tlp_gain_from_qdb(uint8_t qdb) { return static_cast<float>(qdb) / 4.0f; }

/**
 * Encode a beacon into exactly TLP_BEACON_LEN bytes, MIC included.
 * @return TLP_BEACON_LEN, or 0 if `out` is null.
 */
size_t tlp_encode_beacon(const TlpBeacon &b, const uint8_t key[16], uint8_t *out);

/**
 * Decode and authenticate a beacon.
 * @return true only if the length, version, type and MIC all check out.
 */
bool tlp_decode_beacon(const uint8_t *buf, size_t len, const uint8_t key[16], TlpBeacon *out);

/// Distance from `prev` to `next` in a wrapping uint16 sequence space.
/// Returns 0 when they are equal, and treats more than half the space as a
/// backwards jump (reported as 0) rather than a very large gap.
uint16_t tlp_seq_delta(uint16_t prev, uint16_t next);
