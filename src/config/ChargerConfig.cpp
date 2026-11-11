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
UnitMask membersMask(uint8_t n) { return n <= PSU_MAX_UNITS ? UnitMask((1U << n) - 1) : 0; }
uint8_t population(UnitMask m) { uint8_t n = 0; while (m) { n += m & 1; m >>= 1; } return n; }
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
uint16_t currentMaximum(uint16_t rating) {
  const uint32_t rated = uint32_t(rating) * 120 / 100;
  return rated < limits::maxCurrent ? rated : limits::maxCurrent;
}
bool allowedCurrent(uint16_t c, uint16_t r) {
  return r >= 100 && c <= currentMaximum(r);
}
uint16_t allocation(UnitMask members, uint16_t total, uint8_t slot) {
  if (slot >= PSU_MAX_UNITS || !(members & (1U << slot))) return 0;
  const uint8_t n = population(members), rank = population(members & UnitMask((1U << slot) - 1));
  return total / n + (rank < total % n);
}
uint16_t groupCurrent(const Configuration& c, uint8_t g) {
  uint16_t total = 0;
  if (g < limits::maxGroups) for (uint8_t i = 0; i < c.count; ++i)
    if (c.groups[g].members & (1U << i)) total += c.current[i];
  return total;
}
UnitMask scopeMembers(const Configuration& c, SettingScope s) {
  if (s.kind == ScopeKind::Global) return membersMask(c.count);
  if (s.kind == ScopeKind::Group) return s.index < limits::maxGroups ? c.groups[s.index].members : 0;
  return s.index < c.count ? UnitMask(1U << s.index) : 0;
}
SettingSummary settingSummary(const Configuration& c, SettingScope s, SettingFields field) {
  SettingSummary r = {UINT16_MAX, 0, false};
  const UnitMask mask = scopeMembers(c, s);
  if (field == SettingFields::Current && s.kind != ScopeKind::Unit) {
    for (uint8_t g = 0; g < limits::maxGroups; ++g) if (c.groups[g].members & mask) {
      const auto total = groupCurrent(c, g);
      if (total < r.minimum) r.minimum = total;
      if (total > r.maximum) r.maximum = total;
      for (uint8_t i = 0; i < c.count; ++i) if (c.groups[g].members & (1U << i))
        r.custom |= c.current[i] != allocation(c.groups[g].members, total, i);
    }
  } else for (uint8_t i = 0; i < c.count; ++i) if (mask & (1U << i)) {
    const auto value = field == SettingFields::Voltage ? c.voltage[i] : c.current[i];
    if (value < r.minimum) r.minimum = value;
    if (value > r.maximum) r.maximum = value;
  }
  if (r.minimum == UINT16_MAX) r.minimum = 0;
  return r;
}
int8_t groupFor(const Configuration& c, uint8_t slot) {
  for (uint8_t i = 0; i < limits::maxGroups; ++i) if (c.groups[i].members & (1U << slot)) return i;
  return -1;
}
void defaultConfiguration(Configuration& c) {
  c = {};
  c.deploymentId = deployment::id;
  c.offlineVoltage = deployment::offlineVoltage; c.pollMs = deployment::pollMs;
  c.count = deployment::initialSlots;
  PSU_COPY_CONSTANT(c.units, deployment::units, sizeof(c.units));
  PSU_COPY_CONSTANT(c.groups, deployment::groups, sizeof(c.groups));
  for (uint8_t i = 0; i < PSU_MAX_UNITS; ++i) {
    if (!c.units[i].ratedCurrent) c.units[i].ratedCurrent = limits::defaultRatedCurrent;
    c.voltage[i] = deployment::voltage;
  }
}
Configuration defaultConfiguration() { Configuration c; defaultConfiguration(c); return c; }
bool validConfig(const Configuration& c) {
  if (c.thermalStop > ThermalStop::Duration || (c.thermalStop != ThermalStop::None && c.autoResume)) return false;
  if (!c.deploymentId || !c.count || c.count > PSU_MAX_UNITS ||
      c.pollMs < limits::minPollMs || c.pollMs > limits::maxPollMs ||
      c.offlineVoltage < limits::minOfflineVoltage || c.offlineVoltage > limits::maxVoltage ||
      (c.operating.currentMask & ~membersMask(c.count)) ||
      (c.acceptedAbsent & ~membersMask(c.count)) ||
      (c.disabled & ~membersMask(c.count)) ||
      (c.operating.voltageMask & ~membersMask(c.count)) ||
      (c.operating.currentMask & ~c.operating.voltageMask)) return false;
  UnitMask assigned = 0;
  for (uint8_t i = 0; i < limits::maxGroups; ++i) {
    const auto& g = c.groups[i];
    if (!g.name[0]) {
      if (g.members || g.offlineCurrent) return false;
      for (uint8_t j = 1; j <= limits::groupNameLength; ++j) if (g.name[j]) return false;
      continue;
    }
    if (!groupName(g.name) || !g.members || (g.members & ~membersMask(c.count)) || (assigned & g.members)) return false;
    for (uint8_t j = 0; j < i; ++j) if (sameName(g.name, c.groups[j].name)) return false;
    assigned |= g.members;
    for (uint8_t j = 0; j < c.count; ++j) if (g.members & (1U << j))
      if (!allowedCurrent(allocation(g.members, g.offlineCurrent, j), c.units[j].ratedCurrent)) return false;
  }
  for (uint8_t i = 0; i < PSU_MAX_UNITS; ++i) {
    const auto& u = c.units[i];
    if (((c.acceptedAbsent | c.disabled) & (1U << i)) && (!identified(u.identity) || !(assigned & (1U << i)))) return false;
    if (u.ratedCurrent < 100 || c.voltage[i] < limits::minVoltage || c.voltage[i] > limits::maxVoltage ||
        !allowedCurrent(c.current[i], u.ratedCurrent)) return false;
    if ((c.operating.voltageMask & (1U << i)) && (!identified(u.identity) ||
        c.operating.voltage[i] < limits::minVoltage || c.operating.voltage[i] > limits::maxVoltage)) return false;
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
