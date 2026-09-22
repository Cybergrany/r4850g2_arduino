#include "OledDisplay.h"
#if PSU_ENABLE_DISPLAY
#include "../config/BoardConfig.h"
#include "UiText.h"
#include <Wire.h>

namespace psu {
void OledDisplay::begin() {
  Wire.begin(); Wire.setClock(board::displayBusHz);
  Wire.setWireTimeout(board::displayWireTimeoutUs, true);
  Wire.beginTransmission(board::displayAddress);
  available_ = Wire.endTransmission() == 0;
  if (!available_) return; // Missing optional display must not prevent serial input.
  oled_.begin(&Adafruit128x64, board::displayAddress);
  oled_.setFont(Adafruit5x7); oled_.clear();
}
void OledDisplay::status(const PsuController& controller, uint8_t index, uint32_t now) {
  if (!available_) return;
  const Psu& unit = controller.unit(index);
  oled_.clear();
  oled_.print(F("PSU ")); oled_.print(index + 1); oled_.print(F(" addr ")); oled_.println(unit.config().address);
  if (!controller.ready()) { oled_.println(F("CAN init failed")); return; }
  if (unit.stale(now, uint32_t(controller.pollInterval()) * 3)) {
    oled_.println(F("Waiting / stale"));
    oled_.print(F("Last: ")); oled_.println(stateName(unit.commandStatus().state));
    return;
  }
  auto value = [this, &unit](protocol::Metric m) {
    if (unit.hasMetric(m)) oled_.print(unit.metric(m), 1);
    else oled_.print(F("--"));
  };
  oled_.print(F("In ")); value(protocol::InputVoltage); oled_.print(F("V ")); value(protocol::InputCurrent); oled_.println('A');
  oled_.print(F("AC ")); value(protocol::InputPower); oled_.print(F("W ")); value(protocol::InputFrequency); oled_.println(F("Hz"));
  oled_.print(F("T ")); value(protocol::InputTemperature); oled_.print('/'); value(protocol::OutputTemperature); oled_.println('C');
  oled_.print(F("Out ")); value(protocol::OutputVoltage); oled_.println('V');
  value(protocol::OutputCurrent); oled_.print('/'); value(protocol::CurrentCapacity); oled_.println('A');
  value(protocol::OutputPower); oled_.println('W');
  oled_.println(stateName(unit.commandStatus().state));
}
void OledDisplay::menu(const PsuController& controller, uint8_t index, uint8_t item, bool editing, bool error) {
  if (!available_) return;
  const auto& c = controller.unit(index).config();
  oled_.clear(); oled_.print(F("PSU ")); oled_.print(index + 1); oled_.println(F(" settings"));
  oled_.print(editing ? '*' : '>');
  switch (item) {
    case 0: oled_.print(F("Select PSU ")); oled_.println(index + 1); break;
    case 1: oled_.print(F("Volts ")); oled_.println(c.voltage / 100.0f); break;
    case 2: oled_.print(F("Amps ")); oled_.println(c.current / 100.0f); break;
    case 3: oled_.print(F("Offline V ")); oled_.println(c.offlineVoltage / 100.0f); break;
    case 4: oled_.print(F("Offline A ")); oled_.println(c.offlineCurrent / 100.0f); break;
    case 5: oled_.println(F("Apply online")); break;
    case 6: oled_.println(F("Persist PSU")); break;
    case 7: oled_.println(F("Save EEPROM")); break;
    case 8: oled_.println(F("Back")); break;
  }
  oled_.println(F("Rotate / click"));
  oled_.println(stateName(controller.unit(index).commandStatus().state));
  if (error) oled_.println(F("Action failed"));
}
}
#endif
