#pragma once
#include <stdint.h>

#ifndef PSU_SERIAL_ALERT_INTERVAL_MS
#define PSU_SERIAL_ALERT_INTERVAL_MS 5000UL
#endif

namespace psu { namespace console {
// UART has no terminal-presence signal. Abandoned input must not live forever.
constexpr uint32_t inputIdleTimeoutMs = 30000UL;
constexpr uint8_t inputBytesPerTick = 32;
constexpr uint8_t outputBytesPerTick = 48, traceSlots = 4;
constexpr uint16_t outputBufferBytes = 384, outputReserveBytes = 256;
// Unsolicited status is presentation, not protection. Commands/ACKs remain
// immediate; avoid recomputing every member's status on every idle loop.
constexpr uint16_t statusIntervalMs = 100;
// Periodic current-state summaries; zero disables automatic broadcasts.
constexpr uint32_t alertIntervalMs = PSU_SERIAL_ALERT_INTERVAL_MS;
static_assert(alertIntervalMs == 0 || (alertIntervalMs >= 1000 && alertIntervalMs <= 3600000UL &&
    alertIntervalMs % 1000 == 0), "Serial alert interval must be 0 or whole seconds from 1 to 3600");
} }
