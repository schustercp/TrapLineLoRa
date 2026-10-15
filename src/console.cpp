/**
 * @file console.cpp
 * @brief BLE + serial console and the survey command set. See console.h.
 */

#include "console.h"

#include <bluefruit.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "board_pins.h"
#include "device_id.h"
#include "radio_config.h"
#include "survey.h"

namespace {

BLEUart g_bleuart;
BLEDis  g_bledis;

bool   g_connected = false;
char   g_line[96];
size_t g_lineLen = 0;
uint32_t g_lineIdleAt = 0;
char   g_serialLine[96];
size_t g_serialLineLen = 0;
uint32_t g_serialIdleAt = 0;
volatile bool g_greetPending = false;

/// Some BLE terminal apps send exactly what you typed with no line ending —
/// iOS Bluefruit Connect has an EOL toggle that people leave off. Rather than
/// making that a support question, treat a gap in the character stream as the
/// end of a line. A typed command arrives as one BLE write, so this is safe.
#define CONSOLE_IDLE_FLUSH_MS 150

void onConnect(uint16_t h) {
    (void)h;
    g_connected = true;
    g_greetPending = true;   // printed from poll(), not from the BLE callback
}
void onDisconnect(uint16_t h, uint8_t r) { (void)h; (void)r; g_connected = false; }

/// Format a float as "-123.4" without %f — newlib-nano on this core omits
/// floating point from printf, so every number here is built from integers.
///
/// The fraction is emitted with %c rather than %ld: one character, so the
/// compiler can prove it fits. The earlier %ld version drew a
/// -Wformat-truncation warning, which was fair — nothing bounded the value.
/// Clamping makes the function total for NaN and nonsense inputs too.
const char *f1(float v, char *out, size_t n) {
    if (!(v > -99999.0f && v < 99999.0f)) {   // also catches NaN
        snprintf(out, n, "n/a");
        return out;
    }
    long t = lroundf(v * 10.0f);
    // Clamp the long as well as the float. The compiler does not model
    // lroundf's range, so without this it must assume a full-width long and
    // warns that the buffer may be too small.
    if (t > 999999L) t = 999999L;
    if (t < -999999L) t = -999999L;
    const char *sign = (t < 0) ? "-" : "";
    const long mag = (t < 0) ? -t : t;
    snprintf(out, n, "%s%ld.%c", sign, mag / 10, static_cast<char>('0' + (mag % 10)));
    return out;
}

const char *argOf(const char *line) {
    const char *sp = strchr(line, ' ');
    return sp ? sp + 1 : nullptr;
}

bool cmdIs(const char *line, const char *name) {
    const size_t n = strlen(name);
    if (strncmp(line, name, n) != 0) return false;
    return line[n] == '\0' || line[n] == ' ';
}

void printHelp() {
    console::log("commands:");
    console::log("range walk:");
    console::log("  park                leave this one at the site (beacon, full power)");
    console::log("  walk                carry this one (listen + live readout)");
    console::log("  watch <s>|off       live one-line readout every <s> seconds");
    console::log("");
    console::log("  status              current measurement and link estimate");
    console::log("  role beacon|listen|idle");
    console::log("  sf <7..12>          spreading factor");
    console::log("  bw <125|62.5>       bandwidth in kHz");
    console::log("  freq <kHz>          carrier, e.g. 915000");
    console::log("  power <-9..22>      tx power in dBm");
    console::log("  antenna <dBi>       THIS unit's antenna gain - be honest");
    console::log("  interval <ms>       beacon period");
    console::log("  margin <dB>         fade margin demanded, default 15");
    console::log("  noise               re-measure the noise floor now");
    console::log("  reset               clear counters");
    console::log("  mark <label>        record this measurement");
    console::log("  log                 dump marks as CSV");
    console::log("  clear               erase marks");
    console::log("  id                  device id");
    console::log("  ver                 firmware build stamp");
}

/// True when the weakest frames are close enough to the demodulation floor
/// that deeper fades were dropped rather than measured. See
/// SURVEY_CENSOR_MARGIN_DB — this flatters the worse site, so it must be said.
bool fadeStatsCensored(const SurveyStats &s, const SurveyConfig &c) {
    if (s.rssiCount == 0) return false;
    return s.rssiMin <= (link_sensitivity_dbm(c.sf) + SURVEY_CENSOR_MARGIN_DB);
}

uint32_t g_watchMs = 0;        ///< 0 = off
uint32_t g_nextWatchAt = 0;

/// One-glance verdict for the range walk.
///
/// Two numbers decide it, and either alone is misleading: 100 % delivery at
/// +2 dB margin is one wet week away from failing, and 40 % delivery at +20 dB
/// margin is already broken. So both must hold.
const char *verdict(int pct, float marginDb, bool usable) {
    if (pct < 0) return "settling";
    if (!usable) return "FAILING";
    if (pct >= SURVEY_VERDICT_GOOD_PCT && marginDb >= SURVEY_REQUIRED_MARGIN_DB) return "GOOD";
    if (pct >= SURVEY_VERDICT_MARGINAL_PCT && marginDb >= SURVEY_VERDICT_MARGINAL_DB) return "MARGINAL";
    return "FAILING";
}

/// The walk display: short enough to read on a phone in one glance, and
/// self-printing so nobody types commands while holding a radio in a hedge.
void printWatchLine() {
    const SurveyStats &s = survey::stats();
    const PerWindow   &p = survey::per();
    const SurveyConfig &c = survey::config();

    if (!s.haveBeacon) {
        console::log("· · ·  no beacon yet");
        return;
    }

    const uint32_t ago = (millis() - s.lastHeardMs) / 1000UL;
    if (ago >= 10) {
        // Do not show stale numbers as if they were live — out of range looks
        // exactly like a healthy link if the last good frame stays on screen.
        console::logf("· · ·  NOTHING HEARD for %lu s  — out of range, or turn back",
                      static_cast<unsigned long>(ago));
        return;
    }

    char r[12], n[12], m[12];
    const int   pct = p.deliveryPercent();
    const LinkEstimate est = survey::windowEstimate();
    const float margin = est.margin_db[c.sf - LINK_SF_MIN];

    console::logf("%lupkt %d%% %s dBm snr %s  SF%u %s dB  %s",
                  static_cast<unsigned long>(p.heard()),
                  (pct >= 0) ? pct : 0,
                  f1(surveyMeanRssi(s), r, sizeof(r)),
                  f1(surveyMeanSnr(s), n, sizeof(n)),
                  c.sf,
                  f1(margin, m, sizeof(m)),
                  verdict(pct, margin, est.any_sf_usable));

    // Only when it matters — a slower SF is the cheapest fix in the field, but
    // it is not free, so do not nag about it while the link is comfortable.
    if (est.any_sf_usable && est.recommended_sf > c.sf) {
        console::logf("        needs SF%u here", est.recommended_sf);
    }
    if (fadeStatsCensored(s, c)) {
        console::log("        (near the decode floor — fades under-counted)");
    }
}

void printStatus() {
    const SurveyConfig &c = survey::config();
    const SurveyStats  &s = survey::stats();
    const PerWindow    &p = survey::per();

    char a[12], b[12], d[12], e[12];

    const char *roleName = "idle";
    if (survey::role() == SurveyRole::Beacon) roleName = "beacon";
    else if (survey::role() == SurveyRole::Listen) roleName = "listen";

    console::logf("role %s  ·  SF%u  BW %s kHz  %d dBm  ant %s dBi", roleName, c.sf,
                  f1(c.bw_khz, a, sizeof(a)), c.tx_dbm, f1(c.ant_dbi, b, sizeof(b)));
    // "0.0 dBm" is not a noise floor, it is the absence of one. Saying so out
    // loud matters: a bench run printed it for three minutes and it read as a
    // measurement rather than as a unit that had never taken one.
    char noiseTxt[20];
    if (s.ourNoiseDbm == SURVEY_NOISE_UNMEASURED_DBM) {
        snprintf(noiseTxt, sizeof(noiseTxt), "not measured");
    } else {
        snprintf(noiseTxt, sizeof(noiseTxt), "%s dBm", f1(s.ourNoiseDbm, d, sizeof(d)));
    }
    console::logf("freq %lu.%03lu MHz  ·  our noise floor %s",
                  static_cast<unsigned long>(c.freq_mhz),
                  static_cast<unsigned long>(lroundf(c.freq_mhz * 1000.0f)) % 1000UL,
                  noiseTxt);

    if (!s.haveBeacon) {
        console::logf("no beacon heard yet  (%lu frames rejected)",
                      static_cast<unsigned long>(s.rejected));
        return;
    }

    const int pct = p.deliveryPercent();
    console::logf("heard %lu/%lu%s  ·  rssi %s dBm  snr %s dB  ·  %lu s ago",
                  static_cast<unsigned long>(p.heard()),
                  static_cast<unsigned long>(p.expected()),
                  (pct >= 0) ? "" : " (settling)",
                  f1(s.lastRssi, a, sizeof(a)), f1(s.lastSnr, b, sizeof(b)),
                  static_cast<unsigned long>((millis() - s.lastHeardMs) / 1000UL));

    // The line above is the last frame. This one is the window, and it is the
    // one `mark` records. A bench run saw -66.0 dBm and -56.0 dBm three seconds
    // apart across a motionless desk; whichever of those a survey happens to
    // catch would otherwise become the permanent record of the site.
    if (s.rssiCount > 0) {
        char m1[12], m2[12], m3[12], m4[12];
        console::logf("window   ·  rssi mean %s  min %s  max %s (fade %s dB, n=%lu)",
                      f1(surveyMeanRssi(s), m1, sizeof(m1)),
                      f1(s.rssiMin, m2, sizeof(m2)),
                      f1(s.rssiMax, m3, sizeof(m3)),
                      f1(s.rssiMax - s.rssiMin, m4, sizeof(m4)),
                      static_cast<unsigned long>(s.rssiCount));

        // Depth alone does not diagnose. A block of consecutive fades is
        // something crossing the path; the same count scattered through the
        // window is an intermittent connection. Say which.
        if (s.deepFades > 0) {
            console::logf("         ·  %lu frame(s) >=%d dB down, longest run %lu, worst at t+%lu s",
                          static_cast<unsigned long>(s.deepFades),
                          static_cast<int>(SURVEY_DEEP_FADE_DB),
                          static_cast<unsigned long>(s.fadeRunMax),
                          static_cast<unsigned long>(surveyFadeAtS(s)));
            if (s.fadeRunMax >= 3) {
                console::log("         ·  consecutive — something crossed the path");
                console::log("            (a person, a door, a branch). Stand clear and repeat.");
            } else {
                console::log("         ·  scattered — suspect an intermittent antenna");
                console::log("            connection before blaming the channel.");
            }
        }
        if (fadeStatsCensored(s, c)) {
            char sv[12];
            console::logf("         ·  CENSORED: weakest frame %s dBm is within %d dB of the",
                          f1(s.rssiMin, sv, sizeof(sv)), static_cast<int>(SURVEY_CENSOR_MARGIN_DB));
            console::logf("            SF%u decode floor. Deeper fades did not arrive, so min", c.sf);
            console::log("            and the fade count are LOWER BOUNDS. Trust delivery %.");
        }
    }
    if (pct >= 0) console::logf("delivery %d %%", pct);

    // On a desk at full power the two units are ~-6 dBm apart, which saturates
    // the receiver: AGC clips, RSSI stops tracking, and every derived number
    // below is fiction. Say so rather than letting someone trust it.
    if (s.lastRssi > -30.0f) {
        console::log("!! rssi above -30 dBm: the receiver is saturated and the");
        console::log("!! path loss and margins below are NOT trustworthy.");
        console::log("!! On the bench use `power -9` and a few metres of separation.");
    }

    console::logf("path loss %s dB  ·  far noise floor %s dBm",
                  f1(s.est.path_loss_db, a, sizeof(a)),
                  f1(static_cast<float>(s.last.noise_dbm), b, sizeof(b)));
    console::logf("predicted at far end: rssi %s dBm  snr %s dB",
                  f1(s.est.predicted_rssi_dbm, d, sizeof(d)),
                  f1(s.est.predicted_snr_db, e, sizeof(e)));

    for (int i = 0; i < LINK_SF_COUNT; i++) {
        const uint8_t sf = static_cast<uint8_t>(LINK_SF_MIN + i);
        const float m = s.est.margin_db[i];
        const char *verdict = (m >= survey::config().required_margin_db) ? "OK"
                              : (m >= 6.0f) ? "THIN"
                              : (m >= 0.0f) ? "MARGINAL" : "FAILS";
        console::logf("  SF%-2u margin %6s dB   %s", sf, f1(m, a, sizeof(a)), verdict);
    }

    if (s.est.any_sf_usable) {
        console::logf("recommend SF%u   ·   set with: sf %u", s.est.recommended_sf,
                      s.est.recommended_sf);
    } else {
        console::log("NO usable spreading factor at this margin — raise the antenna,");
        console::log("shorten the hop, or accept less margin with `margin <dB>`.");
    }
}

void dumpCsv() {
    char buf[SURVEY_CSV_MIN_BUF];
    survey::csvHeader(buf, sizeof(buf));
    console::log(buf);
    for (uint8_t i = 0; i < survey::markCount(); i++) {
        survey::csvLine(survey::markAt(i), buf, sizeof(buf));
        console::log(buf);
    }
    console::logf("(%u marks)", survey::markCount());
}

void handleCommand(char *line) {
    size_t len = strlen(line);
    while (len > 0 && (line[len - 1] == ' ' || line[len - 1] == '\t')) line[--len] = '\0';
    if (len == 0) return;

    const char *arg = argOf(line);
    SurveyConfig &c = survey::config();

    if (cmdIs(line, "help") || line[0] == '?') {
        printHelp();
    } else if (cmdIs(line, "status")) {
        printStatus();
    } else if (cmdIs(line, "id")) {
        char hex[20];
        deviceIdHex(hex, sizeof(hex));
        console::logf("device id %s", hex);
    } else if (cmdIs(line, "role") && arg) {
        if (strncmp(arg, "beacon", 6) == 0)      survey::setRole(SurveyRole::Beacon);
        else if (strncmp(arg, "listen", 6) == 0) survey::setRole(SurveyRole::Listen);
        else                                     survey::setRole(SurveyRole::Idle);
        console::logf("ok — role is now %s", arg);
    } else if (cmdIs(line, "sf") && arg) {
        const int sf = atoi(arg);
        if (sf < LINK_SF_MIN || sf > LINK_SF_MAX) { console::log("error — SF must be 7..12"); return; }
        c.sf = static_cast<uint8_t>(sf);
        console::log(survey::applyRadio() == RADIOLIB_ERR_NONE ? "ok" : "error — radio rejected it");
    } else if (cmdIs(line, "bw") && arg) {
        c.bw_khz = static_cast<float>(atof(arg));
        console::log(survey::applyRadio() == RADIOLIB_ERR_NONE ? "ok" : "error — radio rejected it");
    } else if (cmdIs(line, "freq") && arg) {
        c.freq_mhz = static_cast<float>(atol(arg)) / 1000.0f;   // kHz in, no float parsing
        console::log(survey::applyRadio() == RADIOLIB_ERR_NONE ? "ok" : "error — radio rejected it");
    } else if (cmdIs(line, "power") && arg) {
        c.tx_dbm = static_cast<int8_t>(atoi(arg));
        console::log(survey::applyRadio() == RADIOLIB_ERR_NONE ? "ok" : "error — radio rejected it");
    } else if (cmdIs(line, "antenna") && arg) {
        c.ant_dbi = static_cast<float>(atof(arg));
        char a[12];
        console::logf("ok — antenna %s dBi", f1(c.ant_dbi, a, sizeof(a)));
    } else if (cmdIs(line, "interval") && arg) {
        c.beacon_interval_ms = static_cast<uint32_t>(atol(arg));
        console::log("ok");
    } else if (cmdIs(line, "margin") && arg) {
        c.required_margin_db = static_cast<float>(atof(arg));
        console::log("ok");
    } else if (cmdIs(line, "park")) {
        // The unit that stays at the trap site. Full power, fast beacon.
        c.tx_dbm = LORA_TX_POWER_DBM;
        c.beacon_interval_ms = SURVEY_WALK_INTERVAL_MS;
        survey::applyRadio();
        survey::setRole(SurveyRole::Beacon);
        console::log("ok — PARK. Beaconing at full power, 1 s interval.");
        console::log("Put it where a trap would sit, antenna vertical, and walk away.");
        console::log("Red LED flashes every 4 s. Nothing else to do here.");
    } else if (cmdIs(line, "walk")) {
        // The unit you carry.
        c.tx_dbm = LORA_TX_POWER_DBM;
        survey::applyRadio();
        survey::setRole(SurveyRole::Listen);
        g_watchMs = SURVEY_WATCH_DEFAULT_S * 1000UL;
        g_nextWatchAt = millis() + 1000UL;
        console::log("ok — WALK. Listening, live readout every 5 s.");
        console::log("At each stop: put it ON THE GROUND, step back, wait 30 s, read.");
        console::log("Then `mark <distance>`. `watch off` to stop the readout.");
    } else if (cmdIs(line, "watch")) {
        if (arg && strncmp(arg, "off", 3) == 0) {
            g_watchMs = 0;
            console::log("ok — watch off");
        } else {
            long secs = arg ? atol(arg) : SURVEY_WATCH_DEFAULT_S;
            if (secs < 1) secs = 1;
            if (secs > 60) secs = 60;
            g_watchMs = static_cast<uint32_t>(secs) * 1000UL;
            g_nextWatchAt = millis();
            console::logf("ok — watch every %ld s. `watch off` to stop.", secs);
        }
    } else if (cmdIs(line, "ver")) {
        console::logf("build %s %s", __DATE__, __TIME__);
    } else if (cmdIs(line, "noise")) {
        // Store it, do not just print it. This previously reported a figure
        // that went nowhere: the CSV and the outgoing beacon both kept the old
        // value, so the number on screen was not the number in the data.
        char a[12];
        const float n = survey::remeasureNoiseFloor();
        console::logf("noise floor %s dBm  (%lu ms window)", f1(n, a, sizeof(a)),
                      static_cast<unsigned long>(survey::noiseWindowMs()));
        if (n == SURVEY_NOISE_UNMEASURED_DBM) {
            console::log("  not measured — the radio was not receiving.");
        } else if (n > -95.0f) {
            console::log("  high — local interference. Every margin is computed against this.");
        } else if (n <= SURVEY_NOISE_INSTRUMENT_FLOOR_DBM && n > SURVEY_NOISE_RAILED_DBM) {
            console::log("  at the instrument floor — quiet site, true noise may be lower.");
        }
    } else if (cmdIs(line, "reset")) {
        survey::resetStats();
        console::log("ok — counters cleared");
    } else if (cmdIs(line, "mark")) {
        console::log(survey::mark(arg) ? "ok — marked" : "error — no measurement yet, or store full");
    } else if (cmdIs(line, "log")) {
        dumpCsv();
    } else if (cmdIs(line, "clear")) {
        survey::clearMarks();
        console::log("ok — marks erased");
    } else {
        console::log("unknown command — try `help`");
    }
}

void feed(int ch, char *buf, size_t &fill, size_t cap, uint32_t &idleAt) {
    if (ch == '\r' || ch == '\n') {
        if (fill > 0) {
            buf[fill] = '\0';
            handleCommand(buf);
            fill = 0;
        }
    } else if (fill < cap - 1) {
        buf[fill++] = static_cast<char>(ch);
        idleAt = millis() + CONSOLE_IDLE_FLUSH_MS;
    }
}

/// Run a line that never got its terminator. See CONSOLE_IDLE_FLUSH_MS.
void flushIfIdle(char *buf, size_t &fill, uint32_t &idleAt) {
    if (fill == 0) return;
    if (static_cast<int32_t>(millis() - idleAt) < 0) return;
    buf[fill] = '\0';
    handleCommand(buf);
    fill = 0;
}

}  // namespace

