#include "MemoryManager.h"

namespace psu {
namespace {
constexpr uint8_t committed = 0xa5;
constexpr uint8_t version = 1;
uint16_t get16(const uint8_t* b) { return uint16_t(b[0]) | (uint16_t(b[1]) << 8); }
void put16(uint8_t* b, uint16_t v) { b[0] = uint8_t(v); b[1] = uint8_t(v >> 8); }
uint32_t get32(const uint8_t* b) { return get16(b) | (uint32_t(get16(b + 2)) << 16); }
void put32(uint8_t* b, uint32_t v) { put16(b, uint16_t(v)); put16(b + 2, uint16_t(v >> 16)); }
uint16_t crc(const uint8_t* b) {
  uint16_t result = 0xffff;
  for (uint16_t i = 1; i < MemoryManager::recordSize; ++i) {
    if (i == 8 || i == 9) continue; // Exclude checksum and commit marker only.
    result ^= uint16_t(b[i]) << 8;
    for (uint8_t bit = 0; bit < 8; ++bit)
      result = result & 0x8000 ? (result << 1) ^ 0x1021 : result << 1;
  }
  return result;
}
bool newer(uint32_t a, uint32_t b) { return int32_t(a - b) > 0; }
void encode(const Configuration& c, uint8_t* b, uint32_t seq) {
  b[0] = committed; b[1] = version; put16(b + 2, MemoryManager::recordSize);
  put32(b + 4, seq); b[10] = 'R'; b[11] = '4'; b[12] = '8'; b[13] = 'C';
  b[16] = c.count; b[17] = c.applyOnBoot; put16(b + 18, c.pollMs);
  for (uint8_t i = 0; i < PSU_MAX_UNITS; ++i) {
    const auto& p = c.units[i]; uint8_t* out = b + 20 + 12 * i;
    out[0] = p.address; out[1] = p.enabled;
    put16(out + 2, p.voltage); put16(out + 4, p.current);
    put16(out + 6, p.offlineVoltage); put16(out + 8, p.offlineCurrent);
    put16(out + 10, p.ratedCurrent);
  }
  put16(b + 8, crc(b));
}
}
bool MemoryManager::readRecord(uint8_t slot, Configuration& c, uint32_t& seq) const {
  uint8_t b[recordSize];
  const uint16_t base = slot * slotSize;
  if (storage_.read(base) != committed) return false;
  for (uint16_t i = 0; i < recordSize; ++i) b[i] = storage_.read(base + i);
  if (b[1] != version || get16(b + 2) != recordSize || b[10] != 'R' || b[11] != '4' ||
      b[12] != '8' || b[13] != 'C' || b[14] || b[15] || b[17] > 1 || crc(b) != get16(b + 8)) return false;
  Configuration candidate = {};
  candidate.count = b[16]; candidate.applyOnBoot = b[17]; candidate.pollMs = get16(b + 18);
  for (uint8_t i = 0; i < PSU_MAX_UNITS; ++i) {
    const uint8_t* in = b + 20 + 12 * i;
    if (in[1] > 1) return false;
    candidate.units[i] = {in[0], bool(in[1]), get16(in + 2), get16(in + 4),
                         get16(in + 6), get16(in + 8), get16(in + 10)};
  }
  if (!validConfig(candidate)) return false;
  c = candidate; seq = get32(b + 4); return true;
}
StorageResult MemoryManager::load(Configuration& config) const {
  if (storage_.length() < budget) return StorageResult::TooSmall;
  Configuration candidate;
  uint32_t first = 0, second = 0;
  const bool a = readRecord(0, candidate, first);
  if (a) config = candidate;
  const bool b = readRecord(1, candidate, second);
  if (b && (!a || newer(second, first))) config = candidate;
  return a || b ? StorageResult::Ok : StorageResult::NoValidRecord;
}
StorageResult MemoryManager::save(const Configuration& config) {
  if (storage_.length() < budget) return StorageResult::TooSmall;
  if (!validConfig(config)) return StorageResult::InvalidConfig;
  Configuration existing;
  uint32_t first = 0, second = 0;
  const bool a = readRecord(0, existing, first);
  const bool b = readRecord(1, existing, second);
  const uint8_t newest = b && (!a || newer(second, first)) ? 1 : 0;
  const uint8_t target = a || b ? 1 - newest : 0;
  const uint32_t sequence = (a || b ? (newest ? second : first) : 0) + 1;
  uint8_t bytes[recordSize] = {};
  encode(config, bytes, sequence);
  const uint16_t base = target * slotSize;
  storage_.update(base, 0); // Invalidate only the older slot before changing its contents.
  for (uint16_t i = 1; i < recordSize; ++i) storage_.update(base + i, bytes[i]);
  storage_.update(base, committed);
  uint32_t written = 0;
  return readRecord(target, existing, written) && written == sequence
      ? StorageResult::Ok : StorageResult::WriteFailed;
}
}
