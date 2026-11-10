#pragma once
#include "../config/BuildOptions.h"
#if PSU_ENABLE_DISPLAY
#include "Display.h"
#include <Adafruit_ILI9341.h>
namespace psu {
class Ili9341Display : public Display {
 public:
  Ili9341Display();
  bool begin() override;
  void invalidate(bool change = false) override;
  void service(const UiFrame& frame) override;
  bool pending() const override { return changes_.pending() || scanRow_ != ui::cellHeight; }
  bool canDim() const override;
  void dim(bool dimmed) override;
  DisplayStats statistics() const override { return stats_; }
  void resetStatistics() override { stats_ = {}; }
 private:
  class Glyph {
   public:
    void prepare(char c);
    uint16_t rows[ui::cellHeight] = {};
  } glyph_;
  Adafruit_ILI9341 tft_;
  UiFrame previous_;
  FrameChanges changes_;
  UiCell active_ = {' ', 0};
  uint16_t activeIndex_ = 0;
  uint16_t dirtyRows_ = 0;
  uint8_t scanRow_ = ui::cellHeight;
  bool available_ = false;
  bool changeScene_ = false;
  DisplayStats stats_;
  uint32_t sceneStarted_ = 0;
};
}
#endif
