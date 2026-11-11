#include "PsuController.h"
#include "Presentation.h"
#include "../config/Deployment.h"
namespace psu {
void PsuController::storageStatus(StorageResult result, bool older) {
  if (result == StorageResult::Busy) return;
  const auto code = result == StorageResult::Ok ? (older ? AlertCode::OlderSave : AlertCode::None) :
      result == StorageResult::NoValidRecord || result == StorageResult::LegacyNeedsReview || result == StorageResult::Migrated ?
      AlertCode::Commissioning : result == StorageResult::WrongDeployment ? AlertCode::Deployment : AlertCode::Storage;
  alerts_.set(AlertManager::Storage, code, now_);
}
void PsuController::localStatus(bool display, bool calibration, bool saveFailed) {
  alerts_.set(AlertManager::LocalUi, saveFailed ? AlertCode::Storage : !display ? AlertCode::Display : !calibration ? AlertCode::Touch : AlertCode::None, now_);
}
void PsuController::alertTick(uint32_t now) {
  alerts_.set(AlertManager::Can, ready_ ? AlertCode::None : AlertCode::CanInit, now);
  alerts_.set(AlertManager::Thermal, config_.thermalStop != ThermalStop::None ? AlertCode::ThermalStop :
      thermalLevel_ == ThermalLevel::Error ? AlertCode::TemperatureError :
      thermalLevel_ == ThermalLevel::Warning ? AlertCode::TemperatureWarning : AlertCode::None, now);
  if (protectionFailed_) alerts_.set(AlertManager::Storage, AlertCode::Storage, now);
  const auto status = controlStatus(now);
  alerts_.set(AlertManager::Startup, config_.deploymentId != deployment::id ? AlertCode::Deployment :
      startup_.permit == PermitState::Fault ? AlertCode::Cutoff :
      (enable_.enabled() && status.blocker == Issue::NoProfile) || (!startup_.configurationReady && startup_.permit == PermitState::Inhibited) ? AlertCode::Commissioning : AlertCode::None, now);
  const UnitMask attention = availabilityAttention(now);
  for (uint8_t i = 0; i < count(); ++i) {
    const UnitMask bit = UnitMask(1U << i); const auto& u = control_[i]; const auto route = routingIssue(i, now);
    const int8_t group = groupFor(config_, i); const auto reason = availabilityReason(i, now);
    AlertCode code = AlertCode::None;
    if ((activeLost_ & bit) && ((identityLost_ & bit) || route == Issue::Conflict || route == Issue::IdentityPending)) code = AlertCode::Identity;
    else if (sensorFaults_ & bit) code = AlertCode::TemperatureSensor;
    else if (activeLost_ & bit) code = AlertCode::ActiveLost;
    else if (u.fault != CommandState::Idle) code = !u.zeroConfirmed && !u.standbyConfirmed ? AlertCode::StopUnconfirmed : AlertCode::Control;
    else if ((excluded_ & acceptedAbsences() & bit) && (reason == AvailabilityReason::NoCan || reason == AvailabilityReason::NoAc)) code = AlertCode::Excluded;
    else if (u.fanFailed || ((restoreBlocked_ & bit) && commandFailed(report_.units[i].state) && report_.units[i].state != CommandState::Changed &&
        report_.units[i].state != CommandState::Incomplete)) code = AlertCode::Command;
    else if (route == Issue::Conflict) code = AlertCode::Identity;
    else if (!identified(config_.units[i].identity) || group < 0) code = AlertCode::Commissioning;
    else if ((excluded_ & bit) && !(deliberateExclusions() & bit) && reason == AvailabilityReason::Ready) code = AlertCode::Returned;
    else if ((excluded_ & acceptedAbsences() & bit)) code = AlertCode::Excluded;
    else if (attention & bit) code = AlertCode::Missing;
    else if (group >= 0 && availabilityUnstable(group, now)) code = AlertCode::Unstable;
    alerts_.set(i, code, now);
  }
  if (transport_.takeReceiveLoss()) alerts_.event(AlertManager::Receive, AlertCode::ReceiveLoss, now);
  if (discovery_.takeOverflow()) alerts_.event(AlertManager::Discovery, AlertCode::DiscoveryFull, now);
  alerts_.expire(AlertManager::Receive, now); alerts_.expire(AlertManager::Discovery, now);
}
}
