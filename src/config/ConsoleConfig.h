#pragma once
#include <stdint.h>

namespace psu { namespace console {
// UART has no terminal-presence signal. Abandoned input must not live forever.
constexpr uint32_t inputIdleTimeoutMs = 30000UL;
constexpr uint8_t inputBytesPerTick = 32;
} }
