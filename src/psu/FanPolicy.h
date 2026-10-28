#pragma once
#include "../config/ControlConfig.h"
#include <math.h>

namespace psu {
// Temperature-only target. Callers override to full duty for missing telemetry,
// high per-unit current, or a MAX mode still held by the shared cooling policy.
inline uint8_t externalFanCurve(float temperature) {
  if (!isfinite(temperature) || temperature >= control::fanOutputTempThreshold) return 255;
  if (temperature <= control::externalFanOffTemp) return 0;
  // Smoothstep x*x*(3-2*x), x in Q8. No pow(), lookup table or allocation.
  const uint32_t x = uint16_t((temperature - control::externalFanOffTemp) *
      (256.0f / (control::fanOutputTempThreshold - control::externalFanOffTemp)));
  return uint8_t((x * x * (768UL - 2 * x) * 255UL + (1UL << 23)) >> 24);
}
}
