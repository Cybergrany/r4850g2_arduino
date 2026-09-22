#pragma once
#include <stdint.h>

namespace psu { namespace console {
// UART has no terminal-presence signal. Abandoned input must not live forever.
constexpr uint32_t inputIdleTimeoutMs = 30000UL;
constexpr uint8_t inputBytesPerTick = 32;
constexpr uint8_t outputBytesPerTick = 48, traceSlots = 4;
constexpr uint16_t outputBufferBytes = 384, outputReserveBytes = 256;
} }
