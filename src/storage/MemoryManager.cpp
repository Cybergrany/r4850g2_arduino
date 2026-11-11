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
  if (b[1] == 6) {
    const uint8_t n = b[14];
    return n >= 1 && n <= 9 && size == uint16_t(34 + 8 * limits::maxGroups + 16 * n + (n + 1) / 2) ? n : 0;
  }
  const uint8_t n = b[15];
  return n >= 1 && n <= 9 && size == uint16_t(36 + 10 * limits::maxGroups + 10 * n + (n + 1) / 2) ? n : 0;
}
bool decode(const uint8_t* b, uint16_t size, Configuration& c) {
  const uint8_t capacity = storedUnits(b, size);
  if (!capacity || capacity > PSU_MAX_UNITS || (b[1] == 6 ? b[15] : b[26]) > capacity) return false;
  c = {}; c.deploymentId = get32(b + 16);
  for (uint8_t i = 0; i < PSU_MAX_UNITS; ++i) { c.units[i].ratedCurrent = limits::defaultRatedCurrent; c.voltage[i] = deployment::voltage; }
  uint16_t at;
  if (b[1] == 6) {
    c.count = b[15]; c.offlineVoltage = get16(b + 20); c.pollMs = get16(b + 22);
    if ((b[24] & ~7) || b[25]) return false;
    c.autoResume = b[24] & 1; c.thermalStop = ThermalStop(b[24] >> 1);
    c.acceptedAbsent = get16(b + 26); c.operating.voltageMask = get16(b + 28); c.operating.currentMask = get16(b + 30);
    c.disabled = get16(b + 32); at = 34;
    for (auto& g : c.groups) { unpackName(b + at, g.name); g.offlineCurrent = get16(b + at + 6); at += 8; }
    for (uint8_t i = 0; i < capacity; ++i) {
      memcpy(c.units[i].identity.bytes, b + at, 6); c.units[i].ratedCurrent = get16(b + at + 6);
      c.voltage[i] = get16(b + at + 8); c.current[i] = get16(b + at + 10);
      c.operating.voltage[i] = get16(b + at + 12); c.operating.current[i] = get16(b + at + 14); at += 16;
    }
  } else {
    c.count = b[26]; c.offlineVoltage = get16(b + 22); c.pollMs = get16(b + 24);
    if ((b[1] < 4 ? b[27] > 1 : (b[27] & ~7)) || (b[1] == 2 && b[28]) || b[14] > 1) return false;
    c.autoResume = b[27] & 1; c.thermalStop = b[1] >= 4 ? ThermalStop(b[27] >> 1) : ThermalStop::None;
    uint16_t totals[limits::maxGroups] = {}, authorized = 0;
    if (b[1] < 5) {
      c.acceptedAbsent = b[1] >= 3 ? b[28] : 0; c.operating.voltageMask = b[15]; at = 29;
      for (uint8_t g = 0; g < limits::maxGroups; ++g) {
        memcpy(c.groups[g].name, b + at, 8); c.groups[g].name[8] = 0; c.groups[g].members = b[at + 8];
        totals[g] = get16(b + at + 9); c.groups[g].offlineCurrent = get16(b + at + 11); at += 13;
      }
      for (uint8_t i = 0; i < capacity; ++i) { memcpy(c.units[i].identity.bytes, b + at, 6); c.units[i].ratedCurrent = get16(b + at + 6); at += 8; }
      authorized = get16(b + at); at += 2; c.operating.currentMask = b[at++];
      for (uint8_t i = 0; i < capacity; ++i) { c.operating.current[i] = get16(b + at); at += 2; }
    } else {
      c.acceptedAbsent = get16(b + 28); c.operating.voltageMask = get16(b + 30); c.operating.currentMask = get16(b + 32);
      authorized = get16(b + 34); at = 36;
      for (uint8_t g = 0; g < limits::maxGroups; ++g) {
        unpackName(b + at, c.groups[g].name); totals[g] = get16(b + at + 6); c.groups[g].offlineCurrent = get16(b + at + 8); at += 10;
      }
      for (uint8_t i = 0; i < capacity; ++i) {
        memcpy(c.units[i].identity.bytes, b + at, 6); c.units[i].ratedCurrent = get16(b + at + 6);
        c.operating.current[i] = get16(b + at + 8); at += 10;
      }
      for (uint8_t i = 0; i < capacity; ++i) {
        const uint8_t g = (b[at + i / 2] >> ((i % 2) * 4)) & 15;
        if (g > limits::maxGroups) return false;
        if (g) c.groups[g - 1].members |= UnitMask(1) << i;
      }
      if ((capacity % 2) && (b[at + capacity / 2] & 0xf0)) return false;
    }
    if (!b[14] && (c.operating.voltageMask || c.operating.currentMask)) return false;
    for (uint8_t i = 0; i < capacity; ++i) {
      c.voltage[i] = get16(b + 20); c.operating.voltage[i] = authorized;
      const int8_t g = groupFor(c, i); if (g >= 0) c.current[i] = allocation(c.groups[g].members, totals[g], i);
    }
    return validConfig(c);
  }
  for (uint8_t i = 0; i < capacity; ++i) {
    const uint8_t g = (b[at + i / 2] >> ((i % 2) * 4)) & 15;
    if (g > limits::maxGroups) return false;
    if (g) c.groups[g - 1].members |= UnitMask(1) << i;
  }
  if ((capacity % 2) && (b[at + capacity / 2] & 0xf0)) return false;
  return validConfig(c);
}
void encode(const Configuration& c, uint8_t* b, uint32_t sequence) {
  memset(b, 0, MemoryManager::recordSize);
  b[0] = 0xa5; b[1] = 6; put16(b + 2, MemoryManager::recordSize); put32(b + 4, sequence);
  memcpy(b + 10, "R48C", 4); b[14] = PSU_MAX_UNITS; b[15] = c.count;
  put32(b + 16, c.deploymentId); put16(b + 20, c.offlineVoltage); put16(b + 22, c.pollMs);
  b[24] = uint8_t(c.autoResume) | uint8_t(c.thermalStop) << 1;
  put16(b + 26, c.acceptedAbsent); put16(b + 28, c.operating.voltageMask); put16(b + 30, c.operating.currentMask);
  put16(b + 32, c.disabled); uint16_t at = 34;
  for (const auto& g : c.groups) { packName(g.name, b + at); put16(b + at + 6, g.offlineCurrent); at += 8; }
  for (uint8_t i = 0; i < PSU_MAX_UNITS; ++i) {
    memcpy(b + at, c.units[i].identity.bytes, 6); put16(b + at + 6, c.units[i].ratedCurrent);
    put16(b + at + 8, c.voltage[i]); put16(b + at + 10, c.current[i]);
    put16(b + at + 12, c.operating.voltage[i]); put16(b + at + 14, c.operating.current[i]); at += 16;
  }
  for (uint8_t i = 0; i < PSU_MAX_UNITS; ++i) b[at + i / 2] |= uint8_t(groupFor(c, i) + 1) << ((i % 2) * 4);
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
// Keep mutually exclusive validation frames off the record-selection stack.
__attribute__((noinline)) bool validCurrentRecord(const uint8_t* b, uint16_t size) {
  Configuration candidate; return decode(b, size, candidate);
}
__attribute__((noinline)) bool validLegacyRecord(const uint8_t* b, uint16_t size) {
  LegacyConfiguration candidate; return decodeLegacy(b, size, candidate);
}

}
bool MemoryManager::readRecord(uint16_t address, uint16_t& size, uint32_t& sequence) const {
  if (storage_.length() < budget) return false;
  auto* b = pending_;
  for (uint8_t i = 0; i < 16; ++i) b[i] = storage_.read(address + i);
  size = get16(b + 2); sequence = get32(b + 4);
  if (b[0] != 0xa5 || b[1] < 1 || b[1] > 6 || memcmp(b + 10, "R48C", 4) ||
      size > sizeof(pending_) || size < 20 || !storedUnits(b, size) || uint32_t(address) + size > storage_.length()) return false;
  for (uint16_t i = 16; i < size; ++i) b[i] = storage_.read(address + i);
  return crc(b, size) == get16(b + 8);
}
int16_t MemoryManager::newestConfig(Configuration* scratch) const {
  uint16_t size; uint32_t seq, best = 0; int16_t found = -1;
  // One candidate only. Record selection no longer nests Configuration copies.
  for (uint8_t pass = 0; pass < 2; ++pass) {
    for (uint8_t i = 0; i < 2; ++i) {
      const uint16_t at = pass ? 256 * i : base + slotSize * i;
      if (!readRecord(at, size, seq) || (pass ? pending_[1] > 5 : pending_[1] != 6)) continue;
      const bool valid = pending_[1] >= 2 ?
          (scratch ? decode(pending_, size, *scratch) : validCurrentRecord(pending_, size)) : validLegacyRecord(pending_, size);
      if (valid && (found < 0 || newer(seq, best))) { found = at; best = seq; }
    }
    if (found >= 0) break;
  }
  return found;
}
StorageResult MemoryManager::load(Configuration& c) const {
  return loadImpl(c, false);
}
StorageResult MemoryManager::loadCandidate(Configuration& c) const {
  return loadImpl(c, true);
}
StorageResult MemoryManager::loadImpl(Configuration& c, bool temporary, int16_t* address) const {
  if (saving() || !storage_.ready()) return StorageResult::Busy;
  if (storage_.length() < budget) return StorageResult::TooSmall;
  const auto at = newestConfig(temporary ? &c : nullptr);
  if (address) *address = at;
  if (at < 0) return StorageResult::NoValidRecord;
  uint16_t size; uint32_t seq; readRecord(at, size, seq);
  if (pending_[1] >= 2) {
    // The selected record is already fully validated; decode directly to caller.
    decode(pending_, size, c);
    return c.deploymentId == deployment::id ? StorageResult::Ok : StorageResult::WrongDeployment;
  }
  return loadLegacy(c, size);
}
StorageResult MemoryManager::loadLegacy(Configuration& c, uint16_t size) const {
  LegacyConfiguration old; decodeLegacy(pending_, size, old);
  for (uint8_t i = 0; i < old.count; ++i) if (!old.units[i].enabled || old.units[i].voltage != old.units[0].voltage ||
      old.units[i].current != old.units[0].current || old.units[i].offlineVoltage != old.units[0].offlineVoltage ||
      old.units[i].offlineCurrent != old.units[0].offlineCurrent) return StorageResult::LegacyNeedsReview;
  defaultConfiguration(c); c.count = old.count; c.pollMs = old.pollMs; c.offlineVoltage = old.units[0].offlineVoltage;
  strcpy(c.groups[0].name, "GROUP1"); c.groups[0].members = membersMask(old.count);
  c.groups[0].offlineCurrent = old.units[0].offlineCurrent * old.count;
  for (uint8_t i = 0; i < PSU_MAX_UNITS; ++i) {
    c.units[i].ratedCurrent = old.units[i].ratedCurrent;
    c.voltage[i] = old.units[0].voltage; c.current[i] = i < old.count ? old.units[0].current : 0;
  }
  return StorageResult::Migrated;
}
bool MemoryManager::legacy(LegacyConfiguration& c) const {
  if (saving() || !storage_.ready()) return false;
  const auto at = newestConfig(); if (at < 0) return false;
  uint16_t size; uint32_t seq;
  return readRecord(at, size, seq) && pending_[1] == 1 && decodeLegacy(pending_, size, c);
}
void MemoryManager::startWrite(uint16_t address, uint16_t length, uint32_t sequence, bool profile) {
  saveBase_ = address; saveLength_ = length; saveSequence_ = sequence; saveAt_ = 0;
  saving_ = true; profileSave_ = profile; saveResult_ = StorageResult::Busy;
  if (++saveToken_ == 0) ++saveToken_;
}
StorageResult MemoryManager::startSave(const Configuration& c) {
  if (saving() || !storage_.ready()) return StorageResult::Busy;
  if (storage_.length() < budget) return StorageResult::TooSmall;
  if (!validConfig(c)) return StorageResult::InvalidConfig;
  if (c.deploymentId != deployment::id) return StorageResult::WrongDeployment;
  const auto previous = newestConfig(); uint16_t size; uint32_t sequence = 0;
  if (previous >= 0) readRecord(previous, size, sequence);
  encode(c, pending_, sequence + 1);
  startWrite(previous == int16_t(base) ? base + slotSize : base, recordSize, sequence + 1, false);
  return StorageResult::Ok;
}
StorageResult MemoryManager::startAvailabilitySave(const Configuration& c, UnitMask accepted) {
  if (accepted & ~membersMask(c.count)) return StorageResult::InvalidConfig;
  for (uint8_t i = 0; i < c.count; ++i) if ((accepted & (1U << i)) &&
      (!identified(c.units[i].identity) || groupFor(c, i) < 0)) return StorageResult::InvalidConfig;
  const auto result = startSave(c);
  if (result == StorageResult::Ok) {
    // The writer has not yielded yet. Change only its private snapshot, never
    // the live configuration or an operator's unsaved draft.
    put16(pending_ + 26, accepted); put16(pending_ + 8, crc(pending_, recordSize));
  }
  return result;
}
StorageResult MemoryManager::save(const Configuration& c) {
  const auto r = startSave(c); if (r != StorageResult::Ok) return r;
  while (saving_) stepSave();
  return saveResult_;
}
StorageResult MemoryManager::startProtectionSave(ThermalStop stop) {
  if (saving() || !storage_.ready()) return StorageResult::Busy;
  if (storage_.length() < budget) return StorageResult::TooSmall;
  const auto previous = newestConfig(); uint16_t size; uint32_t sequence = 0;
  if (previous >= 0) {
    readRecord(previous, size, sequence);
    if (pending_[1] == 6 && size == recordSize) {
      if (get32(pending_ + 16) != deployment::id) return StorageResult::WrongDeployment;
      put32(pending_ + 4, sequence + 1); pending_[24] = uint8_t(stop) << 1;
      put16(pending_ + 8, crc(pending_, recordSize));
      startWrite(previous == int16_t(base) ? base + slotSize : base, recordSize, sequence + 1, false);
      return StorageResult::Ok;
    }
  }
  return startMigratedProtection(stop);
}
StorageResult MemoryManager::startMigratedProtection(ThermalStop stop) {
  Configuration saved; int16_t previous = -1;
  const auto r = loadImpl(saved, true, &previous);
  if (r != StorageResult::Ok && r != StorageResult::NoValidRecord) return r;
  const uint32_t sequence = previous >= 0 ? get32(pending_ + 4) : 0;
  if (r == StorageResult::NoValidRecord) defaultConfiguration(saved);
  saved.autoResume = false; saved.thermalStop = stop;
  if (!validConfig(saved)) return StorageResult::InvalidConfig;
  encode(saved, pending_, sequence + 1);
  startWrite(previous == int16_t(base) ? base + slotSize : base, recordSize, sequence + 1, false);
  return StorageResult::Ok;
}
bool MemoryManager::persistedProtection(ThermalStop stop) const {
  if (saving() || !storage_.ready()) return false;
  const auto at = newestConfig(); if (at < 0) return false;
  uint16_t size; uint32_t seq;
  if (!readRecord(at, size, seq) || pending_[1] < 2 || get32(pending_ + 16) != deployment::id) return false;
  const uint8_t flags = pending_[pending_[1] == 6 ? 24 : 27];
  return !(flags & 1) && (pending_[1] < 4 ? ThermalStop::None : ThermalStop(flags >> 1)) == stop;
}
bool MemoryManager::reviseProtection(uint16_t token, ThermalStop stop) {
  if (!saving_ || profileSave_ || token != saveToken_ || stop == ThermalStop::None) return false;
  pending_[24] = uint8_t(stop) << 1; put16(pending_ + 8, crc(pending_, recordSize)); saveAt_ = 0; return true;
}
bool MemoryManager::cancelProfileSave() {
  if (!saving_ || !profileSave_ || saveAt_ > saveLength_) return false;
  saving_ = false; saveResult_ = StorageResult::Cancelled;
  completedToken_ = saveToken_; completedResult_ = saveResult_; return true;
}
void MemoryManager::stepSave() {
  if (!saving_ || !storage_.ready()) return;
  if (saveAt_ == 0) storage_.update(saveBase_, 0);
  else if (saveAt_ < saveLength_) storage_.update(saveBase_ + saveAt_, pending_[saveAt_]);
  else if (saveAt_ == saveLength_) storage_.update(saveBase_, 0xa5);
  else if (saveAt_ == saveLength_ + 1) {
    bool ok = true;
    for (uint16_t i = 0; i < saveLength_; ++i) if (storage_.read(saveBase_ + i) != pending_[i]) { ok = false; break; }
    saveResult_ = ok ? StorageResult::Ok : StorageResult::WriteFailed;
    if (profileSave_ || !ok) { saving_ = false; completedToken_ = saveToken_; completedResult_ = saveResult_; }
  } else if (saveAt_ <= saveLength_ + 3) storage_.update(saveAt_ == saveLength_ + 2 ? 0 : 256, 0);
  else {
    if (storage_.read(0) || storage_.read(256)) saveResult_ = StorageResult::WriteFailed;
    saving_ = false; completedToken_ = saveToken_; completedResult_ = saveResult_;
  }
  ++saveAt_;
}
namespace {
uint16_t read16(const ByteStorage& s, uint16_t at) { return uint16_t(s.read(at)) | uint16_t(s.read(at + 1)) << 8; }
uint32_t read32(const ByteStorage& s, uint16_t at) { return read16(s, at) | uint32_t(read16(s, at + 2)) << 16; }
bool validProfileName(char c) { return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ' || c == '-' || c == '_'; }
bool validProfile(const ByteStorage& s, uint16_t at) {
  if (s.read(at) != 0xa5) return false;
  uint8_t b[profiles::recordBytes];
  for (uint8_t i = 0; i < profiles::recordBytes; ++i) b[i] = s.read(at + i);
  if (b[0] != 0xa5 || b[1] != 1 || get16(b + 2) != profiles::recordBytes || memcmp(b + 10, "R48P", 4) ||
      crc(b, profiles::recordBytes) != get16(b + 8) || !get32(b + 14) || !b[18] || b[18] > PSU_MAX_UNITS ||
      !b[19] || b[19] > profiles::nameLength || b[124] > 1 || (get16(b + 122) & ~membersMask(b[18]))) return false;
  for (uint8_t i = 0; i < profiles::nameLength; ++i)
    if (i < b[19] ? !validProfileName(b[20 + i]) : b[20 + i] != 0) return false;
  for (uint8_t i = 0; i < b[18]; ++i) {
    const auto* p = b + 32 + 10 * i; Identity id; memcpy(id.bytes, p, 6);
    if (!identified(id) || get16(p + 6) < limits::minVoltage || get16(p + 6) > limits::maxVoltage || get16(p + 8) > limits::maxCurrent) return false;
    for (uint8_t j = 0; j < i; ++j) if (!memcmp(p, b + 32 + 10 * j, 6)) return false;
  }
  return true;
}
}
int16_t MemoryManager::newestProfile(uint8_t slot) const {
  if (slot >= profiles::count || storage_.length() < budget) return -1;
  int16_t found = -1; uint32_t best = 0;
  for (uint8_t i = 0; i < 2; ++i) {
    const uint16_t at = profiles::base + slot * profiles::slotBytes + i * profiles::recordBytes;
    const uint32_t seq = read32(storage_, at + 4);
    if (validProfile(storage_, at) && (found < 0 || newer(seq, best))) { found = at; best = seq; }
  }
  return found;
}
StorageResult MemoryManager::profileInfo(uint8_t slot, ProfileInfo& info) const {
  if (!storage_.ready()) return StorageResult::Busy;
  if (storage_.length() < budget) return StorageResult::TooSmall;
  const auto at = newestProfile(slot); if (at < 0) return StorageResult::NoValidRecord;
  info = {}; info.generation = read32(storage_, at + 4); info.count = storage_.read(at + 18);
  for (uint8_t i = 0; i < storage_.read(at + 19); ++i) info.name[i] = storage_.read(at + 20 + i);
  info.included = read16(storage_, at + 122); info.hasInclusion = storage_.read(at + 124);
  return read32(storage_, at + 14) == deployment::id ? StorageResult::Ok : StorageResult::WrongDeployment;
}
StorageResult MemoryManager::profileTarget(uint8_t slot, uint8_t entry, uint32_t generation, ProfileTarget& target) const {
  if (!storage_.ready()) return StorageResult::Busy;
  const auto at = newestProfile(slot); if (at < 0) return StorageResult::NoValidRecord;
  if (read32(storage_, at + 14) != deployment::id) return StorageResult::WrongDeployment;
  if (read32(storage_, at + 4) != generation) return StorageResult::Changed;
  if (entry >= storage_.read(at + 18)) return StorageResult::InvalidConfig;
  const uint16_t offset = at + 32 + 10 * entry;
  for (uint8_t i = 0; i < 6; ++i) target.identity.bytes[i] = storage_.read(offset + i);
  target.voltage = read16(storage_, offset + 6); target.current = read16(storage_, offset + 8); return StorageResult::Ok;
}
StorageResult MemoryManager::profileTargets(uint8_t slot, uint32_t generation, const Configuration& c,
    uint16_t* voltage, uint16_t* current, uint8_t& mismatch, UnitMask* included) const {
  mismatch = PSU_MAX_UNITS;
  if (!storage_.ready()) return StorageResult::Busy;
  const auto at = newestProfile(slot); if (at < 0) return StorageResult::NoValidRecord;
  if (read32(storage_, at + 14) != c.deploymentId || c.deploymentId != deployment::id) return StorageResult::WrongDeployment;
  if (read32(storage_, at + 4) != generation) return StorageResult::Changed;
  if (storage_.read(at + 18) != c.count) return StorageResult::IdentityMismatch;
  const UnitMask storedIncluded = read16(storage_, at + 122);
  UnitMask matched = 0, selected = 0;
  // This synchronous read has no yield/writer mutation between validation and
  // mapping. Validate the record once, rather than once for every member.
  for (uint8_t i = 0; i < c.count; ++i) {
    ProfileTarget target; const uint16_t offset = at + 32 + 10 * i;
    for (uint8_t k = 0; k < 6; ++k) target.identity.bytes[k] = storage_.read(offset + k);
    target.voltage = read16(storage_, offset + 6); target.current = read16(storage_, offset + 8);
    uint8_t j = 0; for (; j < c.count; ++j) if (sameIdentity(c.units[j].identity, target.identity)) break;
    if (j == c.count) { mismatch = i; return StorageResult::IdentityMismatch; }
    if (!allowedCurrent(target.current, c.units[j].ratedCurrent)) { mismatch = j; return StorageResult::InvalidConfig; }
    matched |= UnitMask(1) << j; if (storedIncluded & (1U << i)) selected |= UnitMask(1) << j;
    voltage[j] = target.voltage; current[j] = target.current;
  }
  if (included) *included = selected;
  return matched == membersMask(c.count) ? StorageResult::Ok : StorageResult::IdentityMismatch;
}
StorageResult MemoryManager::startProfileSave(uint8_t slot, const char* name, const Configuration& c, uint32_t expected,
    bool storeInclusion, UnitMask included) {
  if (saving() || !storage_.ready()) return StorageResult::Busy;
  if (storage_.length() < budget) return StorageResult::TooSmall;
  if (slot >= profiles::count || !validConfig(c)) return StorageResult::InvalidConfig;
  if (c.deploymentId != deployment::id) return StorageResult::WrongDeployment;
  for (uint8_t i = 0; i < c.count; ++i) if (!identified(c.units[i].identity)) return StorageResult::IdentityMismatch;
  const auto previous = newestProfile(slot); const uint32_t generation = previous < 0 ? 0 : read32(storage_, previous + 4);
  if (generation != expected) return StorageResult::Changed;
  char label[profiles::nameLength + 1] = {}; uint8_t n = 0;
  if (name && *name) {
    for (; name[n] && n < profiles::nameLength; ++n) {
      const char ch = name[n] >= 'a' && name[n] <= 'z' ? name[n] - 'a' + 'A' : name[n];
      if (!validProfileName(ch)) return StorageResult::InvalidConfig;
      label[n] = ch;
    }
    if (name[n]) return StorageResult::InvalidConfig;
    while (n && label[n - 1] == ' ') label[--n] = 0;
    if (!n) return StorageResult::InvalidConfig;
  } else { memcpy(label, "PROFILE ", 8); label[8] = '1' + slot; n = 9; }
  memset(pending_, 0, profiles::recordBytes); pending_[0] = 0xa5; pending_[1] = 1;
  put16(pending_ + 2, profiles::recordBytes); put32(pending_ + 4, generation + 1); memcpy(pending_ + 10, "R48P", 4);
  put32(pending_ + 14, c.deploymentId); pending_[18] = c.count; pending_[19] = n; memcpy(pending_ + 20, label, n);
  for (uint8_t i = 0; i < c.count; ++i) {
    auto* p = pending_ + 32 + 10 * i; memcpy(p, c.units[i].identity.bytes, 6); put16(p + 6, c.voltage[i]); put16(p + 8, c.current[i]);
  }
  put16(pending_ + 122, included & membersMask(c.count)); pending_[124] = storeInclusion;
  put16(pending_ + 8, crc(pending_, profiles::recordBytes));
  const uint16_t first = profiles::base + slot * profiles::slotBytes;
  startWrite(previous == int16_t(first) ? first + profiles::recordBytes : first, profiles::recordBytes, generation + 1, true);
  return StorageResult::Ok;
}
}
