#pragma once
#include "../config/BuildOptions.h"
#if PSU_ENABLE_DISPLAY
#include "UiView.h"
#include "Display.h"
#include "WheelInput.h"
namespace psu {
class LocalUi {
 public:
  LocalUi(PsuController& controller, MemoryManager& memory, Display& display)
      : controller_(controller), model_(controller, memory), display_(display) {}
  void begin();
  void tick(uint32_t now);
  bool available() const { return available_; }
 private:
  PsuController& controller_;
  UiModel model_;
  Display& display_;
  UiFrame frame_;
#if PSU_ENABLE_ENCODER
  WheelInput wheel_;
#endif
  uint32_t lastDraw_ = 0, lastInput_ = 0, lastPage_ = 0;
  bool available_ = false, dirty_ = true, dimmed_ = false;
};
}
#endif
