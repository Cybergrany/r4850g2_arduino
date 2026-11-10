#include "MemoryManager.h"
#include "../config/Deployment.h"
#include <string.h>
namespace psu {
namespace {
uint16_t get16(const uint8_t* p) { return uint16_t(p[0]) | uint16_t(p[1]) << 8; }
uint32_t get32(const uint8_t* p) { return get16(p) | uint32_t(get16(p + 2)) << 16; }
void put16(uint8_t* p, uint16_t v) { p[0] = uint8_t(v); p[1] = uint8_t(v >> 8); }
void put32(uint8_t* p, uint32_t v) { put16(p, uint16_t(v)); put16(p + 2, uint16_t(v >> 16)); }
uint16_t crc(const uint8_t* b, uint16_t size) {
  uint16_t c = 0xffff;
  for (uint16_t i = 1; i < size; ++i) {
    if (i == 8 || i == 9) continue;
    c ^= uint16_t(b[i]) << 8;
    for (uint8_t bit = 0; bit < 8; ++bit) c = c & 0x8000 ? (c << 1) ^ 0x1021 : c << 1;
  }
  return c;
}
bool newer(uint32_t a, uint32_t b) { return int32_t(a - b) > 0; }
// Both alphabet cases survive a save. Code 0 terminates a name; all other
// permitted characters fit in the remaining 63 codes.
uint8_t nameCode(char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A' + 1;
  if (c >= 'a' && c <= 'z') return c - 'a' + 27;
  if (c >= '0' && c <= '9') return c - '0' + 53;
  return c == '_' ? 63 : 0;
}
char nameChar(uint8_t c) {
  if (!c) return 0;
  if (c <= 26) return 'A' + c - 1;
  if (c <= 52) return 'a' + c - 27;
  if (c <= 62) return '0' + c - 53;
  return '_';
}
void packName(const char* name, uint8_t* bytes) {
  bool ended = false;
  for (uint8_t part = 0; part < 2; ++part) {
    uint32_t packed = 0;
    for (uint8_t i = 0; i < 4; ++i) {
      const char c = name[part * 4 + i];
      ended |= !c;
      packed |= uint32_t(ended ? 0 : nameCode(c)) << (i * 6);
    }
    for (uint8_t i = 0; i < 3; ++i) bytes[part * 3 + i] = uint8_t(packed >> (i * 8));
  }
}
void unpackName(const uint8_t* bytes, char* name) {
  for (uint8_t part = 0; part < 2; ++part) {
    const uint32_t packed = uint32_t(bytes[part * 3]) | uint32_t(bytes[part * 3 + 1]) << 8 |
        uint32_t(bytes[part * 3 + 2]) << 16;
    for (uint8_t i = 0; i < 4; ++i) name[part * 4 + i] = nameChar((packed >> (i * 6)) & 63);
  }
  name[8] = 0;
}
// Record length, rather than today's compiled capacity, describes old layouts.
// This also permits migration from earlier builds compiled with fewer slots.
uint8_t storedUnits(const uint8_t* b, uint16_t size) {
  if (b[1] == 1) {
    if (size < 32 || (size - 20) % 12) return 0;
    const uint16_t n = (size - 20) / 12;
    return n <= 8 ? uint8_t(n) : 0;
  }
  if (b[1] <= 4) {
    constexpr uint16_t fixed = 16 + 13 + 13 * limits::maxGroups + 3;
    if (size < fixed + 10 || (size - fixed) % 10) return 0;
    const uint16_t n = (size - fixed) / 10;
    return n <= 8 ? uint8_t(n) : 0;
  }
  const uint8_t n = b[15];
  return n >= 1 && n <= 9 && size == uint16_t(36 + 10 * limits::maxGroups + 10 * n + (n + 1) / 2) ? n : 0;
}
bool decode(const uint8_t* b, uint16_t size, Configuration& c) {
  const uint8_t capacity = storedUnits(b, size);
  if (!capacity || capacity > PSU_MAX_UNITS || b[26] > capacity) return false;
  c = {}; c.deploymentId = get32(b + 16); c.voltage = get16(b + 20);
  c.offlineVoltage = get16(b + 22); c.pollMs = get16(b + 24); c.count = b[26];
  for (auto& u : c.units) u.ratedCurrent = limits::defaultRatedCurrent;
  if ((b[1] < 4 ? b[27] > 1 : (b[27] & ~7)) || (b[1] == 2 && b[28]) || b[14] > 1) return false;
  c.autoResume = b[27] & 1;
  c.thermalStop = b[1] >= 4 ? ThermalStop(b[27] >> 1) : ThermalStop::None;
  c.operating.voltageAuthorized = b[14];
  if (b[1] < 5) {
    c.acceptedAbsent = b[1] >= 3 ? b[28] : 0;
    c.operating.voltageMask = b[15];
    uint16_t at = 29;
    for (auto& g : c.groups) {
      memcpy(g.name, b + at, 8); g.name[8] = 0; g.members = b[at + 8];
      g.current = get16(b + at + 9); g.offlineCurrent = get16(b + at + 11); at += 13;
    }
    for (uint8_t i = 0; i < capacity; ++i) {
      auto& u = c.units[i]; memcpy(u.identity.bytes, b + at, 6); u.ratedCurrent = get16(b + at + 6); at += 8;
    }
    c.operating.voltage = get16(b + at); at += 2;
    c.operating.currentMask = b[at++];
    for (uint8_t i = 0; i < capacity; ++i) { c.operating.current[i] = get16(b + at); at += 2; }
  } else {
    c.acceptedAbsent = get16(b + 28);
    c.operating.voltageMask = get16(b + 30); c.operating.currentMask = get16(b + 32);
    c.operating.voltage = get16(b + 34);
    uint16_t at = 36;
    for (auto& g : c.groups) {
      unpackName(b + at, g.name); g.current = get16(b + at + 6); g.offlineCurrent = get16(b + at + 8); at += 10;
    }
    for (uint8_t i = 0; i < capacity; ++i) {
      auto& u = c.units[i]; memcpy(u.identity.bytes, b + at, 6); u.ratedCurrent = get16(b + at + 6);
      c.operating.current[i] = get16(b + at + 8); at += 10;
    }
    for (uint8_t i = 0; i < capacity; ++i) {
      const uint8_t group = (b[at + i / 2] >> ((i % 2) * 4)) & 15;
      if (group > limits::maxGroups) return false;
      if (group) c.groups[group - 1].members |= UnitMask(1) << i;
    }
    if ((capacity % 2) && (b[at + capacity / 2] & 0xf0)) return false;
  }
  return validConfig(c);
}
void encode(const Configuration& c, uint8_t* b, uint32_t sequence) {
  b[0] = 0xa5; b[1] = 5; put16(b + 2, MemoryManager::recordSize); put32(b + 4, sequence);
  memcpy(b + 10, "R48C", 4); b[14] = c.operating.voltageAuthorized; b[15] = PSU_MAX_UNITS;
  put32(b + 16, c.deploymentId); put16(b + 20, c.voltage); put16(b + 22, c.offlineVoltage);
  put16(b + 24, c.pollMs); b[26] = c.count; b[27] = uint8_t(c.autoResume) | uint8_t(c.thermalStop) << 1;
  put16(b + 28, c.acceptedAbsent); put16(b + 30, c.operating.voltageMask);
  put16(b + 32, c.operating.currentMask); put16(b + 34, c.operating.voltage);
  uint16_t at = 36;
  for (const auto& g : c.groups) {
    packName(g.name, b + at); put16(b + at + 6, g.current); put16(b + at + 8, g.offlineCurrent); at += 10;
  }
  for (uint8_t i = 0; i < PSU_MAX_UNITS; ++i) {
    const auto& u = c.units[i]; memcpy(b + at, u.identity.bytes, 6); put16(b + at + 6, u.ratedCurrent);
    put16(b + at + 8, c.operating.current[i]); at += 10;
  }
  // Groups never overlap; zero means unassigned, 1..8 selects a group.
  for (uint8_t i = 0; i < PSU_MAX_UNITS; ++i)
    b[at + i / 2] |= uint8_t(groupFor(c, i) + 1) << ((i % 2) * 4);
  put16(b + 8, crc(b, MemoryManager::recordSize));
}
bool decodeLegacy(const uint8_t* b, uint16_t size, LegacyConfiguration& c) {
  const uint8_t capacity = storedUnits(b, size);
  if (!capacity || capacity > PSU_MAX_UNITS || b[14] || b[15] || b[17] > 1) return false;
  c = {}; c.count = b[16]; c.applyOnBoot = b[17]; c.pollMs = get16(b + 18);
  if (!c.count || c.count > capacity || c.pollMs < limits::minPollMs || c.pollMs > limits::maxPollMs) return false;
  for (uint8_t i = 0; i < capacity; ++i) {
    const auto* p = b + 20 + 12 * i; if (p[1] > 1) return false;
    c.units[i] = {p[0], bool(p[1]), get16(p + 2), get16(p + 4), get16(p + 6), get16(p + 8), get16(p + 10)};
    const auto& u = c.units[i];
    if (!u.address || u.address > 127 || u.voltage < limits::minVoltage || u.voltage > limits::maxVoltage ||
        u.offlineVoltage < limits::minOfflineVoltage || u.offlineVoltage > limits::maxVoltage ||
        !allowedCurrent(u.current, u.ratedCurrent) || !allowedCurrent(u.offlineCurrent, u.ratedCurrent)) return false;
    for (uint8_t j = 0; j < i && i < c.count; ++j) if (c.units[j].address == u.address) return false;
  }
  for (uint8_t i = capacity; i < PSU_MAX_UNITS; ++i) c.units[i].ratedCurrent = limits::defaultRatedCurrent;
  return true;
}
}
bool MemoryManager::readRecord(uint8_t slot, uint8_t* bytes, uint16_t& size, uint32_t& sequence) const {
  if (storage_.length() < budget) return false;
  const uint16_t base = slot * slotSize;
  for (uint8_t i = 0; i < 16; ++i) bytes[i] = storage_.read(base + i);
  size = get16(bytes + 2); sequence = get32(bytes + 4);
  if (bytes[0] != 0xa5 || bytes[1] < 1 || bytes[1] > 5 || memcmp(bytes + 10, "R48C", 4) ||
      size > 216 || size < 20 || !storedUnits(bytes, size)) return false;
  for (uint16_t i = 16; i < size; ++i) bytes[i] = storage_.read(base + i);
  if (crc(bytes, size) != get16(bytes + 8)) return false;
  if (bytes[1] >= 2) { Configuration c; return decode(bytes, size, c); }
  LegacyConfiguration old; return decodeLegacy(bytes, size, old);
}
StorageResult MemoryManager::load(Configuration& c) const {
  if (saving() || !storage_.ready()) return StorageResult::Busy;
  if (storage_.length() < budget) return StorageResult::TooSmall;
  uint8_t b[slotSize]; uint16_t size = 0; uint32_t seq = 0, best = 0; int8_t slot = -1;
  for (uint8_t i = 0; i < 2; ++i) if (readRecord(i, b, size, seq) && (slot < 0 || newer(seq, best))) { slot = i; best = seq; }
  if (slot < 0) return StorageResult::NoValidRecord;
  readRecord(slot, b, size, seq);
  if (b[1] >= 2) {
    Configuration next; decode(b, size, next); c = next;
    return c.deploymentId == deployment::id ? StorageResult::Ok : StorageResult::WrongDeployment;
  }
  LegacyConfiguration old; decodeLegacy(b, size, old);
  // Preserve incompatible per-unit drafts in EEPROM for `legacy` inspection.
  for (uint8_t i = 0; i < old.count; ++i) if (!old.units[i].enabled ||
      old.units[i].voltage != old.units[0].voltage || old.units[i].offlineVoltage != old.units[0].offlineVoltage ||
      old.units[i].current != old.units[0].current || old.units[i].offlineCurrent != old.units[0].offlineCurrent)
    return StorageResult::LegacyNeedsReview;
  Configuration next = defaultConfiguration(); next.count = old.count; next.pollMs = old.pollMs;
  next.voltage = old.units[0].voltage; next.offlineVoltage = old.units[0].offlineVoltage;
  strcpy(next.groups[0].name, "GROUP1"); next.groups[0].members = membersMask(old.count);
  next.groups[0].current = old.units[0].current * old.count;
  next.groups[0].offlineCurrent = old.units[0].offlineCurrent * old.count;
  for (uint8_t i = 0; i < PSU_MAX_UNITS; ++i) next.units[i].ratedCurrent = old.units[i].ratedCurrent;
  if (!validConfig(next)) return StorageResult::LegacyNeedsReview;
  c = next; return StorageResult::Migrated; // Unbound, unauthorized, auto-resume off.
}
bool MemoryManager::legacy(LegacyConfiguration& c) const {
  if (saving() || !storage_.ready()) return false;
  uint8_t b[slotSize]; uint16_t size = 0; uint32_t seq = 0, best = 0; int8_t slot = -1;
  for (uint8_t i = 0; i < 2; ++i) if (readRecord(i, b, size, seq) && b[1] == 1 && (slot < 0 || newer(seq, best))) { slot = i; best = seq; }
  if (slot < 0) return false;
  readRecord(slot, b, size, seq); return decodeLegacy(b, size, c);
}
StorageResult MemoryManager::save(const Configuration& c) {
  const auto result = startSave(c);
  if (result != StorageResult::Ok) return result;
  while (saving_) stepSave();
  return saveResult_;
}
StorageResult MemoryManager::startProtectionSave(ThermalStop stop) {
  Configuration saved = defaultConfiguration();
  const auto r = load(saved);
  if (r != StorageResult::Ok && r != StorageResult::NoValidRecord) return r;
  saved.autoResume = false; saved.thermalStop = stop;
  return startSave(saved);
}
StorageResult MemoryManager::startSave(const Configuration& c) {
  if (saving() || !storage_.ready()) return StorageResult::Busy;
  if (storage_.length() < budget) return StorageResult::TooSmall;
  if (!validConfig(c)) return StorageResult::InvalidConfig;
  if (c.deploymentId != deployment::id) return StorageResult::WrongDeployment;
  uint8_t b[slotSize]; uint16_t size = 0; uint32_t seq = 0, best = 0; int8_t slot = -1;
  for (uint8_t i = 0; i < 2; ++i) if (readRecord(i, b, size, seq) && (slot < 0 || newer(seq, best))) { slot = i; best = seq; }
  saveBase_ = slot < 0 ? 0 : uint16_t(1 - slot) * slotSize;
  saveSequence_ = slot < 0 ? 1 : best + 1;
  memset(pending_, 0, sizeof(pending_)); encode(c, pending_, saveSequence_);
  saveAt_ = 0; saving_ = true; saveResult_ = StorageResult::Busy;
  if (++saveToken_ == 0) ++saveToken_;
  return StorageResult::Ok;
}
bool MemoryManager::reviseProtection(uint16_t token, ThermalStop stop) {
  if (!saving_ || token != saveToken_ || stop == ThermalStop::None) return false;
  pending_[27] = uint8_t(stop) << 1; put16(pending_ + 8, crc(pending_, recordSize));
  saveAt_ = 0; // Invalidate/rewrite this destination; the previous journal survives.
  return true;
}
void MemoryManager::stepSave() {
  if (!saving_ || !storage_.ready()) return;
  if (saveAt_ == 0) storage_.update(saveBase_, 0);
  else if (saveAt_ < recordSize) storage_.update(saveBase_ + saveAt_, pending_[saveAt_]);
  else if (saveAt_ == recordSize) storage_.update(saveBase_, 0xa5);
  else {
    uint8_t b[slotSize]; uint16_t size = 0; uint32_t seq = 0;
    saveResult_ = readRecord(saveBase_ / slotSize, b, size, seq) && seq == saveSequence_
        ? StorageResult::Ok : StorageResult::WriteFailed;
    saving_ = false;
    completedToken_ = saveToken_; completedResult_ = saveResult_;
  }
  ++saveAt_;
}
}
