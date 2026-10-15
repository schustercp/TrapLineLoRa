/**
 * @file device_id.h
 * @brief The node's immutable hardware identity — spec §7.8.
 *
 * `FICR.DEVICEID` is a 64-bit unique device identifier, factory-programmed into
 * the nRF52840's Factory Information Configuration Registers. Nordic's product
 * specification says these registers are "pre-programmed in factory and cannot
 * be erased by the user", and every field is read-only, so it survives power
 * loss, our own factory reset, a full chip erase, and being reflashed with
 * different firmware entirely.
 *
 * The trap number is a mutable label; this is the identity underneath it.
 */

#pragma once

#include <Arduino.h>

/// The full 64-bit factory device identifier.
uint64_t deviceId();

/// Uppercase 16-character hex, null-terminated. `len` should be >= 17.
void deviceIdHex(char *out, size_t len);

/// Low 16 bits as 4 hex characters — enough to tell three boxes apart on a
/// BLE scan list without reading a whole 64-bit number in the rain.
void deviceIdShort(char *out, size_t len);
