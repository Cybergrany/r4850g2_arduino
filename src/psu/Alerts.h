#pragma once
#include "../config/AlertConfig.h"
#include "../config/BuildOptions.h"
namespace psu {
enum class AlertCode : uint8_t {
  None, Missing, Excluded, Returned, Unstable, Commissioning, Display, Touch, OlderSave,
  ReceiveLoss, DiscoveryFull, ActiveLost, Identity, Command, Control, StopUnconfirmed,
  Cutoff, CanInit, Deployment, Storage, TemperatureWarning, TemperatureError, ThermalStop, TemperatureSensor
};
enum class AlertLevel : uint8_t { None, Warning, Error };
AlertLevel alertLevel(AlertCode code);
struct AlertState {
  uint16_t id = 0;
  AlertCode code = AlertCode::None;
  uint8_t source = 0;
  bool active = false, acknowledged = false;
};
// One primary incident per stable configured slot, plus fixed system sources.
// No message queue, heap allocation or per-report strings.
class AlertManager {
 public:
  enum Source : uint8_t { Thermal = PSU_MAX_UNITS, Storage, Startup, LocalUi, Receive, Discovery, Can, Count };
  void set(uint8_t source, AlertCode code, uint32_t now);
  void event(uint8_t source, AlertCode code, uint32_t now);
  void expire(uint8_t source, uint32_t now);
  void reset(uint8_t source);
  AlertState at(uint8_t source) const;
  bool visible(uint8_t source) const;
  uint8_t count() const;
  uint16_t revision() const { return sequence_; }
  bool warning() const;
  bool error() const;
  bool acknowledge(uint16_t id);
  void acknowledgeAll();
  // Dismiss a modal without opening a queue of incidents accumulated behind it.
  // They remain unacknowledged in the list; new incidents/escalations still notify.
  void deferPendingNotifications();
  // Claims a notification once. Critical escalation bypasses the ordinary gap.
  uint16_t notification(uint32_t now);
  int8_t find(uint16_t id) const;
 private:
  enum : uint8_t { Active = 1, Unacked = 2, Announced = 4, Urgent = 8, Armed = 16, PeakError = 32, PeakStop = 64 };
  // A never-used cell is armed by code == None. Zero initialization avoids an
  // additional SRAM copy of the entire cell array on AVR.
  struct Cell { uint32_t changed = 0; uint16_t id = 0; AlertCode code = AlertCode::None; uint8_t flags = 0; };
  Cell cells_[Count];
  uint32_t lastPopup_ = 0;
  uint16_t sequence_ = 0;
  bool popped_ = false;
};
static_assert(AlertManager::Count <= 16, "Keep alert state bounded on Mega");
}
