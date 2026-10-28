#include "ChargeAllowOutput.h"
#if PSU_CHG_ALLOW_PIN >= 0 && !PSU_UI_DEBUG
#include <Arduino.h>
namespace psu {
void ChargeAllowOutput::begin() {
  digitalWrite(control::chargeAllowPin, LOW);
  pinMode(control::chargeAllowPin, OUTPUT);
  allowed_ = false;
}
void ChargeAllowOutput::write(bool allowed) {
  if (allowed == allowed_) return;
  digitalWrite(control::chargeAllowPin, allowed ? HIGH : LOW);
  allowed_ = allowed;
}
}
#endif
