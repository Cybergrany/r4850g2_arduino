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
  if (!available_) return; // Optional hardware cannot hold up the control loop.
  oled_.begin(&Adafruit128x64, board::displayAddress);
  oled_.setFont(Adafruit5x7); oled_.clear();
}
void OledDisplay::status(const PsuController& c, uint8_t slot, uint32_t now) {
  if (!available_) return;
  const auto& config = c.configuration();
  const auto* d = c.deviceForSlot(slot, now);
  const int8_t group = groupFor(config, slot);
  oled_.clear();
  oled_.print(F("PSU ")); oled_.print(slot + 1); oled_.print(' ');
  oled_.println(group >= 0 ? config.groups[group].name : "ungrouped");
  if (!c.ready()) { oled_.println(F("CAN init failed")); return; }
  oled_.print(F("Addr ")); if (d) oled_.println(d->address); else oled_.println('-');
  // Short codes fit the existing display; serial diagnostics provide remedies.
  switch (c.issue(slot, now)) {
    case Issue::None: oled_.println(F("Online / ready")); break;
    case Issue::Missing: oled_.println(F("MISSING")); break;
    case Issue::Unbound: oled_.println(F("UNBOUND identity")); break;
    case Issue::Unassigned: oled_.println(F("NO GROUP")); break;
    case Issue::IdentityPending: oled_.println(F("VERIFY identity")); break;
    case Issue::Conflict: oled_.println(F("IDENTITY CONFLICT")); break;
    case Issue::NoTelemetry: oled_.println(F("DATA/BROADCAST STALE")); break;
    case Issue::NotReady: oled_.println(F("PSU NOT READY")); break;
    case Issue::DeploymentMismatch: oled_.println(F("WRONG DEPLOYMENT")); break;
    default: oled_.println(F("See serial diag")); break;
  }
  auto value = [this, &c, slot, now](protocol::Metric m) {
    float v;
    if (c.metric(slot, m, v, now)) oled_.print(v, 1); else oled_.print(F("--"));
  };
  oled_.print(F("Last ")); value(protocol::OutputVoltage); oled_.print(F("V "));
  value(protocol::OutputCurrent); oled_.println('A');
  oled_.print(F("Bus target "));
  if (config.operating.voltageAuthorized) oled_.print(config.operating.voltage / 100.0f, 1);
  else oled_.print(F("--"));
  oled_.println('V');
  oled_.print(F("Group draft "));
  if (group >= 0) oled_.print(config.groups[group].current / 100.0f, 1);
  else oled_.print(F("--"));
  oled_.println('A');
  oled_.println(stateName(c.report().units[slot].state));
  oled_.println(F("Config: serial help"));
}
}
#endif
