#pragma once
#include "../config/ChargerConfig.h"
namespace psu {
class ByteStorage {
 public:
  virtual uint16_t length() const = 0;
  virtual uint8_t read(uint16_t address) const = 0;
  virtual void update(uint16_t address, uint8_t value) = 0;
  virtual bool ready() const { return true; }
  virtual ~ByteStorage() = default;
};
enum class StorageResult : uint8_t { Ok, NoValidRecord, TooSmall, InvalidConfig, WriteFailed, Migrated, LegacyNeedsReview, WrongDeployment, Busy };
struct LegacyUnit { uint8_t address; bool enabled; uint16_t voltage, current, offlineVoltage, offlineCurrent, ratedCurrent; };
struct LegacyConfiguration { uint8_t count; bool applyOnBoot; uint16_t pollMs; LegacyUnit units[PSU_MAX_UNITS]; };
class MemoryManager {
 public:
  static constexpr uint16_t budget = 512, slotSize = 256;
  static constexpr uint16_t recordSize = 16 + 13 + 13 * limits::maxGroups + 8 * PSU_MAX_UNITS + 3 + 2 * PSU_MAX_UNITS;
  explicit MemoryManager(ByteStorage& storage) : storage_(storage) {}
  StorageResult load(Configuration& config) const;
  StorageResult save(const Configuration& config);
  // Snapshot once; Application calls stepSave once per loop, only updating a
  // byte when storage.ready(). UI/serial observe their completion token.
  // Synchronous save is for offline callers/tests, not the runtime UI/console.
  StorageResult startSave(const Configuration& config);
  void stepSave();
  bool saving() const { return saving_; }
  StorageResult saveResult() const { return saveResult_; }
  // Completion belongs to a particular snapshot, even if another caller starts
  // a new save before the UI/serial consumer observes the previous completion.
  uint16_t saveToken() const { return saveToken_; }
  uint16_t completedToken() const { return completedToken_; }
  StorageResult completedResult() const { return completedResult_; }
  bool legacy(LegacyConfiguration& config) const;
 private:
  bool readRecord(uint8_t slot, uint8_t* bytes, uint16_t& size, uint32_t& sequence) const;
  ByteStorage& storage_;
  uint8_t pending_[recordSize] = {};
  uint16_t saveBase_ = 0, saveAt_ = 0;
  uint32_t saveSequence_ = 0;
  bool saving_ = false;
  StorageResult saveResult_ = StorageResult::Ok;
  uint16_t saveToken_ = 0, completedToken_ = 0;
  StorageResult completedResult_ = StorageResult::Ok;
};
static_assert(MemoryManager::recordSize <= MemoryManager::slotSize, "EEPROM journal overflow");
}
