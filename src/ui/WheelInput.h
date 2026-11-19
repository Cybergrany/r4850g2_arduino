#pragma once
#include "../config/BuildOptions.h"
#if PSU_ENABLE_ENCODER
#include "UiModel.h"

namespace psu {
class WheelInput {
 public:
  void begin();
  UiInput read();
  bool takeResetRequest() { const bool pending=resetPending_; resetPending_=false; return pending; }
  // Cancel ordinary UI input without restarting a physical recovery hold.
  void cancelGesture() { blocked_ = true; released_ = held_ = clicked_ = false; }
  void consumeResetGesture() { cancelGesture(); resetFired_=true; resetPending_=false; }
 private:
  bool held_ = false, clicked_ = false, blocked_ = false, released_ = false;
  bool pressed_ = false, resetFired_ = false, resetPending_ = false;
  uint32_t releasedAt_ = 0, pressedAt_ = 0;
};
}
#endif
