#pragma once
#include "../config/BuildOptions.h"
#if PSU_ENABLE_ENCODER
#include <stdint.h>

namespace psu {
struct WheelEvent { int16_t delta; bool clicked; };
class WheelInput {
 public:
  void begin();
  WheelEvent read();
};
}
#endif
