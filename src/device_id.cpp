/**
 * @file device_id.cpp
 * @brief See device_id.h and spec §7.8.
 */

#include "device_id.h"

#include <stdio.h>

uint64_t deviceId() {
    return (static_cast<uint64_t>(NRF_FICR->DEVICEID[1]) << 32) |
           static_cast<uint64_t>(NRF_FICR->DEVICEID[0]);
}

void deviceIdHex(char *out, size_t len) {
    if (out == nullptr || len == 0) return;
    // %llX is not reliably available in newlib-nano, so print it as two words.
    snprintf(out, len, "%08lX%08lX", static_cast<unsigned long>(NRF_FICR->DEVICEID[1]),
             static_cast<unsigned long>(NRF_FICR->DEVICEID[0]));
}

void deviceIdShort(char *out, size_t len) {
    if (out == nullptr || len == 0) return;
    snprintf(out, len, "%04lX",
             static_cast<unsigned long>(NRF_FICR->DEVICEID[0] & 0xFFFFUL));
}
