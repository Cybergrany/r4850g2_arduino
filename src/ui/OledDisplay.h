#pragma once
#include "../config/BuildOptions.h"
#if PSU_ENABLE_DISPLAY
#include <SSD1306Ascii.h>
#include <SSD1306AsciiWire.h>
#include "../psu/PsuController.h"
namespace psu {
class OledDisplay {
 public:
  void begin();
  void status(const PsuController& controller, uint8_t index, uint32_t now);
 private:
  SSD1306AsciiWire oled_;
  bool available_ = false;
};
}
#endif
