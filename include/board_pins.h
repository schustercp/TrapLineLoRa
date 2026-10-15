/**
 * @file board_pins.h
 * @brief Pin map for the Seeed Studio XIAO nRF52840 + Wio-SX1262 kit (SKU 102010710).
 *
 * The Wio-SX1262 mates with the XIAO through the two 2x7 board-to-board (B2B)
 * connectors on the underside of the XIAO, NOT through the castellated header,
 * but the signals still land on the standard XIAO D0..D10 pin numbers.
 *
 * Pin assignments below match the Meshtastic `seeed_xiao_nrf52840_kit` variant,
 * which is the reference implementation for this exact kit.
 *
 * NOTE: the Wio-SX1262 shipped with the *ESP32-S3* kit uses a different
 * pin configuration. Do not copy pin numbers between the two kits.
 */

#pragma once

#include <Arduino.h>
#include <RadioLib.h>

// -----------------------------------------------------------------------------
// SX1262 control lines
// -----------------------------------------------------------------------------
#define LORA_NSS        D4    ///< SPI chip select (active low)
#define LORA_DIO1       D1    ///< IRQ line: TxDone / RxDone / Timeout
#define LORA_NRST       D2    ///< Radio reset (active low)
#define LORA_BUSY       D3    ///< Radio busy handshake

/// RF switch RX enable. TX enable is handled by the SX1262 itself via DIO2,
/// so there is no separate TXEN GPIO on this board.
#define LORA_RXEN       D5
#define LORA_TXEN       RADIOLIB_NC

// -----------------------------------------------------------------------------
// SPI bus (matches the default `SPI` instance of the Seeed_XIAO_nRF52840 variant,
// so no custom SPIClass is required)
// -----------------------------------------------------------------------------
#define LORA_SCK        D8
#define LORA_MISO       D9
#define LORA_MOSI       D10

// -----------------------------------------------------------------------------
// Optional Seeed L76K GNSS module (sold separately, not in kit 102010710)
// -----------------------------------------------------------------------------
#define GNSS_TX_PIN     D6    ///< GNSS TX -> XIAO RX
#define GNSS_RX_PIN     D7    ///< GNSS RX <- XIAO TX
#define GNSS_STANDBY_PIN D0   ///< Shared with the user button - pick one

// -----------------------------------------------------------------------------
// On-board XIAO peripherals
// -----------------------------------------------------------------------------
#define USER_BUTTON_PIN D0    ///< Kit's side button (active low, needs INPUT_PULLUP)

// The RGB LED pins (LED_RED / LED_GREEN / LED_BLUE) and the battery macros
// (PIN_VBAT, VBAT_ENABLE, HICHG) come from the Seeed variant header. They are
// re-checked here so a wrong board selection fails loudly at compile time.
#if !defined(LED_RED) || !defined(LED_GREEN) || !defined(LED_BLUE)
#error "Seeed XIAO nRF52840 variant not selected - check `board` in platformio.ini"
#endif

// -----------------------------------------------------------------------------
// RGB LED polarity — SETTLED FROM THE SCHEMATIC, do not "fix" it
// -----------------------------------------------------------------------------
// From Seeed's own KiCad project for XIAO nRF52840 v1.1
// (docs/datasheets/Seeed-Studio-XIAO-nRF52840_V1.1_SCH & PCB_251222_01),
// part RGB6, a 4-pad common-anode RGB LED:
//
//     RGB6 pad +   -> 3V3
//     RGB6 pad R-  -> R7  2.2k -> P0.26  (LED_RED,   pin 11)
//     RGB6 pad G-  -> R4  10k  -> P0.30  (LED_GREEN, pin 13)
//     RGB6 pad B-  -> R3  2.2k -> P0.06  (LED_BLUE,  pin 12)
//
// Anode on 3V3, cathodes through resistors to GPIO: driving a pin LOW lights
// that channel. The Seeed BSP variant header disagrees —
//   framework-arduinoadafruitnrf52-seeed/variants/*/variant.h
//   #define LED_STATE_ON  1
// — and it is simply wrong, apparently inherited from Adafruit's active-high
// boards. The schematic wins.
#define LED_ON  LOW
#define LED_OFF HIGH

// The three channels are NOT equally bright. Green is on 10k while red and
// blue are on 2.2k, so at 3V3 the currents are roughly:
//
//     red    (3.3 - 1.9) / 2.2k  ~= 640 uA
//     blue   (3.3 - 2.8) / 2.2k  ~= 230 uA
//     green  (3.3 - 2.8) / 10k   ~=  50 uA     <-- barely visible outdoors
//
// (Vf is approximate and varies by part.) Use RED for anything that has to be
// seen in daylight. Green is a dim indicator at best — do not rely on it in
// the field. And note red alone, held on, exceeds the node's entire average
// current budget twice over: flash, never hold.
#define LED_R_ISET_OHMS  2200
#define LED_G_ISET_OHMS 10000
#define LED_B_ISET_OHMS  2200
