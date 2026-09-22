#pragma once
#include <stdint.h>
#include "BuildOptions.h"

namespace psu {
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
// Space read requests while giving eight units enough slots for DATA and INFO.
constexpr uint16_t readRequestGapMs = 25;
}
struct Identity { uint8_t bytes[6]; };
bool identified(const Identity& id);
bool sameIdentity(const Identity& a, const Identity& b);
struct UnitConfig { Identity identity; uint16_t ratedCurrent; };
struct GroupConfig {
  char name[limits::groupNameLength + 1];
  uint8_t members; // Stable, global controller slots. Groups never overlap.
  uint16_t current, offlineCurrent; // TOTAL group centiamps, split on staging.
};
// Separate from drafts: only an explicitly queued online apply authorizes these.
struct OperatingProfile {
  uint16_t voltage;
  uint16_t current[PSU_MAX_UNITS];
  uint8_t currentMask, voltageMask;
  bool voltageAuthorized;
};
struct Configuration {
  uint32_t deploymentId;
  uint16_t voltage, offlineVoltage, pollMs;
  uint8_t count;
  bool autoResume;
  UnitConfig units[PSU_MAX_UNITS];
  GroupConfig groups[limits::maxGroups];
  OperatingProfile operating;
};
Configuration defaultConfiguration();
bool validConfig(const Configuration& config);
bool groupName(const char* name);
bool sameName(const char* a, const char* b);
uint8_t membersMask(uint8_t count);
uint8_t population(uint8_t mask);
int8_t groupFor(const Configuration& config, uint8_t slot);
// Deterministic rounding: the first remainder members receive one extra centiamp.
uint16_t allocation(const GroupConfig& group, uint8_t slot, bool offline = false);
bool allowedCurrent(uint16_t current, uint16_t rating);
uint16_t currentMaximum(uint16_t rating);
}
