#include "AlertOutputs.h"
#if !PSU_UI_DEBUG && (PSU_WARNING_PIN >= 0 || PSU_ERROR_PIN >= 0)
#include <Arduino.h>
#endif
namespace psu {
void AlertOutputs::begin() {
#if !PSU_UI_DEBUG && PSU_WARNING_PIN >= 0
  digitalWrite(alerts::warningPin, LOW); pinMode(alerts::warningPin, OUTPUT);
#endif
#if !PSU_UI_DEBUG && PSU_ERROR_PIN >= 0
  digitalWrite(alerts::errorPin, LOW); pinMode(alerts::errorPin, OUTPUT);
#endif
}
void AlertOutputs::write(const AlertManager& state) {
  (void)state;
#if !PSU_UI_DEBUG && PSU_WARNING_PIN >= 0
  digitalWrite(alerts::warningPin, state.warning() ? HIGH : LOW);
#endif
#if !PSU_UI_DEBUG && PSU_ERROR_PIN >= 0
  digitalWrite(alerts::errorPin, state.error() ? HIGH : LOW);
#endif
}
}
