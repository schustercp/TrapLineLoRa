/**
 * @file console.h
 * @brief Text console over BLE (Nordic UART Service) and USB serial at once.
 *
 * Plain text over NUS because it works from any phone with Bluefruit Connect or
 * nRF Connect and needs no app development — and because Web Bluetooth is
 * unavailable on every stock iOS browser, Chrome included (spec §17 Q11).
 *
 * Anything logged goes to both transports, and commands are accepted from
 * either, so the same firmware is usable from a phone in the bush and from a
 * laptop on the bench.
 */

#pragma once

#include <Arduino.h>

namespace console {

/// Bring up USB serial and the BLE UART. `name` is the advertised device name.
void begin(const char *name);

/// Pump both input streams and dispatch commands. Call from loop().
void poll();

/// Print one line to both transports.
void log(const char *line);
void logf(const char *fmt, ...);

/// True while a BLE central is connected.
bool bleConnected();

/// Update the advertised name (e.g. after the device ID is known).
void setName(const char *name);

}  // namespace console
