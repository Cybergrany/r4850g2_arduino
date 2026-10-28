#pragma once
#include "UiModel.h"
#include "UiFrame.h"
namespace psu {
struct UiButton {
  uint8_t row, col, width, height;
  UiAction action;
  FlashText label;
};
// Drawing and hit testing consume the same rectangles. Coordinates are grid cells.
class UiLayout {
 public:
  static uint8_t buttons(const UiModel& model, UiButton* output);
  static uint8_t hit(const UiModel& model, int16_t x, int16_t y, UiAction& action);
  static void controls(UiFrame& frame, const UiModel& model);
  static void extras(UiFrame& frame, const UiModel& model, const PsuController& controller, uint32_t now);
  static constexpr uint8_t capacity = 8;
};
}
