/**
 * @file aes128.h
 * @brief AES-128 block encryption and AES-CMAC (RFC 4493).
 *
 * Deliberately free of Arduino and nRF headers so it compiles and unit-tests
 * natively on a PC (`pio test -e native`). Encrypt-only: TLP authenticates but
 * does not encrypt (spec §7.5), and CMAC needs only the forward direction.
 *
 * A few blocks per frame at 64 MHz is microseconds, so a software
 * implementation costs nothing measurable. Using the nRF52840's ECB peripheral
 * directly would fight the SoftDevice for ownership; `sd_ecb_block_encrypt()`
 * is the supported alternative if this ever needs to be faster.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#define AES128_BLOCK_SIZE 16
#define AES128_KEY_SIZE   16

/// Encrypt one 16-byte block. `in` and `out` may alias.
void aes128_encrypt_block(const uint8_t key[AES128_KEY_SIZE],
                          const uint8_t in[AES128_BLOCK_SIZE],
                          uint8_t out[AES128_BLOCK_SIZE]);

/// Full 16-byte AES-CMAC over `msg`. RFC 4493.
void aes128_cmac(const uint8_t key[AES128_KEY_SIZE], const uint8_t *msg, size_t len,
                 uint8_t mac[AES128_BLOCK_SIZE]);

/// The truncated 4-byte MIC TLP actually puts on the air (spec §7.5).
void aes128_cmac_mic4(const uint8_t key[AES128_KEY_SIZE], const uint8_t *msg, size_t len,
                      uint8_t mic[4]);

/// Constant-time comparison of two 4-byte MICs. Returns true when equal.
bool aes128_mic4_equal(const uint8_t a[4], const uint8_t b[4]);
