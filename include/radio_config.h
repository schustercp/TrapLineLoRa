/**
 * @file radio_config.h
 * @brief Default LoRa PHY settings — spec §6.2.
 *
 * REGULATORY NOTE (New Zealand)
 * ----------------------------
 * These defaults sit in the 915-928 MHz band covered by the Radio Spectrum
 * Management "General User Radio Licence for Short Range Devices", which
 * permits 0 dBW (30 dBm) e.i.r.p. with no duty-cycle condition stated. At
 * +22 dBm conducted you may spend up to 8 dBi of antenna gain and stay legal.
 *   https://www.rsm.govt.nz/licensing/spectrum-and-licensing/general-user-licences/
 *
 * NEVER key the transmitter without an antenna connected - the SX1262 PA can be
 * damaged by the reflected power.
 */

#pragma once

// -----------------------------------------------------------------------------
// PHY
// -----------------------------------------------------------------------------

/// Carrier frequency in MHz. 921.5 MHz is the better long-term choice (more
/// margin from the band edge) - see spec §17 Q5.
#define LORA_FREQ_MHZ           915.0f

#define LORA_BANDWIDTH_KHZ      125.0f

/// SF10. Every link in the chain is a 250 m hop, where SF10 gives about 17 dB
/// of margin even in wet bush, and stays fast enough to keep relay energy
/// affordable (spec §11.3).
#define LORA_SPREADING_FACTOR   10

#define LORA_CODING_RATE        5

/// 0x12 is RadioLib's "private network" sync word. 0x34 is reserved for public
/// LoRaWAN - keeping ours private means the line ignores everyone else.
#define LORA_SYNC_WORD          0x12

#define LORA_TX_POWER_DBM       22
#define LORA_PREAMBLE_LENGTH    8

/// The Wio-SX1262 powers its TCXO from the SX1262's DIO3 pin at 1.8 V. Get this
/// wrong and BUSY never releases: RadioLib reports SPI_CMD_TIMEOUT.
#define LORA_TCXO_VOLTAGE       1.8f

#define LORA_CURRENT_LIMIT_MA   140.0f
#define LORA_USE_REGULATOR_LDO  false

// -----------------------------------------------------------------------------
// Survey mode (spec §7.9)
// -----------------------------------------------------------------------------

/// Beacon period. At SF10 a 14-byte beacon is ~330 ms, so 2 s is about 17 %
/// duty - acceptable for a hand-held tool, not for anything left running.
#define SURVEY_BEACON_INTERVAL_MS   2000UL

/// Fade margin a link must show before survey mode calls a spreading factor
/// usable. 15 dB covers rain, wet foliage and a season of growth.
#define SURVEY_REQUIRED_MARGIN_DB   15.0f

/// Antenna gain of this unit, dBi. Set it over BLE to match what is fitted -
/// every margin the tool reports depends on it being honest.
#define SURVEY_DEFAULT_ANT_DBI      2.0f

/// Median of this many instantaneous RSSI reads makes one noise-floor figure.
#define SURVEY_NOISE_SAMPLES        15

/// Fast sampling window, ms. Only safe on a channel known to be quiet - see
/// survey::measureNoiseFloor(). A window shorter than one packet airtime
/// (289 ms at SF10/BW125) can land wholly inside a neighbour's transmission,
/// contaminating every sample at once.
#define SURVEY_NOISE_FAST_MS        30UL

/// Guard added to the beacon interval to make the slow sampling window, ms.
#define SURVEY_NOISE_WINDOW_GUARD_MS 400UL

/// Bounds on the slow sampling window, ms.
#define SURVEY_NOISE_WINDOW_MIN_MS  500UL
#define SURVEY_NOISE_WINDOW_MAX_MS  6000UL

/// Measured on the bench 2026-08-22: with no antenna fitted, the SX1262's
/// instantaneous-RSSI block bottoms out at -113 dBm. Thermal noise in 125 kHz
/// plus a 6 dB noise figure is -117 dBm, so the instrument reads about 4 dB
/// high at a genuinely quiet site and cannot see below this. Margins at quiet
/// sites are therefore pessimistic by up to 4 dB - acceptable, and in the safe
/// direction, but it is why a bush reading of -113 means "quiet", not "exactly
/// -113 dBm".
#define SURVEY_NOISE_INSTRUMENT_FLOOR_DBM (-113.0f)

