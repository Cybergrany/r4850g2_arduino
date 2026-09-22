#include "ChargerConfig.h"
#include "Deployment.h"
#include <string.h>
namespace psu {
bool identified(const Identity& id) {
  uint8_t any = 0, all = 0xff;
  for (auto b : id.bytes) { any |= b; all &= b; }
  return any && all != 0xff;
}
bool sameIdentity(const Identity& a, const Identity& b) { return !memcmp(a.bytes, b.bytes, 6); }
uint8_t membersMask(uint8_t n) { return n <= PSU_MAX_UNITS ? uint8_t((1U << n) - 1) : 0; }
uint8_t population(uint8_t m) { uint8_t n = 0; while (m) { n += m & 1; m >>= 1; } return n; }
namespace { char upper(char c) { return c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c; } }
bool sameName(const char* a, const char* b) {
  for (uint8_t i = 0; i <= limits::groupNameLength; ++i) {
    if (upper(a[i]) != upper(b[i])) return false;
    if (!a[i]) return true;
  }
  return false;
}
bool groupName(const char* s) {
  if (!s || upper(*s) < 'A' || upper(*s) > 'Z') return false;
  uint8_t n = 0;
  for (; n <= limits::groupNameLength && s[n]; ++n) {
    char c = upper(s[n]);
    if (!(c >= 'A' && c <= 'Z') && !(c >= '0' && c <= '9') && c != '_') return false;
  }
  return n <= limits::groupNameLength && !sameName(s, "all") && !sameName(s, "voltage") &&
      !sameName(s, "current") && !sameName(s, "bus");
}
bool allowedCurrent(uint16_t c, uint16_t r) {
  return r >= 100 && c <= limits::maxCurrent && uint32_t(c) * 100 <= uint32_t(r) * 120;
}
uint16_t allocation(const GroupConfig& g, uint8_t slot, bool offline) {
  if (slot >= PSU_MAX_UNITS || !(g.members & (1U << slot))) return 0;
  const uint8_t n = population(g.members), rank = population(g.members & uint8_t((1U << slot) - 1));
  const uint16_t total = offline ? g.offlineCurrent : g.current;
  return total / n + (rank < total % n);
}
int8_t groupFor(const Configuration& c, uint8_t slot) {
  for (uint8_t i = 0; i < limits::maxGroups; ++i) if (c.groups[i].members & (1U << slot)) return i;
  return -1;
}
Configuration defaultConfiguration() {
  Configuration c = {};
  c.deploymentId = deployment::id; c.voltage = deployment::voltage;
  c.offlineVoltage = deployment::offlineVoltage; c.pollMs = deployment::pollMs;
  c.count = deployment::initialSlots;
  for (uint8_t i = 0; i < PSU_MAX_UNITS; ++i) {
    c.units[i] = deployment::units[i];
    if (!c.units[i].ratedCurrent) c.units[i].ratedCurrent = limits::defaultRatedCurrent;
  }
  for (uint8_t i = 0; i < limits::maxGroups; ++i) c.groups[i] = deployment::groups[i];
  return c;
}
bool validConfig(const Configuration& c) {
  if (!c.deploymentId || !c.count || c.count > PSU_MAX_UNITS ||
      c.pollMs < limits::minPollMs || c.pollMs > limits::maxPollMs ||
      c.voltage < limits::minVoltage || c.voltage > limits::maxVoltage ||
      c.offlineVoltage < limits::minOfflineVoltage || c.offlineVoltage > limits::maxVoltage ||
      (c.operating.currentMask & ~membersMask(c.count)) ||
      (c.operating.voltageMask & ~membersMask(c.count)) ||
      (c.operating.currentMask & ~c.operating.voltageMask)) return false;
  if (c.operating.voltageAuthorized && (c.operating.voltage < limits::minVoltage ||
      c.operating.voltage > limits::maxVoltage)) return false;
  if ((c.operating.currentMask || c.operating.voltageMask) && !c.operating.voltageAuthorized) return false;
  uint8_t assigned = 0;
  for (uint8_t i = 0; i < limits::maxGroups; ++i) {
    const auto& g = c.groups[i];
    if (!g.name[0]) {
      if (g.members || g.current || g.offlineCurrent) return false;
      for (uint8_t j = 1; j <= limits::groupNameLength; ++j) if (g.name[j]) return false;
      continue;
    }
    if (!groupName(g.name) || !g.members || (g.members & ~membersMask(c.count)) || (assigned & g.members)) return false;
    for (uint8_t j = 0; j < i; ++j) if (sameName(g.name, c.groups[j].name)) return false;
    assigned |= g.members;
    for (uint8_t j = 0; j < c.count; ++j) if (g.members & (1U << j))
      if (!allowedCurrent(allocation(g, j), c.units[j].ratedCurrent) ||
          !allowedCurrent(allocation(g, j, true), c.units[j].ratedCurrent)) return false;
  }
  for (uint8_t i = 0; i < PSU_MAX_UNITS; ++i) {
    const auto& u = c.units[i];
    if (u.ratedCurrent < 100) return false;
    if ((c.operating.voltageMask & (1U << i)) && !identified(u.identity)) return false;
    for (uint8_t j = 0; j < i; ++j)
      if (identified(u.identity) && sameIdentity(u.identity, c.units[j].identity)) return false;
    if (c.operating.currentMask & (1U << i)) {
      if (!identified(u.identity) || !(assigned & (1U << i)) ||
          !allowedCurrent(c.operating.current[i], u.ratedCurrent)) return false;
    }
  }
  return true;
}
}
