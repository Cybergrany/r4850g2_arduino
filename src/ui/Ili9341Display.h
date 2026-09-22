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
  void service(const UiFrame& frame) override;
  bool canDim() const override;
  void dim(bool dimmed) override;
 private:
  class Glyph : public Adafruit_GFX {
   public:
    Glyph() : Adafruit_GFX(ui::cellWidth, ui::cellHeight) {}
    void drawPixel(int16_t x, int16_t y, uint16_t color) override;
    void prepare(char c);
    uint16_t rows[ui::cellHeight] = {};
  } glyph_;
  Adafruit_ILI9341 tft_;
  UiFrame previous_;
  UiCell active_ = {' ', 0};
  uint16_t cursor_ = 0;
  uint8_t scanRow_ = ui::cellHeight;
  bool available_ = false;
};
}
#endif
