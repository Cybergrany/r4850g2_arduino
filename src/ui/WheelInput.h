#pragma once
#include "../config/BuildOptions.h"
#if PSU_ENABLE_ENCODER
#include "UiModel.h"

namespace psu {
class WheelInput {
 public:
  void begin();
  UiInput read();
 private:
  bool held_ = false;
};
}
#endif
