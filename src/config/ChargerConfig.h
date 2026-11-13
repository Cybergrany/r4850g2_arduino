#pragma once
#include <stdint.h>
#include "BuildOptions.h"

namespace psu {
using UnitMask = uint16_t;
constexpr UnitMask allUnitsMask = (UnitMask(1) << PSU_MAX_UNITS) - 1;
namespace limits {
constexpr uint16_t minVoltage = 4150, maxVoltage = 5850, minOfflineVoltage = 4800;
constexpr uint16_t maxCurrent = 6000, defaultRatedCurrent = 5000;
constexpr uint16_t minPollMs = 1000, maxPollMs = 10000;
constexpr uint16_t commandGapMs = 250, ackTimeoutMs = 750;
constexpr uint8_t maxGroups = 8, groupNameLength = 8;
constexpr uint32_t minimumStaleMs = 5000;
constexpr uint32_t identityLeaseMs = 15000, confirmationMs = 15000;
constexpr uint16_t discoveryGapMs = 100, identityRefreshMs = 3000;
// A telemetry request yields a burst of about 14 replies at 125 kbit/s.
// Space read requests while giving nine units enough slots for DATA and INFO.
constexpr uint16_t readRequestGapMs = 25;
}
struct Identity { uint8_t bytes[6]; };
bool identified(const Identity& id);
bool sameIdentity(const Identity& a, const Identity& b);
struct UnitConfig { Identity identity; uint16_t ratedCurrent; };
struct GroupConfig {
  char name[limits::groupNameLength + 1];
  UnitMask members; // Stable, global controller slots. Groups never overlap.
  uint16_t offlineCurrent; // Offline TOTAL centiamps; online totals are derived.
};
enum class ScopeKind : uint8_t { Global, Group, Unit };
struct SettingScope {
  ScopeKind kind;
  uint8_t index;
  SettingScope(ScopeKind k = ScopeKind::Global, uint8_t i = 0) : kind(k), index(i) {}
};
enum class SettingFields : uint8_t { Voltage = 1, Current = 2, Both = 3 };
inline bool includes(SettingFields fields, SettingFields field) { return uint8_t(fields) & uint8_t(field); }
struct SettingSummary { uint16_t minimum, maximum; };
// Separate from drafts: only an explicitly queued online apply authorizes these.
struct OperatingProfile {
  uint16_t voltage[PSU_MAX_UNITS];
  uint16_t current[PSU_MAX_UNITS];
  UnitMask currentMask, voltageMask;
};
enum class ThermalStop : uint8_t { None, Temperature, Duration };
struct Configuration {
  uint32_t deploymentId;
  uint16_t voltage[PSU_MAX_UNITS], current[PSU_MAX_UNITS];
  uint16_t offlineVoltage, pollMs;
  uint8_t count;
  bool autoResume;
  ThermalStop thermalStop = ThermalStop::None; // Packed with autoResume since schema 4.
  UnitMask acceptedAbsent = 0; // Bound identities permitted to be absent.
  UnitMask disabled = 0; // Explicit participation choice, independent of absence permission.
  UnitConfig units[PSU_MAX_UNITS];
  GroupConfig groups[limits::maxGroups];
  OperatingProfile operating;
};
void defaultConfiguration(Configuration& config);
Configuration defaultConfiguration();
bool validConfig(const Configuration& config);
bool groupName(const char* name);
bool sameName(const char* a, const char* b);
UnitMask membersMask(uint8_t count);
uint8_t population(UnitMask mask);
int8_t groupFor(const Configuration& config, uint8_t slot);
// Legacy totals / PSU offline defaults only. Online settings are per PSU.
// Deterministic rounding: the first remainder members receive one extra centiamp.
uint16_t allocation(UnitMask members, uint16_t total, uint8_t slot);
uint16_t groupCurrent(const Configuration& config, uint8_t group);
UnitMask scopeMembers(const Configuration& config, SettingScope scope);
SettingSummary settingSummary(const Configuration& config, SettingScope scope, SettingFields field);
bool allowedCurrent(uint16_t current, uint16_t rating);
uint16_t currentMaximum(uint16_t rating);
}
