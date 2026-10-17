#pragma once
#include "../config/BuildOptions.h"
#if PSU_ENABLE_DISPLAY
#include <Arduino.h>
#include "UiView.h"
#include "Display.h"
#include "WheelInput.h"
#include "Xpt2046Input.h"
namespace psu {
class LocalUi {
 public:
  LocalUi(PsuController& controller, MemoryManager& memory, Display& display, ByteStorage& bytes)
      : controller_(controller), model_(controller, memory), display_(display)
#if PSU_ENABLE_TOUCH
      , calibrationStore_(bytes,memory)
#endif
      { (void)bytes; }
  void begin();
  void tick(uint32_t now);
  void storageTick();
  bool command(uint8_t count, char** args, Print& output, uint32_t now);
  UiAction takeRequest() { const auto r=request_; request_={}; return r; }
  void resetNavigation() { model_.resetNavigation(); dirty_=true; pageReady_=false; }
  bool editing() const { return model_.editing(); }
  bool available() const { return available_; }
 private:
  void request(uint32_t now);
#if PSU_ENABLE_TOUCH
  void touchTick(uint32_t now);
  void drawTouch();
  Xpt2046Input touchInput_;
  TouchCalibration calibration_;
  CalibrationWizard wizard_;
  TouchGesture gesture_;
  TouchSample raw_ = {};
  TouchPoint point_ = {};
  UiAction pointedAction_;
  uint16_t rejected_ = 0;
  bool calibrationWrite_ = false, retryArmed_ = false;
  uint8_t hit_ = 0;
#endif
  PsuController& controller_;
  UiModel model_;
  Display& display_;
#if PSU_ENABLE_TOUCH
  CalibrationStore calibrationStore_;
#endif
  UiFrame frame_;
#if PSU_ENABLE_ENCODER
  WheelInput wheel_;
#endif
  UiAction request_;
  uint32_t lastDraw_ = 0, lastInput_ = 0, lastPage_ = 0;
  bool available_ = false, dirty_ = true, dimmed_ = false, pageReady_ = false;
};
}
#endif
