/**
 * @file tlp.cpp
 * @brief Trap Line Protocol frame encoding. See tlp.h and spec §7.
 */

#include "tlp.h"

#include <string.h>

#include "aes128.h"

size_t tlp_encode_beacon(const TlpBeacon &b, const uint8_t key[16], uint8_t *out) {
    if (out == nullptr) return 0;

    out[0] = static_cast<uint8_t>((TLP_VERSION << 4) | TLP_BEACON);
    out[1] = static_cast<uint8_t>(TLP_BASE_ID & 0xFF);
    out[2] = static_cast<uint8_t>(TLP_BASE_ID >> 8);
    out[3] = static_cast<uint8_t>(b.seq & 0xFF);
    out[4] = static_cast<uint8_t>(b.seq >> 8);
    out[5] = static_cast<uint8_t>(b.tx_dbm);
    out[6] = b.ant_gain_qdb;
    out[7] = static_cast<uint8_t>(b.noise_dbm);
    out[8] = b.phy;
    out[9] = b.mins_left;

    aes128_cmac_mic4(key, out, 10, out + 10);
    return TLP_BEACON_LEN;
}

bool tlp_decode_beacon(const uint8_t *buf, size_t len, const uint8_t key[16], TlpBeacon *out) {
    if (buf == nullptr || out == nullptr) return false;
    if (len != TLP_BEACON_LEN) return false;
    if ((buf[0] >> 4) != TLP_VERSION) return false;
    if ((buf[0] & 0x0F) != TLP_BEACON) return false;

    uint8_t mic[TLP_MIC_LEN];
    aes128_cmac_mic4(key, buf, 10, mic);
    if (!aes128_mic4_equal(mic, buf + 10)) return false;

    out->seq          = static_cast<uint16_t>(buf[3] | (buf[4] << 8));
    out->tx_dbm       = static_cast<int8_t>(buf[5]);
    out->ant_gain_qdb = buf[6];
    out->noise_dbm    = static_cast<int8_t>(buf[7]);
    out->phy          = buf[8];
    out->mins_left    = buf[9];
    return true;
}

uint16_t tlp_seq_delta(uint16_t prev, uint16_t next) {
    const uint16_t d = static_cast<uint16_t>(next - prev);
    // More than half the sequence space means the counter went backwards
    // (a restarted beacon, or a stale frame). Not a gap.
    return (d > 0x8000) ? 0 : d;
}
