#include "PsuActiveOutput.h"
#if !PSU_UI_DEBUG && PSU_ACTIVE_PIN >= 0
#include <Arduino.h>
#endif
namespace psu {
void PsuActiveOutput::begin() {
#if !PSU_UI_DEBUG && PSU_ACTIVE_PIN >= 0
  digitalWrite(control::activePin, LOW);
  pinMode(control::activePin, OUTPUT);
#endif
}
void PsuActiveOutput::write(bool active) {
  (void)active;
#if !PSU_UI_DEBUG && PSU_ACTIVE_PIN >= 0
  digitalWrite(control::activePin, active ? HIGH : LOW);
#endif
}
}
