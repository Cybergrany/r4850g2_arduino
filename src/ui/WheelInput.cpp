#include "WheelInput.h"
#if PSU_ENABLE_ENCODER
#include "../config/BoardConfig.h"
#include <ClickEncoder.h>
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
WheelEvent WheelInput::read() { return {encoder.getValue(), encoder.getButton() == ClickEncoder::Clicked}; }
}
#endif
