#pragma once
#include <stdint.h>
namespace psu { namespace canScheduling {
// Safe phase boundaries only. Under pressure, release more ring slots without
// allowing a continuous producer to monopolize control, input or protection.
constexpr uint8_t normalFrames = 4, burstFrames = 12;
constexpr uint8_t highWater = 8, lowWater = 2;
static_assert(lowWater < highWater && normalFrames < burstFrames, "Invalid RX scheduling bounds");
} }
