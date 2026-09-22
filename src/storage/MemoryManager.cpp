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
bool decode(const uint8_t* b, Configuration& c) {
  c = {}; c.deploymentId = get32(b + 16); c.voltage = get16(b + 20);
  c.offlineVoltage = get16(b + 22); c.pollMs = get16(b + 24); c.count = b[26];
  if (b[27] > 1 || b[28]) return false;
  c.autoResume = b[27]; uint16_t at = 29;
  for (auto& g : c.groups) {
    memcpy(g.name, b + at, 8); g.name[8] = 0; g.members = b[at + 8];
    g.current = get16(b + at + 9); g.offlineCurrent = get16(b + at + 11); at += 13;
  }
  for (auto& u : c.units) { memcpy(u.identity.bytes, b + at, 6); u.ratedCurrent = get16(b + at + 6); at += 8; }
  c.operating.voltage = get16(b + at); at += 2;
  c.operating.currentMask = b[at++];
  // Authorization flag is encoded in header reserved byte 14 for schema 2.
  if (b[14] > 1) return false;
  c.operating.voltageAuthorized = b[14];
  c.operating.voltageMask = b[15];
  for (auto& a : c.operating.current) { a = get16(b + at); at += 2; }
  return validConfig(c);
}
void encode(const Configuration& c, uint8_t* b, uint32_t sequence) {
  b[0] = 0xa5; b[1] = 2; put16(b + 2, MemoryManager::recordSize); put32(b + 4, sequence);
  memcpy(b + 10, "R48C", 4); b[14] = c.operating.voltageAuthorized; b[15] = c.operating.voltageMask;
  put32(b + 16, c.deploymentId); put16(b + 20, c.voltage); put16(b + 22, c.offlineVoltage);
  put16(b + 24, c.pollMs); b[26] = c.count; b[27] = c.autoResume; uint16_t at = 29;
  for (const auto& g : c.groups) {
    memcpy(b + at, g.name, 8); b[at + 8] = g.members;
    put16(b + at + 9, g.current); put16(b + at + 11, g.offlineCurrent); at += 13;
  }
  for (const auto& u : c.units) { memcpy(b + at, u.identity.bytes, 6); put16(b + at + 6, u.ratedCurrent); at += 8; }
  put16(b + at, c.operating.voltage); at += 2; b[at++] = c.operating.currentMask;
  for (auto a : c.operating.current) { put16(b + at, a); at += 2; }
  put16(b + 8, crc(b, MemoryManager::recordSize));
}
bool decodeLegacy(const uint8_t* b, uint16_t size, LegacyConfiguration& c) {
  if (size != 20 + 12 * PSU_MAX_UNITS || b[14] || b[15] || b[17] > 1) return false;
  c = {}; c.count = b[16]; c.applyOnBoot = b[17]; c.pollMs = get16(b + 18);
  if (!c.count || c.count > PSU_MAX_UNITS || c.pollMs < limits::minPollMs || c.pollMs > limits::maxPollMs) return false;
  for (uint8_t i = 0; i < PSU_MAX_UNITS; ++i) {
    const auto* p = b + 20 + 12 * i; if (p[1] > 1) return false;
    c.units[i] = {p[0], bool(p[1]), get16(p + 2), get16(p + 4), get16(p + 6), get16(p + 8), get16(p + 10)};
    const auto& u = c.units[i];
    if (!u.address || u.address > 127 || u.voltage < limits::minVoltage || u.voltage > limits::maxVoltage ||
        u.offlineVoltage < limits::minOfflineVoltage || u.offlineVoltage > limits::maxVoltage ||
        !allowedCurrent(u.current, u.ratedCurrent) || !allowedCurrent(u.offlineCurrent, u.ratedCurrent)) return false;
    for (uint8_t j = 0; j < i && i < c.count; ++j) if (c.units[j].address == u.address) return false;
  }
  return true;
}
}
bool MemoryManager::readRecord(uint8_t slot, uint8_t* bytes, uint16_t& size, uint32_t& sequence) const {
  if (storage_.length() < budget) return false;
  const uint16_t base = slot * slotSize;
  for (uint8_t i = 0; i < 16; ++i) bytes[i] = storage_.read(base + i);
  size = get16(bytes + 2); sequence = get32(bytes + 4);
  if (bytes[0] != 0xa5 || (bytes[1] != 1 && bytes[1] != 2) || memcmp(bytes + 10, "R48C", 4) ||
      size > slotSize || size < 20 || (bytes[1] == 2 && size != recordSize)) return false;
  for (uint16_t i = 16; i < size; ++i) bytes[i] = storage_.read(base + i);
  if (crc(bytes, size) != get16(bytes + 8)) return false;
  if (bytes[1] == 2) { Configuration c; return decode(bytes, c); }
  LegacyConfiguration old; return decodeLegacy(bytes, size, old);
}
StorageResult MemoryManager::load(Configuration& c) const {
  if (storage_.length() < budget) return StorageResult::TooSmall;
  uint8_t b[slotSize]; uint16_t size = 0; uint32_t seq = 0, best = 0; int8_t slot = -1;
  for (uint8_t i = 0; i < 2; ++i) if (readRecord(i, b, size, seq) && (slot < 0 || newer(seq, best))) { slot = i; best = seq; }
  if (slot < 0) return StorageResult::NoValidRecord;
  readRecord(slot, b, size, seq);
  if (b[1] == 2) {
    Configuration next; decode(b, next); c = next;
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
StorageResult MemoryManager::startSave(const Configuration& c) {
  if (saving_) return StorageResult::Busy;
  if (storage_.length() < budget) return StorageResult::TooSmall;
  if (!validConfig(c)) return StorageResult::InvalidConfig;
  if (c.deploymentId != deployment::id) return StorageResult::WrongDeployment;
  uint8_t b[slotSize]; uint16_t size = 0; uint32_t seq = 0, best = 0; int8_t slot = -1;
  for (uint8_t i = 0; i < 2; ++i) if (readRecord(i, b, size, seq) && (slot < 0 || newer(seq, best))) { slot = i; best = seq; }
  saveBase_ = slot < 0 ? 0 : uint16_t(1 - slot) * slotSize;
  saveSequence_ = slot < 0 ? 1 : best + 1;
  memset(pending_, 0, sizeof(pending_)); encode(c, pending_, saveSequence_);
  saveAt_ = 0; saving_ = true; saveResult_ = StorageResult::Busy;
  return StorageResult::Ok;
}
void MemoryManager::stepSave() {
  if (!saving_) return;
  if (saveAt_ == 0) storage_.update(saveBase_, 0);
  else if (saveAt_ < recordSize) storage_.update(saveBase_ + saveAt_, pending_[saveAt_]);
  else if (saveAt_ == recordSize) storage_.update(saveBase_, 0xa5);
  else {
    uint8_t b[slotSize]; uint16_t size = 0; uint32_t seq = 0;
    saveResult_ = readRecord(saveBase_ / slotSize, b, size, seq) && seq == saveSequence_
        ? StorageResult::Ok : StorageResult::WriteFailed;
    saving_ = false;
  }
  ++saveAt_;
}
}
