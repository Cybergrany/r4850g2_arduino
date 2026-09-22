#pragma once
#include "../config/UiConfig.h"
#include <stddef.h>
#ifdef __AVR__
#include <avr/pgmspace.h>
#define UI_TEXT(s) psu::FlashText{PSTR(s)}
#else
#define UI_TEXT(s) psu::FlashText{s}
#endif
namespace psu {
struct FlashText { const char* data; };
enum UiColor : uint8_t { Text, Muted, Good, Warning, Error, Accent };
constexpr uint8_t selectedStyle = 8;
struct UiCell { char character; uint8_t style; };
// A character scene, not a pixel framebuffer. 585 bytes at 26 x 15 cells.
// Layout and device rendering are separate; another renderer can consume this
// scene, or a different layout can consume UiModel on a different-sized screen.
class UiFrame {
 public:
  static constexpr uint16_t cells = uint16_t(ui::columns) * ui::rows;
  void clear();
  void invalidate();
  UiCell cell(uint16_t index) const;
  void cell(uint16_t index, UiCell value);
  void text(uint8_t row, uint8_t column, uint8_t width, const char* value,
            uint8_t style = Text, bool right = false);
  void text(uint8_t row, uint8_t column, uint8_t width, FlashText value,
            uint8_t style = Text, bool right = false);
  void wrap(uint8_t row, uint8_t endRow, FlashText value, uint8_t style = Text);
 private:
  void put(uint8_t row, uint8_t column, uint8_t width, const char* value, bool flash, uint8_t style, bool right);
  char characters_[cells];
  uint8_t styles_[(cells + 1) / 2];
};
// One bounded pass over a new scene. Advance before handing a changed cell to
// the device: invalidation during a partial glyph schedules a complete new pass.
class FrameChanges {
 public:
  void invalidate() { remaining_ = UiFrame::cells; }
  bool next(const UiFrame& frame, const UiFrame& painted, uint8_t& budget,
            uint16_t& index, UiCell& cell);
  bool pending() const { return remaining_ != 0; }
 private:
  uint16_t cursor_ = 0, remaining_ = 0;
};
// Bounded numeric formatting: adjust precision/engineering units or show OVF.
// Never truncate a digit or unit to make a measurement fit.
void formatValue(char* out, uint8_t size, float value, const char* unit, uint8_t decimals = 1);
}
