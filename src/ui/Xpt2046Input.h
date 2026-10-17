#pragma once
#include "../config/BuildOptions.h"
#if PSU_ENABLE_TOUCH
#include "Touch.h"
namespace psu {
class Xpt2046Input {
 public:
  void begin();
  bool read(TouchSample& sample, uint32_t now);
  uint32_t maximumMicros() const { return maximumMicros_; }
 private:
  uint32_t lastRead_ = 0, maximumMicros_ = 0;
  bool contact_ = false;
};
}
#endif
