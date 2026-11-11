#pragma once
#include "../config/BuildOptions.h"
#if PSU_UI_DEBUG
#include "../psu/PsuController.h"
#include "../storage/MemoryManager.h"
#include "../ui/UiFrame.h"
namespace psu {
enum class DebugScenario : uint8_t {
  Healthy, Empty, Missing, Stale, Unbound, Conflict, Replacement, NotReady,
  VoltageUnsynced, Reject, Timeout, IdentityTimeout, Transport, Partial, SaveFailed, Changed,
  Extremes, WrongDeployment, Unassigned, IdentityPending, SaveChanged, BusOffline,
  ZeroHold, Hot, Cool, SensorFault, FanReject, ZeroReject, StopUnconfirmed, WakeCurrent,
  BatteryCan, ColdStart, SlowStartup, SourceAbsent, SourceReturn, PartialReturn, UnstableSource, ThermalWarning, ThermalError, ThermalStop, InputHot, Count
};
FlashText scenarioName(DebugScenario scenario);
Configuration debugConfiguration();
// This store cannot access EEPROM. Failed writes exercise real journal verification.
class RamStorage : public ByteStorage {
 public:
  RamStorage() { clear(); }
  void clear();
  uint16_t length() const override { return 4096; }
  uint8_t read(uint16_t at) const override;
  void update(uint16_t at, uint8_t value) override;
  bool failWrites = false;
 private:
#if PSU_DEBUG_COMPACT_STORAGE
  // Bench simulation keeps the latest copy per record. No physical persistence
  // or power-loss guarantee is claimed here; host tests use the full journal.
  // Four simulated units leave room for all eight profiles and UI call stacks.
  static constexpr uint16_t bytes = MemoryManager::recordSize + 8 * 128 + 2 * 40 + 2;
  static int16_t address(uint16_t at);
#else
  static constexpr uint16_t bytes = 4096;
  static int16_t address(uint16_t at) { return at < bytes ? int16_t(at) : -1; }
#endif
  uint8_t bytes_[bytes];
};
// Generates one response at a time: no heap, hardware access or unbounded queues.
class SimulatedCan : public CanTransport {
 public:
  bool begin() override { return scenario_ != DebugScenario::BusOffline; }
  bool send(const CanFrame& frame) override;
  void service(uint32_t now) override;
  bool receive(CanFrame& frame) override;
  void reset();
  void scenario(DebugScenario value);
  void permit(bool allowed);
  void sourcePower(UnitMask mask, bool on);
  void batteryPower(UnitMask mask, bool on);
  void temperatures(int16_t output, int16_t input) { outputTemperature_ = output; inputTemperature_ = input; }
  DebugScenario scenario() const { return scenario_; }
 private:
  bool present(uint8_t slot) const;
  bool ac(uint8_t slot) const { return permit_ && (sources_ & (1U << slot)); }
  bool outputReady(uint8_t slot) const;
  uint16_t measuredCurrent(uint8_t slot) const;
  CanFrame identityFrame(uint8_t slot) const;
  CanFrame metricFrame(uint8_t slot, uint8_t metric) const;
  CanFrame broadcastFrame(uint8_t slot) const;
  uint16_t voltage_[PSU_MAX_UNITS] = {}, current_[PSU_MAX_UNITS] = {};
  CanFrame response_ = {};
  uint32_t lastCycle_ = 0;
  uint8_t slot_ = 0, field_ = 0, budget_ = 0;
  bool pending_ = false, cycling_ = false;
  UnitMask standby_ = 0, fanMax_ = 0, wakeSeen_ = 0;
  DebugScenario scenario_ = DebugScenario::Healthy;
  UnitMask sources_ = allUnitsMask, batteries_ = allUnitsMask;
  bool permit_ = true;
  uint32_t now_ = 0, scenarioSince_ = 0, changedSource_ = 0;
  uint32_t readyAt_[PSU_MAX_UNITS] = {};
  int16_t outputTemperature_ = INT16_MIN, inputTemperature_ = INT16_MIN;
};
}
#endif
