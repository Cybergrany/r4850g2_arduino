#pragma once
#include "../config/BuildOptions.h"
#if PSU_ENABLE_DISPLAY || PSU_ENABLE_ENCODER
#include "../psu/PsuController.h"
#include "OledDisplay.h"
#include "WheelInput.h"
namespace psu {
// Existing screen remains a monitor. Future editing UIs should use the same
// preview/queue/structured diagnostics API as SerialConsole.
class LocalUi {
 public:
  explicit LocalUi(PsuController& controller) : controller_(controller) {}
  void begin();
  void tick(uint32_t now);
 private:
  PsuController& controller_;
#if PSU_ENABLE_DISPLAY
  OledDisplay display_;
#endif
#if PSU_ENABLE_ENCODER
  WheelInput wheel_;
#endif
  uint8_t index_ = 0;
  bool dirty_ = true;
  uint32_t lastDraw_ = 0;
};
}
#endif