/// RadioLib returns `rssiRaw / -2.0` from an 8-bit register, so the most
/// negative value it can possibly produce is 255/-2 = -127.5 dBm. That is not
/// a quiet channel, it is a railed register - and the SX1262 rails it whenever
/// GetRssiInst is issued outside receive mode. Anything at or below this is a
/// non-measurement and must never be stored or transmitted.
#define SURVEY_NOISE_RAILED_DBM     (-127.0f)

/// "Never successfully measured". Printed as `not measured`, never as 0.0 dBm.
#define SURVEY_NOISE_UNMEASURED_DBM (0.0f)

/// A frame this far below the window's best is counted as a deep fade. 10 dB
/// is roughly two spreading factors of margin, so a link that spends part of
/// its time this far down is a materially different link from its mean.
#define SURVEY_DEEP_FADE_DB         10.0f

// -----------------------------------------------------------------------------
// Range walk
// -----------------------------------------------------------------------------

/// Silence longer than this starts a NEW measurement window automatically.
///
/// On a range walk you carry the listener out of range between stops, and every
/// beacon sent while you were walking is booked as a loss by `PerWindow`. Ten
/// minutes of walking at a 1 s interval is 600 phantom losses, and the delivery
/// rate at the next stop is then meaningless. Role changes already reset; a
/// walk has no role change, so the silence itself is the signal.
///
/// 30 s is deliberately generous: it is 30 missed beacons at a 1 s interval, far
/// more than any usable link drops in a row, so a genuinely bad site still reads
/// as bad rather than being quietly rescued by a restart.
#define SURVEY_WINDOW_GAP_MS        30000UL

/// How often `watch` prints, by default, in seconds.
#define SURVEY_WATCH_DEFAULT_S      5

/// Verdict thresholds for the walk display. A link needs BOTH a delivery rate
/// and a margin: 100 % delivery at +2 dB margin is one wet week from failing,
/// and 40 % delivery at +20 dB margin is already broken.
#define SURVEY_VERDICT_GOOD_PCT     99
#define SURVEY_VERDICT_MARGINAL_PCT 90
#define SURVEY_VERDICT_MARGINAL_DB  6.0f

/// How close the weakest received frame must get to the demodulation floor
/// before the fade statistics are declared censored.
///
/// This is the one bias in this tool that points the wrong way. `rssi_min` and
/// `deep_fades` can only count frames that DECODED. Once the weakest arrivals
/// are near the cliff, every deeper fade is simply missing from the sample, so
/// a distant site reports *fewer* deep fades and a *shallower* minimum than a
/// close one — and reads as the more stable of the two. A 100 m / 150 m city
/// walk showed exactly that: 24 deep fades at 100 m against 13 at 150 m, with
/// both bottoming out at precisely -126.0 dBm, 6 dB above the SF10 floor.
/// Say so on screen, because the numbers alone flatter the worse site.
#define SURVEY_CENSOR_MARGIN_DB     10.0f

/// Beacon interval for a range walk. Shorter than the default so a stop takes
/// 20 s rather than 40 s — you will be making a lot of stops. At SF10/BW125 a
/// 14-byte frame is 289 ms, so this is a ~29 % duty cycle: fine for a one-off
/// test under NZ's GURL for 915-928 MHz, which sets no duty-cycle condition,
/// but NOT what a deployed node does.
#define SURVEY_WALK_INTERVAL_MS     1000UL

// -----------------------------------------------------------------------------
// Network key
// -----------------------------------------------------------------------------

/// Development key. MUST be replaced before deployment - set it over BLE and
/// never commit the real one. Spec §12.
#define TRAPLINE_DEFAULT_KEY                                                   \
    { 0x54, 0x72, 0x61, 0x70, 0x4C, 0x69, 0x6E, 0x65,                          \
      0x44, 0x45, 0x56, 0x4B, 0x45, 0x59, 0x30, 0x31 }
