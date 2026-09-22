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
enum class StorageResult : uint8_t { Ok, NoValidRecord, TooSmall, InvalidConfig, WriteFailed, Migrated, LegacyNeedsReview, WrongDeployment };
struct LegacyUnit { uint8_t address; bool enabled; uint16_t voltage, current, offlineVoltage, offlineCurrent, ratedCurrent; };
struct LegacyConfiguration { uint8_t count; bool applyOnBoot; uint16_t pollMs; LegacyUnit units[PSU_MAX_UNITS]; };
class MemoryManager {
 public:
  static constexpr uint16_t budget = 512, slotSize = 256;
  static constexpr uint16_t recordSize = 16 + 13 + 13 * limits::maxGroups + 8 * PSU_MAX_UNITS + 3 + 2 * PSU_MAX_UNITS;
  explicit MemoryManager(ByteStorage& storage) : storage_(storage) {}
  StorageResult load(Configuration& config) const;
  StorageResult save(const Configuration& config);
  bool legacy(LegacyConfiguration& config) const;
 private:
  bool readRecord(uint8_t slot, uint8_t* bytes, uint16_t& size, uint32_t& sequence) const;
  ByteStorage& storage_;
};
static_assert(MemoryManager::recordSize <= MemoryManager::slotSize, "EEPROM journal overflow");
}
