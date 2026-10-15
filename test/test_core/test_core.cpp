/**
 * @file test_core.cpp
 * @brief Native unit tests for the Arduino-free core: AES/CMAC, TLP framing,
 *        and the link maths. Run with `pio test -e native` — no hardware.
 *
 * The AES and CMAC vectors are from FIPS-197 and RFC 4493. They exist because a
 * MIC that is subtly wrong fails in exactly the way that is hardest to debug in
 * the field: everything looks fine until two units disagree.
 */

#include <string.h>
#include <unity.h>

#include "aes128.h"
#include "link_math.h"
#include "tlp.h"

static const uint8_t kRfcKey[16] = {0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6,
                                    0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c};

static void assertBytes(const uint8_t *expected, const uint8_t *actual, size_t n) {
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, actual, n);
}

// ---------------------------------------------------------------- AES-128 ---

void test_aes128_fips197_vector(void) {
    const uint8_t key[16] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                             0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};
    const uint8_t pt[16]  = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
                             0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff};
    const uint8_t ct[16]  = {0x69, 0xc4, 0xe0, 0xd8, 0x6a, 0x7b, 0x04, 0x30,
                             0xd8, 0xcd, 0xb7, 0x80, 0x70, 0xb4, 0xc5, 0x5a};
    uint8_t out[16];
    aes128_encrypt_block(key, pt, out);
    assertBytes(ct, out, 16);
}

// ------------------------------------------------------------- AES-CMAC ----

void test_cmac_empty_message(void) {
    const uint8_t expect[16] = {0xbb, 0x1d, 0x69, 0x29, 0xe9, 0x59, 0x37, 0x28,
                                0x7f, 0xa3, 0x7d, 0x12, 0x9b, 0x75, 0x67, 0x46};
    uint8_t mac[16];
    aes128_cmac(kRfcKey, nullptr, 0, mac);
    assertBytes(expect, mac, 16);
}

void test_cmac_one_block(void) {
    const uint8_t msg[16] = {0x6b, 0xc1, 0xbe, 0xe2, 0x2e, 0x40, 0x9f, 0x96,
                             0xe9, 0x3d, 0x7e, 0x11, 0x73, 0x93, 0x17, 0x2a};
    const uint8_t expect[16] = {0x07, 0x0a, 0x16, 0xb4, 0x6b, 0x4d, 0x41, 0x44,
                                0xf7, 0x9b, 0xdd, 0x9d, 0xd0, 0x4a, 0x28, 0x7c};
    uint8_t mac[16];
    aes128_cmac(kRfcKey, msg, sizeof(msg), mac);
    assertBytes(expect, mac, 16);
}

void test_cmac_partial_final_block(void) {
    // RFC 4493 example 3: 40 bytes, so the final block is incomplete and the
    // K2 path is exercised. This is the case a naive implementation gets wrong.
    const uint8_t msg[40] = {
        0x6b, 0xc1, 0xbe, 0xe2, 0x2e, 0x40, 0x9f, 0x96, 0xe9, 0x3d, 0x7e, 0x11, 0x73, 0x93,
        0x17, 0x2a, 0xae, 0x2d, 0x8a, 0x57, 0x1e, 0x03, 0xac, 0x9c, 0x9e, 0xb7, 0x6f, 0xac,
        0x45, 0xaf, 0x8e, 0x51, 0x30, 0xc8, 0x1c, 0x46, 0xa3, 0x5c, 0xe4, 0x11};
    const uint8_t expect[16] = {0xdf, 0xa6, 0x67, 0x47, 0xde, 0x9a, 0xe6, 0x30,
                                0x30, 0xca, 0x32, 0x61, 0x14, 0x97, 0xc8, 0x27};
    uint8_t mac[16];
    aes128_cmac(kRfcKey, msg, sizeof(msg), mac);
    assertBytes(expect, mac, 16);
}

void test_mic4_is_cmac_prefix(void) {
    const uint8_t msg[10] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    uint8_t full[16], mic[4];
    aes128_cmac(kRfcKey, msg, sizeof(msg), full);
    aes128_cmac_mic4(kRfcKey, msg, sizeof(msg), mic);
    assertBytes(full, mic, 4);
    TEST_ASSERT_TRUE(aes128_mic4_equal(mic, full));
    uint8_t other[4] = {full[0], full[1], full[2], static_cast<uint8_t>(full[3] ^ 0x01)};
    TEST_ASSERT_FALSE(aes128_mic4_equal(mic, other));
}

// ----------------------------------------------------------- TLP framing ---

static TlpBeacon sampleBeacon(void) {
    TlpBeacon b{};
    b.seq          = 0x1234;
    b.tx_dbm       = 22;
    b.ant_gain_qdb = tlp_gain_to_qdb(2.25f);
    b.noise_dbm    = -122;
    b.phy          = tlp_phy(10, TLP_BW_125);
    b.mins_left    = 200;
    return b;
}

