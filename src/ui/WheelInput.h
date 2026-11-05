#pragma once
#include "../config/BuildOptions.h"
#if PSU_ENABLE_ENCODER
#include "UiModel.h"

namespace psu {
class WheelInput {
 public:
  void begin();
  UiInput read();
  void cancelGesture() { blocked_ = true; released_ = false; }
 private:
  bool held_ = false, blocked_ = false, released_ = false;
  uint32_t releasedAt_ = 0;
};
}
#endif