namespace console {

void begin(const char *name) {
    Serial.begin(115200);
    const uint32_t until = millis() + 1500;
    while (!Serial && millis() < until) delay(10);

    Bluefruit.configPrphBandwidth(BANDWIDTH_MAX);   // must precede begin()
    Bluefruit.begin();
    Bluefruit.autoConnLed(false);
    Bluefruit.setTxPower(4);
    Bluefruit.setName(name);
    Bluefruit.Periph.setConnectCallback(onConnect);
    Bluefruit.Periph.setDisconnectCallback(onDisconnect);

    g_bledis.setManufacturer("Seeed Studio");
    g_bledis.setModel("XIAO nRF52840 + Wio-SX1262");
    g_bledis.begin();
    g_bleuart.begin();

    Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
    Bluefruit.Advertising.addTxPower();
    Bluefruit.Advertising.addService(g_bleuart);
    Bluefruit.ScanResponse.addName();
    Bluefruit.Advertising.restartOnDisconnect(true);
    Bluefruit.Advertising.setInterval(32, 244);
    Bluefruit.Advertising.setFastTimeout(30);
    Bluefruit.Advertising.start(0);
}

void setName(const char *name) { Bluefruit.setName(name); }

void poll() {
    // Greet on connect so the first thing a phone shows is not a blank screen.
    if (g_greetPending) {
        g_greetPending = false;
        delay(250);                     // let the link settle before writing
        log("");
        log("TrapLineLoRa survey tool — connected.");
        log("type `help` for commands, `status` for the current measurement.");
    }

    // The boot banner is printed once, and `log()` only writes when the CDC
    // port is actually open — so a monitor attached after boot would otherwise
    // see a blank screen forever on an idle unit. Re-announce on the rising
    // edge instead.
    static bool serialWasUp = false;
    const bool serialUp = static_cast<bool>(Serial);
    if (serialUp && !serialWasUp) {
        delay(50);
        Serial.println("");
        Serial.println("--- serial attached · TrapLineLoRa survey tool");
        // The build stamp goes here deliberately. A desk run was analysed for
        // twenty minutes before it emerged that one of the two units was
        // carrying a build three fixes old, and nothing on screen said so.
        Serial.print("--- build ");
        Serial.print(__DATE__);
        Serial.print(" ");
        Serial.println(__TIME__);
        Serial.println("--- type `help`, or `status` for the current measurement");
    }
    serialWasUp = serialUp;

    while (g_bleuart.available()) {
        const int c = g_bleuart.read();
        if (c < 0) break;
        feed(c, g_line, g_lineLen, sizeof(g_line), g_lineIdleAt);
    }
    while (Serial.available()) {
        feed(Serial.read(), g_serialLine, g_serialLineLen, sizeof(g_serialLine), g_serialIdleAt);
    }

    flushIfIdle(g_line, g_lineLen, g_lineIdleAt);
    flushIfIdle(g_serialLine, g_serialLineLen, g_serialIdleAt);

    // The walk readout. Printing on a timer rather than on receipt keeps the
    // rate steady whether beacons are arriving every second or not at all —
    // and silence is itself the reading you need when you have gone too far.
    if (g_watchMs != 0 && static_cast<int32_t>(millis() - g_nextWatchAt) >= 0) {
        g_nextWatchAt = millis() + g_watchMs;
        printWatchLine();
    }
}

void log(const char *line) {
    if (line == nullptr) return;
    // `Serial` here is USB CDC, and RadioLib warns at compile time that CDC
    // output "might stop on first sleep". Guarding on the port actually being
    // open costs nothing now and is required behaviour once the node firmware
    // starts sleeping with no host attached.
    if (Serial) Serial.println(line);
    if (g_connected) {
        g_bleuart.write(reinterpret_cast<const uint8_t *>(line), strlen(line));
        g_bleuart.write(reinterpret_cast<const uint8_t *>("\r\n"), 2);
    }
}

void logf(const char *fmt, ...) {
    char buf[192];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    log(buf);
}

bool bleConnected() { return g_connected; }

}  // namespace console
