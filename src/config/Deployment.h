#pragma once
#include "ChargerConfig.h"
namespace psu { namespace deployment {
// Change this ID when moving the controller to another physical installation.
// EEPROM from a different deployment is inspectable but cannot authorize writes.
constexpr uint32_t id = 1;
constexpr uint16_t voltage = 5400, offlineVoltage = 5400;
constexpr uint8_t initialSlots = 1;
constexpr uint16_t pollMs = 1000;
// Optional compiled installation template. Zero identities stay unbound; zero
// ratings use the firmware default (50 A). Serial commissioning + save overrides
// these defaults for this deployment. No profile starts automatically.
// Example with initialSlots=2:
//   groups = {{"GROUP1", 0x03, 5500, 0}}; // slots 1+2, total 55 A online
// Fill bindings only after checking the physical units with `diag bus`.
constexpr UnitConfig units[PSU_MAX_UNITS] = {};
constexpr GroupConfig groups[limits::maxGroups] = {};
static_assert(id != 0 && initialSlots >= 1 && initialSlots <= PSU_MAX_UNITS, "Invalid deployment identity/count");
static_assert(voltage >= limits::minVoltage && voltage <= limits::maxVoltage, "Invalid deployment voltage");
static_assert(offlineVoltage >= limits::minOfflineVoltage && offlineVoltage <= limits::maxVoltage, "Invalid offline voltage");
static_assert(pollMs >= limits::minPollMs && pollMs <= limits::maxPollMs, "Invalid deployment poll interval");
} }
