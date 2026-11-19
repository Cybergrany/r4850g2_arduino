#include "WheelInput.h"
#if PSU_ENABLE_ENCODER
#include "../config/BoardConfig.h"
#include "../config/UiConfig.h"
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
  const uint32_t now=millis();
  const bool down=digitalRead(board::encoderButton)==LOW;
  const bool released=released_ && uint32_t(now-releasedAt_)>=ui::wheelReleaseMs;
  if(down) {
    if(!pressed_) { pressed_=true; pressedAt_=now; }
    if(blocked_ && released) { blocked_=false; held_=clicked_=false; }
    released_=false;
  } else {
    if(!released_) { released_=true; releasedAt_=now; }
    if(released) { pressed_=false; resetFired_=false; }
  }
  if(down && !resetFired_ && uint32_t(now-pressedAt_)>=ui::displayResetHoldMs) {
    resetFired_=resetPending_=true;
    cancelGesture(); // Includes its eventual release, bounce and queued click.
  }
  // Turning while holding cannot edit a value or select a confirmation. Drain
  // the encoder throughout the gesture, including the release debounce window.
  if(down || !released) event.delta=0;
  if(blocked_) return event;
  if(button==ClickEncoder::Held || button==ClickEncoder::Released) held_=true;
  else if(button==ClickEncoder::Clicked) clicked_=true;
  // Defer the ordinary 1.2 s long press until release: a ten-second recovery
  // must never first perform Back/Cancel/Configure on the current screen.
  if(!down && released) {
    event.held=held_; event.clicked=clicked_ && !held_;
    held_=clicked_=false;
  }
  return event;
}
}
#endif
