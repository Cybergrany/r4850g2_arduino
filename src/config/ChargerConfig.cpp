#include "ChargerConfig.h"
#include <math.h>

namespace psu {
PsuConfig defaultPsuConfig(uint8_t index) {
  return {uint8_t(index + 1), true, limits::defaultVoltage, limits::defaultCurrent,
          limits::defaultVoltage, limits::defaultCurrent, limits::defaultRatedCurrent};
}
Configuration defaultConfiguration() {
  Configuration config = {};
  config.count = 1;
  config.pollMs = limits::defaultPollMs;
  // Preserve the original startup behaviour: poll, but require explicit Apply.
  config.applyOnBoot = false;
  for (uint8_t i = 0; i < PSU_MAX_UNITS; ++i) config.units[i] = defaultPsuConfig(i);
  return config;
}
bool validConfig(const PsuConfig& c) {
  return c.address >= 1 && c.address <= 127 &&
      c.voltage >= limits::minVoltage && c.voltage <= limits::maxVoltage &&
      c.offlineVoltage >= limits::minOfflineVoltage && c.offlineVoltage <= limits::maxVoltage &&
      c.current <= limits::maxCurrent && c.offlineCurrent <= limits::maxCurrent &&
      c.ratedCurrent >= 100 &&
      uint32_t(c.current) * 100 <= uint32_t(c.ratedCurrent) * 120 &&
      uint32_t(c.offlineCurrent) * 100 <= uint32_t(c.ratedCurrent) * 120;
}
bool validConfig(const Configuration& c) {
  if (!c.count || c.count > PSU_MAX_UNITS || c.pollMs < limits::minPollMs ||
      c.pollMs > limits::maxPollMs) return false;
  for (uint8_t i = 0; i < PSU_MAX_UNITS; ++i) {
    if (!validConfig(c.units[i])) return false;
    if (i >= c.count) continue;
    for (uint8_t j = 0; j < i; ++j)
      if (c.units[i].address == c.units[j].address) return false;
  }
  return true;
}
bool changeParameter(PsuConfig& c, Parameter p, float value) {
  if (!isfinite(value) || value < 0 || value > 655.35f) return false;
  PsuConfig next = c;
  const uint16_t scaled = uint16_t(value * 100.0f + 0.5f);
  switch (p) {
    case Parameter::Voltage: next.voltage = scaled; break;
    case Parameter::Current: next.current = scaled; break;
    case Parameter::OfflineVoltage: next.offlineVoltage = scaled; break;
    case Parameter::OfflineCurrent: next.offlineCurrent = scaled; break;
    case Parameter::RatedCurrent: next.ratedCurrent = scaled; break;
    case Parameter::Address:
      if (value < 1 || value > 127 || value != uint8_t(value)) return false;
      next.address = uint8_t(value); break;
    case Parameter::Enabled:
      if (value != 0 && value != 1) return false;
      next.enabled = value != 0; break;
    default: return false;
  }
  if (!validConfig(next)) return false;
  c = next;
  return true;
}
}
