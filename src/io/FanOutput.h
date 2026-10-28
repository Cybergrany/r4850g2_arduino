#pragma once
#include "../config/ControlConfig.h"

namespace psu {
// Hardware adapter only; the controller owns temperature/current policy.
class FanOutput {
 public:
  void begin();
  bool due(uint32_t now);
  void write(uint8_t demand);
 private:
  uint32_t updated_ = 0;
  uint8_t demand_ = 255;
};
}
