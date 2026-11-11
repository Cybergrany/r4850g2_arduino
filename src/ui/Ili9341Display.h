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
  bool pending() const override { return changes_.pending() || scanRow_ != ui::cellHeight || outlineRemaining_ || outlineStage_ || outlinesAfterText_; }
  bool canDim() const override;
  void dim(bool dimmed) override;
  DisplayStats statistics() const override { return stats_; }
  void resetStatistics() override { stats_ = {}; }
 private:
  class Glyph {
   public:
    void prepare(char c, uint8_t firstRow = 0, uint8_t endRow = ui::cellHeight);
    uint16_t row(uint8_t y) const { return rows[y/2]; }
    // Both supported font scales repeat each row at least twice.
    uint16_t rows[ui::cellHeight/2] = {};
  } glyph_;
  struct Rect { uint16_t x, y, w, h; };
  static Rect bounds(UiOutline outline);
  static Rect edge(UiOutline outline, uint8_t edge);
  static bool intersects(Rect a, Rect b);
  static void ink(uint16_t* pixels, Rect strip, uint16_t x, uint16_t y, const Glyph& glyph, uint16_t color);
  static void outlines(const UiFrame& frame, Rect strip, uint16_t* pixels, int8_t replace = -1, UiOutline value = {});
  void text(const UiFrame& frame, Rect strip, uint16_t* pixels);
  void transfer(Rect strip, uint16_t* pixels);
  bool outlineStep(const UiFrame& frame, bool eraseOnly);
  Adafruit_ILI9341 tft_;
  UiFrame previous_;
  FrameChanges changes_;
  UiCell active_ = {' ', 0};
  uint16_t activeIndex_ = 0;
  uint16_t dirtyRows_ = 0;
  uint16_t dirtyColumns_[ui::cellHeight/2] = {};
  uint8_t scanRow_ = ui::cellHeight;
  UiOutline outlineTarget_;
  uint16_t outlineOffset_ = 0;
  uint8_t outlineNext_ = 0, outlineRemaining_ = 0, outlineStage_ = 0, outlineEdge_ = 0;
  bool available_ = false;
  bool activeBorder_ = false;
  bool sceneCheck_ = false, outlinesAfterText_ = false;
  bool changeScene_ = false;
  DisplayStats stats_;
  uint32_t sceneStarted_ = 0;
};
}
#endif
