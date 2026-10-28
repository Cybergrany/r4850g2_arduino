#include "FanOutput.h"
#if PSU_FAN_PWM_PIN >= 0 && !PSU_UI_DEBUG
#include <Arduino.h>
#endif

namespace psu {
void FanOutput::begin() {
  updated_ = 0; demand_ = 255;
#if PSU_FAN_PWM_PIN >= 0 && !PSU_UI_DEBUG
  // Establish full cooling before display/CAN initialization. Set the latch
  // before enabling output so an active-high driver does not see a LOW pulse.
  digitalWrite(control::fanPwmPin, control::fanPwmInverted ? LOW : HIGH);
  pinMode(control::fanPwmPin, OUTPUT);
#endif
}
bool FanOutput::due(uint32_t now) {
  if (uint32_t(now - updated_) < control::externalFanUpdateMs) return false;
  updated_ = now; return true;
}
void FanOutput::write(uint8_t demand) {
  if (demand == demand_) return;
  demand_ = demand;
#if PSU_FAN_PWM_PIN >= 0 && !PSU_UI_DEBUG
  analogWrite(control::fanPwmPin, control::fanPwmInverted ? 255 - demand : demand);
#endif
}
}
