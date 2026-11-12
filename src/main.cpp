#include <Arduino.h>
#include "config/BuildOptions.h"
#if PSU_CAN_OBSERVER
#include "observer/BusObserver.h"
#include "can/Mcp2515Transport.h"
#include "io/ChargeAllowOutput.h"
#include "config/AlertConfig.h"
namespace {
psu::Mcp2515Transport can;
psu::BusObserver observer(can,Serial);
#if PSU_CHG_ALLOW_PIN >= 0
psu::ChargeAllowOutput cutoff;
#endif
void lowOutput(int8_t pin) { if(pin>=0) { digitalWrite(pin,LOW); pinMode(pin,OUTPUT); } }
void deselect(uint8_t pin) { digitalWrite(pin,HIGH); pinMode(pin,OUTPUT); }
constexpr bool parkedChipSelect(int pin) {
  return pin==psu::board::displayChipSelect || pin==psu::touch::chipSelect;
}
static_assert(!parkedChipSelect(PSU_CHG_ALLOW_PIN) && !parkedChipSelect(PSU_ACTIVE_PIN) &&
    !parkedChipSelect(PSU_WARNING_PIN) && !parkedChipSelect(PSU_ERROR_PIN),
    "Observer outputs conflict with parked TFT/touch chip selects");
}
void setup() {
#if PSU_CHG_ALLOW_PIN >= 0
  cutoff.begin(); // First hardware action. No EEPROM/default can release AC.
#endif
  lowOutput(PSU_ACTIVE_PIN); lowOutput(PSU_WARNING_PIN); lowOutput(PSU_ERROR_PIN);
  // Keep the installed screen/touch electrically idle on shared SPI even
  // though neither display driver is linked into this application.
  deselect(psu::board::displayChipSelect); deselect(psu::touch::chipSelect);
  Serial.begin(psu::board::serialBaud); observer.begin(millis());
}
void loop() {
  observer.tick(millis());
#if PSU_CHG_ALLOW_PIN >= 0
  cutoff.write(observer.allowAc());
#endif
}
#else
#include "app/Application.h"

namespace { psu::Application application; }
void setup() { application.begin(); }
void loop() { application.tick(); }
#endif
