#include "WheelInput.h"
#if PSU_ENABLE_ENCODER
#include "../config/BoardConfig.h"
#include "../vendor/ClickEncoder/ClickEncoder.h"
#include <TimerOne.h>

namespace psu {
namespace {
ClickEncoder encoder(board::encoderClock, board::encoderData, board::encoderButton,
                     board::encoderStepsPerNotch);
void serviceEncoder() { encoder.service(); }
}
void WheelInput::begin() {
  encoder.setAccelerationEnabled(false);
  encoder.setDoubleClickEnabled(false);
  Timer1.initialize(1000);
  Timer1.attachInterrupt(serviceEncoder);
}
UiInput WheelInput::read() {
  const auto button = encoder.getButton();
  UiInput event = {encoder.getValue(), false, false};
  if (blocked_) {
    // ClickEncoder never emits Pressed. Observe a stable physical release,
    // consume all old events, then require a new physical press.
    if (!released_ || uint32_t(millis() - releasedAt_) < 50) event.delta = 0;
    if (digitalRead(board::encoderButton) != LOW) {
      if (!released_) { released_ = true; releasedAt_ = millis(); }
    } else if (released_ && uint32_t(millis() - releasedAt_) >= 50) {
      blocked_ = false; held_ = false;
    } else released_ = false;
    return event;
  }
  if (button == ClickEncoder::Held) { event.held = !held_; held_ = true; }
  else if (button == ClickEncoder::Released) held_ = false;
  else if (button == ClickEncoder::Clicked) { event.clicked = !held_; held_ = false; }
  return event;
}
}
#endif
