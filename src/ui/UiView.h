#pragma once
#include "UiFrame.h"
#include "UiModel.h"
namespace psu {
class UiView {
 public:
  static void compose(UiFrame& frame, const UiModel& model, const PsuController& controller, uint32_t now);
};
}