void test_beacon_round_trip(void) {
    const TlpBeacon in = sampleBeacon();
    uint8_t buf[TLP_BEACON_LEN];
    TEST_ASSERT_EQUAL_UINT32(TLP_BEACON_LEN, tlp_encode_beacon(in, kRfcKey, buf));

    TlpBeacon out{};
    TEST_ASSERT_TRUE(tlp_decode_beacon(buf, sizeof(buf), kRfcKey, &out));
    TEST_ASSERT_EQUAL_UINT16(in.seq, out.seq);
    TEST_ASSERT_EQUAL_INT8(in.tx_dbm, out.tx_dbm);
    TEST_ASSERT_EQUAL_UINT8(in.ant_gain_qdb, out.ant_gain_qdb);
    TEST_ASSERT_EQUAL_INT8(in.noise_dbm, out.noise_dbm);
    TEST_ASSERT_EQUAL_UINT8(10, tlp_phy_sf(out.phy));
    TEST_ASSERT_EQUAL_UINT8(TLP_BW_125, tlp_phy_bw(out.phy));
    TEST_ASSERT_EQUAL_FLOAT(2.25f, tlp_gain_from_qdb(out.ant_gain_qdb));
}

void test_beacon_rejects_bit_flip(void) {
    uint8_t buf[TLP_BEACON_LEN];
    tlp_encode_beacon(sampleBeacon(), kRfcKey, buf);
    TlpBeacon out{};

    for (size_t i = 0; i < TLP_BEACON_LEN; i++) {
        uint8_t corrupt[TLP_BEACON_LEN];
        memcpy(corrupt, buf, sizeof(corrupt));
        corrupt[i] ^= 0x01;
        TEST_ASSERT_FALSE(tlp_decode_beacon(corrupt, sizeof(corrupt), kRfcKey, &out));
    }
}

void test_beacon_rejects_wrong_key_and_length(void) {
    uint8_t buf[TLP_BEACON_LEN];
    tlp_encode_beacon(sampleBeacon(), kRfcKey, buf);
    TlpBeacon out{};

    uint8_t wrongKey[16];
    memcpy(wrongKey, kRfcKey, 16);
    wrongKey[0] ^= 0xFF;
    TEST_ASSERT_FALSE(tlp_decode_beacon(buf, sizeof(buf), wrongKey, &out));
    TEST_ASSERT_FALSE(tlp_decode_beacon(buf, TLP_BEACON_LEN - 1, kRfcKey, &out));
    TEST_ASSERT_FALSE(tlp_decode_beacon(nullptr, TLP_BEACON_LEN, kRfcKey, &out));
}

void test_seq_delta_wraps(void) {
    TEST_ASSERT_EQUAL_UINT16(1, tlp_seq_delta(5, 6));
    TEST_ASSERT_EQUAL_UINT16(0, tlp_seq_delta(5, 5));
    TEST_ASSERT_EQUAL_UINT16(3, tlp_seq_delta(65534, 1));   // wraps forward
    TEST_ASSERT_EQUAL_UINT16(0, tlp_seq_delta(6, 5));       // backwards is not a gap
}

// ------------------------------------------------------------ link maths ---

void test_snr_limits(void) {
    TEST_ASSERT_EQUAL_FLOAT(-7.5f, link_snr_limit_db(7));
    TEST_ASSERT_EQUAL_FLOAT(-20.0f, link_snr_limit_db(12));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, link_snr_limit_db(6));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, link_snr_limit_db(13));
}

void test_reciprocity_when_ends_are_identical(void) {
    // Both ends at the same power and antenna: predicted RSSI at the far end
    // must equal the RSSI we measured. This is the property that makes a
    // one-way beacon a two-way measurement (spec §7.9).
    LinkInputs in{};
    in.rssi_dbm      = -113.0f;
    in.snr_db        = 2.5f;
    in.far_tx_dbm    = 22.0f;
    in.far_ant_dbi   = 2.0f;
    in.far_noise_dbm = -122.0f;
    in.our_tx_dbm    = 22.0f;
    in.our_ant_dbi   = 2.0f;

    const LinkEstimate e = link_estimate(in, 15.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, -113.0f, e.predicted_rssi_dbm);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 139.0f, e.path_loss_db);   // 22 + 2 + 2 + 113
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 9.0f, e.predicted_snr_db); // -113 - (-122)
}

void test_margin_and_recommendation(void) {
    LinkInputs in{};
    in.rssi_dbm      = -113.0f;
    in.far_tx_dbm    = 22.0f;
    in.far_ant_dbi   = 2.0f;
    in.far_noise_dbm = -122.0f;
    in.our_tx_dbm    = 22.0f;
    in.our_ant_dbi   = 2.0f;

    const LinkEstimate e = link_estimate(in, 15.0f);
    // predicted SNR +9 dB. SF7 needs -7.5 => margin 16.5, which clears 15.
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 16.5f, e.margin_db[0]);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 29.0f, e.margin_db[5]);   // SF12
    TEST_ASSERT_TRUE(e.any_sf_usable);
    TEST_ASSERT_EQUAL_UINT8(7, e.recommended_sf);             // fastest that clears
}

