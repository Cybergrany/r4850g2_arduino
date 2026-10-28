#pragma once
#include "ControllerTypes.h"
#include "../storage/MemoryManager.h"
namespace psu {
enum class AvailabilityAction : uint8_t { Accept, Include, Require, Probe };
enum class AvailabilityReason : uint8_t { NoCan, NoAc, Reconciling, Ready, Fault };
enum class PermitState : uint8_t { NotFitted, Inhibited, Allowed, Fault };
struct StartupStatus {
  PermitState permit = PermitState::NotFitted;
  bool configurationReady = true;
  bool commissioning = false;
  uint32_t since = 0;
};
struct AvailabilityPlan {
  AvailabilityAction action = AvailabilityAction::Accept;
  uint8_t requested = 0, selected = 0, participants = 0;
  Issue blocker = Issue::None;
  bool remember = false;
  uint32_t configurationRevision = 0, topologyRevision = 0, availabilityRevision = 0, created = 0;
};
struct AvailabilityReport {
  uint32_t sequence = 0;
  Result result = Result::Ok;
  StorageResult storage = StorageResult::Ok;
  bool saving = false, remembered = false;
};
}
