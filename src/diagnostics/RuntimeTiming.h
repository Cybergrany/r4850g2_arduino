#pragma once
#include <stdint.h>

namespace psu { namespace timing {
struct Diagnostics { uint16_t invalidSamples, maxBackstepUs; };
// For short work/budget measurements only, not uptime or safety deadlines.
// Normal uint32_t rollover is valid. A backwards sample (or an interval over
// half the clock range) is observable and ends the current cooperative slice.
constexpr uint32_t invalid = UINT32_MAX;
uint32_t elapsed(uint32_t started, uint32_t now);
Diagnostics diagnostics();
} }