void test_no_usable_sf_is_reported(void) {
    LinkInputs in{};
    in.rssi_dbm      = -140.0f;
    in.far_tx_dbm    = 22.0f;
    in.far_ant_dbi   = 2.0f;
    in.far_noise_dbm = -122.0f;
    in.our_tx_dbm    = 22.0f;
    in.our_ant_dbi   = 2.0f;

    const LinkEstimate e = link_estimate(in, 15.0f);
    TEST_ASSERT_FALSE(e.any_sf_usable);
}

void test_per_window(void) {
    PerWindow p;
    TEST_ASSERT_EQUAL_INT(-1, p.deliveryPercent());   // nothing said yet

    p.onBeacon(100);
    TEST_ASSERT_EQUAL_INT(-1, p.deliveryPercent());   // still settling

    p.onBeacon(101);
    p.onBeacon(102);
    TEST_ASSERT_EQUAL_UINT32(3, p.heard());
    TEST_ASSERT_EQUAL_UINT32(3, p.expected());
    TEST_ASSERT_EQUAL_INT(100, p.deliveryPercent());

    p.onBeacon(112);                                  // ten sent, one heard
    TEST_ASSERT_EQUAL_UINT32(4, p.heard());
    TEST_ASSERT_EQUAL_UINT32(13, p.expected());
    TEST_ASSERT_EQUAL_INT(30, p.deliveryPercent());

    p.reset();
    TEST_ASSERT_EQUAL_INT(-1, p.deliveryPercent());
}

/// The walk between trap sites, and why survey::setRole() must reset.
///
/// A listener hears trap 7 perfectly, then is carried 250 m to trap 8. The
/// beacon keeps transmitting the whole way — about 300 beacons at a 2 s
/// interval over ten minutes — and the listener hears none of them. When it
/// arrives and hears the next one, the sequence gap is booked as loss.
///
/// This is correct behaviour for one continuous measurement, and completely
/// wrong across two sites. Without a reset, trap 8 opens its account 300
/// packets in the red and can never climb out: delivery rate, the headline
/// number of the whole survey, becomes a fiction at every site after the
/// first. The fix is in setRole(), not here — this test pins the mechanism so
/// nobody later "simplifies" that reset away.
void test_site_change_without_reset_is_ruinous(void) {
    PerWindow p;
    for (uint16_t s = 100; s < 150; s++) p.onBeacon(s);   // a clean site
    TEST_ASSERT_EQUAL_INT(100, p.deliveryPercent());

    p.onBeacon(450);                                      // arrive after a walk
    TEST_ASSERT_EQUAL_UINT32(51, p.heard());
    TEST_ASSERT_EQUAL_UINT32(351, p.expected());          // 50 + the 301-wide gap
    TEST_ASSERT_EQUAL_INT(14, p.deliveryPercent());       // a perfect site, "14 %"

    // What setRole() now does on arrival.
    p.reset();
    for (uint16_t s = 450; s < 500; s++) p.onBeacon(s);
    TEST_ASSERT_EQUAL_INT(100, p.deliveryPercent());      // the truth
}

/// A beacon unit power-cycled at the far end restarts its sequence at 0. That
/// must not read as a 65000-packet loss, or a battery swap mid-survey would
/// condemn a good site.
void test_beacon_restart_is_not_counted_as_loss(void) {
    PerWindow p;
    for (uint16_t s = 900; s < 950; s++) p.onBeacon(s);
    TEST_ASSERT_EQUAL_INT(100, p.deliveryPercent());

    p.onBeacon(0);                                        // far end rebooted
    TEST_ASSERT_EQUAL_UINT32(51, p.heard());
    TEST_ASSERT_EQUAL_UINT32(51, p.expected());
    TEST_ASSERT_EQUAL_INT(100, p.deliveryPercent());
}

// ------------------------------------------------------------------ main ---

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_aes128_fips197_vector);
    RUN_TEST(test_cmac_empty_message);
    RUN_TEST(test_cmac_one_block);
    RUN_TEST(test_cmac_partial_final_block);
    RUN_TEST(test_mic4_is_cmac_prefix);
    RUN_TEST(test_beacon_round_trip);
    RUN_TEST(test_beacon_rejects_bit_flip);
    RUN_TEST(test_beacon_rejects_wrong_key_and_length);
    RUN_TEST(test_seq_delta_wraps);
    RUN_TEST(test_snr_limits);
    RUN_TEST(test_reciprocity_when_ends_are_identical);
    RUN_TEST(test_margin_and_recommendation);
    RUN_TEST(test_no_usable_sf_is_reported);
    RUN_TEST(test_per_window);
    RUN_TEST(test_site_change_without_reset_is_ruinous);
    RUN_TEST(test_beacon_restart_is_not_counted_as_loss);
    return UNITY_END();
}

void setUp(void) {}
void tearDown(void) {}
