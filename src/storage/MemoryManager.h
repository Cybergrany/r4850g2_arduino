#pragma once
#include "../config/ChargerConfig.h"
#include "../config/ProfileConfig.h"
namespace psu {
class ByteStorage {
 public:
  virtual uint16_t length() const = 0;
  virtual uint8_t read(uint16_t address) const = 0;
  virtual void update(uint16_t address, uint8_t value) = 0;
  virtual bool ready() const { return true; }
  virtual ~ByteStorage() = default;
};
enum class StorageResult : uint8_t { Ok, NoValidRecord, TooSmall, InvalidConfig, WriteFailed, Migrated, LegacyNeedsReview, WrongDeployment, Busy, IdentityMismatch, Changed, Cancelled };
struct ProfileInfo { char name[profiles::nameLength + 1]; uint32_t generation; UnitMask included; uint8_t count; bool hasInclusion; };
struct ProfileTarget { Identity identity; uint16_t voltage, current; };
struct LegacyUnit { uint8_t address; bool enabled; uint16_t voltage, current, offlineVoltage, offlineCurrent, ratedCurrent; };
struct LegacyConfiguration { uint8_t count; bool applyOnBoot; uint16_t pollMs; LegacyUnit units[PSU_MAX_UNITS]; };
class MemoryManager {
 public:
  static constexpr uint16_t budget = 4096, base = 512, slotSize = 512;
  static constexpr uint16_t recordSize = 34 + 8 * limits::maxGroups + 16 * PSU_MAX_UNITS + (PSU_MAX_UNITS + 1) / 2;
  explicit MemoryManager(ByteStorage& storage) : storage_(storage) {}
  StorageResult load(Configuration& config) const;
  // For a caller-owned temporary only. Failure may modify the temporary;
  // callers must inspect the result before configuring the controller.
  StorageResult loadCandidate(Configuration& candidate) const;
  StorageResult save(const Configuration& config);
  // Snapshot once; Application calls stepSave once per loop, only updating a
  // byte when storage.ready(). UI/serial observe their completion token.
  // Synchronous save is for offline callers/tests, not the runtime UI/console.
  StorageResult startSave(const Configuration& config);
  StorageResult startAvailabilitySave(const Configuration& config, UnitMask accepted, UnitMask disabled);
  // Modify only protection state in the last saved profile, never RAM drafts.
  StorageResult startProtectionSave(ThermalStop stop);
  bool reviseProtection(uint16_t token, ThermalStop stop);
  void stepSave();
  bool saving() const { return saving_ || auxiliaryBusy_; }
  // Calibration uses the same byte device and acquires this single-writer gate.
  void auxiliaryBusy(bool busy) { auxiliaryBusy_ = busy; }
  StorageResult saveResult() const { return saveResult_; }
  // Completion belongs to a particular snapshot, even if another caller starts
  // a new save before the UI/serial consumer observes the previous completion.
  uint16_t saveToken() const { return saveToken_; }
  uint16_t completedToken() const { return completedToken_; }
  StorageResult completedResult() const { return completedResult_; }
  bool persistedProtection(ThermalStop stop) const;
  bool legacy(LegacyConfiguration& config) const;
  StorageResult profileInfo(uint8_t slot, ProfileInfo& info) const;
  StorageResult profileTarget(uint8_t slot, uint8_t entry, uint32_t generation, ProfileTarget& target) const;
  StorageResult profileTargets(uint8_t slot, uint32_t generation, const Configuration& config,
      uint16_t* voltage, uint16_t* current, uint8_t& mismatch, UnitMask* included = nullptr) const;
  StorageResult startProfileSave(uint8_t slot, const char* name, const Configuration& config, uint32_t expectedGeneration,
      bool storeInclusion = false, UnitMask included = allUnitsMask);
  bool cancelProfileSave(); // Protection can preempt an uncommitted profile copy.
 private:
  int16_t newestConfig(Configuration* scratch = nullptr) const;
  StorageResult loadImpl(Configuration& config, bool temporary, int16_t* address = nullptr) const;
  __attribute__((noinline)) StorageResult loadLegacy(Configuration& config, uint16_t size) const;
  __attribute__((noinline)) StorageResult startMigratedProtection(ThermalStop stop);
  int16_t newestProfile(uint8_t slot) const;
  bool readRecord(uint16_t address, uint16_t& size, uint32_t& sequence) const;
  void startWrite(uint16_t address, uint16_t length, uint32_t sequence, bool profile);
  ByteStorage& storage_;
  // Reads own this scratch only while the writer is idle. All record selection
  // uses the same buffer, avoiding nested full-record stack allocations.
  mutable uint8_t pending_[recordSize > 216 ? recordSize : 216] = {};
  uint16_t saveBase_ = 0, saveAt_ = 0;
  uint16_t saveLength_ = recordSize;
  uint32_t saveSequence_ = 0;
  bool saving_ = false, auxiliaryBusy_ = false;
  bool profileSave_ = false;
  StorageResult saveResult_ = StorageResult::Ok;
  uint16_t saveToken_ = 0, completedToken_ = 0;
  StorageResult completedResult_ = StorageResult::Ok;
};
static_assert(MemoryManager::recordSize <= MemoryManager::slotSize && MemoryManager::recordSize >= profiles::recordBytes, "EEPROM journal overflow");
static_assert(profiles::base + profiles::count * profiles::slotBytes <= MemoryManager::budget, "Profile EEPROM overflow");
}
