#include "RuntimeTiming.h"
#ifdef __AVR__
#include <util/atomic.h>
#endif

namespace psu { namespace timing {
namespace { volatile uint16_t invalidSamples = 0, maxBackstepUs = 0; }
uint32_t elapsed(uint32_t started, uint32_t now) {
  const uint32_t value = now - started;
  if (value <= INT32_MAX) return value;
  // The counter counts invalid measurements, not unique physical clock jumps.
  // Both main-loop measurements and the bounded CAN ISR use this helper.
#ifdef __AVR__
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
#endif
  {
    if (invalidSamples != UINT16_MAX) ++invalidSamples;
    const uint32_t backwards = started - now;
    const uint16_t bounded = backwards > UINT16_MAX ? UINT16_MAX : backwards;
    if (bounded > maxBackstepUs) maxBackstepUs = bounded;
  }
  return invalid;
}
Diagnostics diagnostics() {
  Diagnostics result;
#ifdef __AVR__
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
#endif
  { result.invalidSamples = invalidSamples; result.maxBackstepUs = maxBackstepUs; }
  return result;
}
} }
