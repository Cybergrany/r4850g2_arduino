#pragma once
#include "../config/BuildOptions.h"
#if PSU_ENABLE_DISPLAY || PSU_ENABLE_ENCODER
#include "../psu/PsuController.h"
#include "../storage/MemoryManager.h"
#include "OledDisplay.h"
#include "WheelInput.h"

namespace psu {
class LocalUi {
 public:
  LocalUi(PsuController& controller, MemoryManager& memory) : controller_(controller), memory_(memory) {}
  void begin();
  void tick(uint32_t now);
 private:
  PsuController& controller_;
  MemoryManager& memory_;
#if PSU_ENABLE_DISPLAY
  OledDisplay display_;
#endif
#if PSU_ENABLE_ENCODER
  WheelInput wheel_;
#endif
  uint8_t index_ = 0;
  uint8_t item_ = 0;
  bool menu_ = false;
  bool editing_ = false;
  bool error_ = false;
  bool dirty_ = true;
  uint32_t lastInput_ = 0;
  uint32_t lastDraw_ = 0;
};
}
#endif
