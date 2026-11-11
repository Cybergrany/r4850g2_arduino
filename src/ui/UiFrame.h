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
struct UiCell { char character; uint8_t style; };
// Grid rectangle: row/height, column/width and focus fit in three bytes.
struct UiOutline {
  uint8_t a, b, c;
  constexpr UiOutline(uint8_t rh = 0, uint8_t cw = 0, uint8_t wf = 0) : a(rh), b(cw), c(wf) {}
  uint8_t row() const { return a & 15; }
  uint8_t height() const { return a >> 4; }
  uint8_t column() const { return b & 31; }
  uint8_t width() const { return (b >> 5) | ((c & 3) << 3); }
  bool focused() const { return c & 4; }
  bool valid() const { return height() && width(); }
  bool sameBounds(UiOutline o) const { return a == o.a && b == o.b && (c & 3) == (o.c & 3); }
  bool operator==(UiOutline o) const { return a == o.a && b == o.b && c == o.c; }
};
// A compact character/outline scene, not a pixel framebuffer.
// Layout and device rendering are separate; another renderer can consume this
// scene, or a different layout can consume UiModel on a different-sized screen.
class UiFrame {
 public:
  static constexpr uint16_t cells = uint16_t(ui::columns) * ui::rows;
  static constexpr uint8_t maxOutlines = 9;
  void clear();
  void invalidate();
  UiCell cell(uint16_t index) const;
  char character(uint16_t index) const { return characters_[index]; }
  void cell(uint16_t index, UiCell value);
  void text(uint8_t row, uint8_t column, uint8_t width, const char* value,
            uint8_t style = Text, bool right = false);
  void text(uint8_t row, uint8_t column, uint8_t width, FlashText value,
            uint8_t style = Text, bool right = false);
  void largeNumber(uint8_t row, const char* value, uint8_t style = Text);
  void wrap(uint8_t row, uint8_t endRow, FlashText value, uint8_t style = Text);
  bool outline(uint8_t row, uint8_t column, uint8_t width, uint8_t height, bool focused = false);
  UiOutline outline(uint8_t index) const { return index < outlineCount_ ? outlines_[index] : UiOutline{}; }
  void outline(uint8_t index, UiOutline value);
  uint8_t outlineCount() const { return outlineCount_; }
  void clearOutlines() { outlineCount_ = 0; }
  bool sameText(const UiFrame& other) const;
 private:
  void style(uint16_t index, uint8_t value);
  void put(uint8_t row, uint8_t column, uint8_t width, const char* value, bool flash, uint8_t style, bool right);
  char characters_[cells];
  uint8_t styles_[(cells * 3 + 7) / 8];
  UiOutline outlines_[maxOutlines];
  uint8_t outlineCount_ = 0;
};
// One bounded pass over a new scene. Advance before handing a changed cell to
// the device: invalidation during a partial glyph schedules a complete new pass.
class FrameChanges {
 public:
  void invalidate() { remaining_ = UiFrame::cells; }
  bool next(const UiFrame& frame, const UiFrame& painted, uint8_t& budget,
            uint16_t& index, UiCell& cell);
  bool pending() const { return remaining_ != 0; }
  bool fullPass() const { return remaining_ == UiFrame::cells; }
  void finish() { remaining_ = 0; }
 private:
  uint16_t cursor_ = 0, remaining_ = 0;
};
// Bounded numeric formatting: adjust precision/engineering units or show OVF.
// Never truncate a digit or unit to make a measurement fit.
void formatValue(char* out, uint8_t size, float value, const char* unit, uint8_t decimals = 1);
}
