#pragma once
#include "../config/ChargerConfig.h"

namespace psu {
class ByteStorage {
 public:
  virtual uint16_t length() const = 0;
  virtual uint8_t read(uint16_t address) const = 0;
  virtual void update(uint16_t address, uint8_t value) = 0;
  virtual ~ByteStorage() = default;
};
enum class StorageResult : uint8_t { Ok, NoValidRecord, TooSmall, InvalidConfig, WriteFailed };

class MemoryManager {
 public:
  // Two 256-byte slots, commit marker written last, version + CRC + sequence.
  // Uses only addresses 0..511, even on boards with larger EEPROMs.
  static constexpr uint16_t budget = 512;
  static constexpr uint16_t slotSize = budget / 2;
  static constexpr uint16_t recordSize = 16 + 4 + 12 * PSU_MAX_UNITS;
  explicit MemoryManager(ByteStorage& storage) : storage_(storage) {}
  StorageResult load(Configuration& config) const; // Failure leaves config unchanged.
  StorageResult save(const Configuration& config); // Only explicit saves wear EEPROM.
 private:
  bool readRecord(uint8_t slot, Configuration& config, uint32_t& sequence) const;
  ByteStorage& storage_;
};
static_assert(MemoryManager::recordSize <= MemoryManager::slotSize, "EEPROM record exceeds journal slot");
}
