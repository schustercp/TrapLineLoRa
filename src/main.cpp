/**
 * @file main.cpp
 * @brief TrapLineLoRa survey tool — Phase 0.
 *
 * Hardware: Seeed Studio XIAO nRF52840 + Wio-SX1262 (kit SKU 102010710).
 *
 * Build two of these. Put one in `role beacon` and leave it at a trap site;
 * carry the other in `role listen` to the next site 250 m along. Connect to the
 * listener with Bluefruit Connect or nRF Connect, watch RSSI, SNR and delivery
 * rate settle, then `mark trap-07-to-08`. Leapfrog down the line and `log` at
 * the end of the day for a CSV.
 *
 * Measure at the height and orientation the deployed node's antenna will use.
 * Holding the unit at head height overstates the link by 10 dB or more and
 * produces a line that passes commissioning and fails in winter (spec §17 Q14).
 *
 * A survey unit draws ~8 mA. Run it from a USB power bank, not from a cell you
 * intend to deploy — four hours of surveying is about fifty days of a node's
 * normal budget.
 */

#include <Arduino.h>
#include <RadioLib.h>
#include <SPI.h>
#include <math.h>
#include <stdio.h>

#include "board_pins.h"
#include "console.h"
#include "device_id.h"
#include "radio_config.h"
#include "survey.h"

static SX1262 radio = new Module(LORA_NSS, LORA_DIO1, LORA_NRST, LORA_BUSY);

static const uint8_t kNetworkKey[16] = TRAPLINE_DEFAULT_KEY;

static void onDio1() { survey::onRadioIrq(); }

static void ledSet(uint32_t pin, bool on) { digitalWrite(pin, on ? LED_ON : LED_OFF); }

static void haltBlinking() {
    while (true) {
        ledSet(LED_RED, true);
        delay(120);
        ledSet(LED_RED, false);
        delay(380);
    }
}

/// Boot signature — the only "I am alive" signal that depends on neither USB
/// nor BLE. An idle unit is otherwise indistinguishable from a dead one, which
/// made the first bring-up session much harder than it needed to be.
///
///   2 red blinks   entered setup()
///   3 blue blinks  radio initialised, about to listen
///   red 120/380 forever   fatal: radio init failed, see serial
static void blinkCode(uint32_t pin, uint8_t times) {
    for (uint8_t i = 0; i < times; i++) {
        ledSet(pin, true);
        delay(60);
        ledSet(pin, false);
        delay(140);
    }
}

void setup() {
    pinMode(LED_RED, OUTPUT);
    pinMode(LED_GREEN, OUTPUT);
    pinMode(LED_BLUE, OUTPUT);
    ledSet(LED_RED, false);
    ledSet(LED_GREEN, false);
    ledSet(LED_BLUE, false);

    // Before USB, before BLE, before anything that can fail quietly.
    blinkCode(LED_RED, 2);

    char shortId[8];
    deviceIdShort(shortId, sizeof(shortId));
    char bleName[24];
    snprintf(bleName, sizeof(bleName), "TrapSurvey-%s", shortId);
    console::begin(bleName);

    console::log("");
    console::log("TrapLineLoRa survey tool — XIAO nRF52840 + Wio-SX1262");
    char idHex[20];
    deviceIdHex(idHex, sizeof(idHex));
    console::logf("device %s  ·  built %s %s", idHex, __DATE__, __TIME__);

    // SPI on the XIAO nRF52840 is already SCK=D8 / MISO=D9 / MOSI=D10, which is
    // how the Wio-SX1262 is wired, so the default instance is correct.
    SPI.begin();

    console::log("[radio] initialising SX1262 ...");
    int16_t st = radio.begin(LORA_FREQ_MHZ, LORA_BANDWIDTH_KHZ, LORA_SPREADING_FACTOR,
                             LORA_CODING_RATE, LORA_SYNC_WORD, LORA_TX_POWER_DBM,
                             LORA_PREAMBLE_LENGTH, LORA_TCXO_VOLTAGE, LORA_USE_REGULATOR_LDO);
    if (st != RADIOLIB_ERR_NONE) {
        console::logf("[radio] begin() failed: %d", st);
        console::log("  -2   chip not found — check the board-to-board seating");
        console::log("  -705 BUSY stuck high — usually the TCXO voltage");
        haltBlinking();
    }

    // The Wio-SX1262 drives its TX path from the SX1262's own DIO2 and its RX
    // path from a GPIO. Configure only one and the module transmits into a
    // terminated port and hears nothing.
    st = radio.setDio2AsRfSwitch(true);
    if (st != RADIOLIB_ERR_NONE) console::logf("[radio] setDio2AsRfSwitch failed: %d", st);
    radio.setRfSwitchPins(LORA_RXEN, LORA_TXEN);

    radio.setCurrentLimit(LORA_CURRENT_LIMIT_MA);
    radio.setCRC(2);
    radio.setDio1Action(onDio1);

    survey::begin(&radio, kNetworkKey);
    survey::applyRadio();
    survey::setRole(SurveyRole::Listen);

    console::logf("[radio] %lu.%03lu MHz  SF%u  BW %u kHz  %d dBm  sync 0x%02X",
                  static_cast<unsigned long>(LORA_FREQ_MHZ),
                  static_cast<unsigned long>(lroundf(LORA_FREQ_MHZ * 1000.0f)) % 1000UL,
                  LORA_SPREADING_FACTOR, static_cast<unsigned>(LORA_BANDWIDTH_KHZ),
                  LORA_TX_POWER_DBM, LORA_SYNC_WORD);
    console::log("ready — listening. Type `help`, or `role beacon` on the other unit.");
    blinkCode(LED_BLUE, 3);   // radio up, entering loop()
}

void loop() {
    console::poll();
    survey::loop();

    // Blue flashes when a beacon lands, so the unit is readable at arm's length
    // without a phone. Blue runs at ~230 uA — dimmer than red but visible.
    static uint32_t lastShown = 0;
    const SurveyStats &s = survey::stats();
    if (s.haveBeacon && s.lastHeardMs != lastShown) {
        lastShown = s.lastHeardMs;
        ledSet(LED_BLUE, true);
        delay(15);
        ledSet(LED_BLUE, false);
    }

    // Faint blue tick every 3 s: proof the loop is running. Without it an idle
    // unit that has heard nothing looks exactly like a dead one — no LED, no
    // serial, no BLE — which is a bad place to start debugging from.
    static uint32_t lastAlive = 0;
    if ((millis() - lastAlive) >= 3000) {
        lastAlive = millis();
        ledSet(LED_BLUE, true);
        delay(8);                 // shorter than the 15 ms beacon flash
        ledSet(LED_BLUE, false);
    }

    // Red heartbeat while beaconing, so you can tell the two units apart at a
    // glance. Deliberately NOT green: the schematic puts green on a 10k
    // resistor against 2.2k for red and blue, so green draws ~50 uA and is
    // effectively invisible in daylight (see board_pins.h).
    static uint32_t lastBeaconBlink = 0;
    if (survey::role() == SurveyRole::Beacon && (millis() - lastBeaconBlink) >= 4000) {
        lastBeaconBlink = millis();
        ledSet(LED_RED, true);
        delay(20);
        ledSet(LED_RED, false);
    }
}
