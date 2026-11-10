#include "PsuController.h"
#include <math.h>
namespace psu {
bool PsuController::temperatures(uint8_t i, float& highest, uint32_t now) const {
  float input, output;
  if (!freshMetric(i, protocol::InputTemperature, input, now) ||
      !freshMetric(i, protocol::OutputTemperature, output, now) || !isfinite(input) || !isfinite(output) ||
      input < control::minimumTemperature || input > control::maximumTemperature ||
      output < control::minimumTemperature || output > control::maximumTemperature) return false;
  highest = input > output ? input : output; return true;
}
uint16_t PsuController::limitedCurrent(uint8_t i, uint16_t authorized) const {
  if (i >= count() || !enable_.enabled() || config_.thermalStop != ThermalStop::None ||
      ((sensorFaults_ | excluded_) & (1U << i)) || !(validTemperatures_ & (1U << i))) return 0;
  const uint8_t percent = thermalLevel_ == ThermalLevel::Error ? 100 - alerts::errorReduction :
      thermalLevel_ == ThermalLevel::Warning ? 100 - alerts::warningReduction : 100;
  return uint32_t(authorized) * percent / 100;
}
uint16_t PsuController::effectiveCurrent(uint8_t i) const {
  return i < count() && (config_.operating.currentMask & (1U << i)) ? limitedCurrent(i, config_.operating.current[i]) : 0;
}
uint16_t PsuController::effectiveRaw(uint8_t i) const {
  return protocol::encodeCurrent(effectiveCurrent(i), config_.units[i].ratedCurrent);
}
bool PsuController::acknowledgedCurrent(uint8_t i, uint16_t& raw) const {
  if (i >= count() || !(acknowledgedMask_ & (1U << i))) return false;
  raw = acknowledgedCurrent_[i]; return true;
}
ThermalStatus PsuController::thermalStatus() const {
  ThermalStatus s;
  s.maximum = highestTemperature_; s.valid = validTemperatures_; s.hotMs = hotMs_;
  s.contributors = hotContributors_; s.sensorFaults = sensorFaults_; s.ignored = thermalIgnored_;
  s.level = thermalLevel_; s.percent = thermalLevel_ == ThermalLevel::Error ? 100 - alerts::errorReduction :
      thermalLevel_ == ThermalLevel::Warning ? 100 - alerts::warningReduction : 100;
  s.stop = config_.thermalStop; s.cool = cool_; s.saving = protectionToken_ != 0;
  s.persisted = !protectionDirty_ && !protectionToken_ && !protectionFailed_;
  s.saveFailed = protectionFailed_; s.resuming = resumePending_; return s;
}
void PsuController::thermalStop(ThermalStop reason) {
  resumePending_ = false;
  if (config_.thermalStop != ThermalStop::None) return;
  config_.thermalStop = reason; protectionDirty_ = true; protectionFailed_ = false;
  const bool was = enable_.enabled(); enable_.request(false, EnableSource::Protection); enableChanged(was); thermalRevision_ = revision_;
}
Result PsuController::requestEnable(bool on, EnableSource source) {
  if (!on) resumePending_ = false;
  if (on && config_.thermalStop != ThermalStop::None) {
    if (!cool_) return Result::Blocked;
    resumePending_ = true; resumeSource_ = source; protectionDirty_ = true; protectionFailed_ = false;
    return Result::Ok; // Remain OFF until the cleared journal record is verified.
  }
  if (on) startup_.commissioning = false;
  const bool was = enable_.enabled(); enable_.request(on, source); enableChanged(was); return Result::Ok;
}
Result PsuController::retryTemperature(UnitMask mask, uint32_t now) {
  if (!mask || (mask & ~membersMask(count()))) return Result::Invalid;
  if (busy() || controlActive_) return Result::Busy;
  mask &= sensorFaults_; // A sensor retry cannot clear another command failure.
  for (uint8_t i = 0; i < count(); ++i) if ((mask & sensorFaults_) & (1U << i)) {
    float t;
    if (!temperatures(i, t, now) || routingIssue(i, now) != Issue::None ||
        control_[i].fault != CommandState::Idle || !control_[i].zeroConfirmed) return Result::Blocked;
  }
  sensorFaults_ &= UnitMask(~mask); restoreBlocked_ &= UnitMask(~mask); restorePending_ |= running_ & mask;
  return Result::Ok;
}
void PsuController::thermalTick(uint32_t now) {
  const auto previous = thermalLevel_;
  const UnitMask previousValid = validTemperatures_;
  bool hot = false, belowError = true, allCool = true, monitored = false;
  highestTemperature_ = control::minimumTemperature; validTemperatures_ = 0;
  for (uint8_t i = 0; i < count(); ++i) {
    if (!identified(config_.units[i].identity)) continue;
    const UnitMask bit = UnitMask(1U << i); float temperature;
    const auto route = routingIssue(i, now); const auto* d = deviceForSlot(i, now);
    const bool absent = route == Issue::Missing || (d && d->acState(now, staleMs()) == AcState::Absent);
    // A return revokes the incident-specific waiver; remembered permissions
    // never clear a new hot contribution.
    if (!absent && route == Issue::None) thermalIgnored_ &= UnitMask(~bit);
    if (absent && (thermalIgnored_ & bit)) continue;
    // Any trustworthy hot reading acts immediately, even if its partner is
    // absent. Both readings are still required to permit current or prove cool.
    bool complete = true; temperature = control::minimumTemperature;
    for (uint8_t sensor = 0; sensor < 2; ++sensor) {
      const auto metric = sensor ? protocol::OutputTemperature : protocol::InputTemperature;
      float value;
      if (!freshMetric(i, metric, value, now) || !isfinite(value) || value < control::minimumTemperature || value > control::maximumTemperature) { complete = false; continue; }
      if (value > temperature) temperature = value;
      if (value > highestTemperature_) highestTemperature_ = value;
      if (value >= alerts::warningC) { hotContributors_ |= bit; thermalIgnored_ &= UnitMask(~bit);
        if (thermalLevel_ == ThermalLevel::Normal) thermalLevel_ = ThermalLevel::Warning; }
      if (value >= alerts::errorC) { hot = true; thermalLevel_ = ThermalLevel::Error; }
      if (value > alerts::stopC) thermalStop(ThermalStop::Temperature);
    }
    if (complete) {
      validTemperatures_ |= bit;
      if (temperature < alerts::clearC) hotContributors_ &= UnitMask(~bit);
      if (!(thermalIgnored_ & bit)) {
        monitored = true; allCool &= temperature < alerts::clearC;
        belowError &= temperature < alerts::errorC;
      }
    } else if (!(thermalIgnored_ & bit) && (!(excluded_ & acceptedAbsences() & bit) || (hotContributors_ & bit))) {
      monitored = true; allCool = belowError = false;
      // Loss of CAN is classified separately. A live unit whose temperature
      // coverage disappears is stopped individually and requires explicit retry.
      auto& u = control_[i]; bool liveLoss = false;
      for (uint8_t sensor=0; d && sensor<2; ++sensor) {
        const auto metric = sensor ? protocol::OutputTemperature : protocol::InputTemperature;
        float value;
        liveLoss |= (freshMetric(i, metric, value, now) && (!isfinite(value) || value < control::minimumTemperature || value > control::maximumTemperature)) ||
            uint32_t(d->lastSeen - d->telemetry.updated[metric]) > staleMs();
      }
      if ((energized_ & bit) && route == Issue::None && !absent && d->epoch == observedEpoch_[i] && liveLoss &&
          (u.outputDemand == OutputDemand::Starting || u.outputDemand == OutputDemand::Active) && !(sensorFaults_ & bit)) {
        sensorFaults_ |= bit; restoreBlocked_ |= bit; currentSynced_ &= UnitMask(~bit);
        if (controlActive_ && controlSlot_ == i) controlActive_ = controlWaiting_ = controlVerified_ = false;
        u.zeroConfirmed = false; u.attempts = 0; controlPhase(i, ControlPhase::Zero, now);
      }
    }
    const auto& u = control_[i];
    if ((energized_ & bit) && (route != Issue::None || absent ||
        (u.outputDemand == OutputDemand::Active && !readinessAcceptable(i, now)))) activeLost_ |= bit;
    if ((u.outputDemand == OutputDemand::Active || (u.reconciled && u.zeroConfirmed && u.awakeConfirmed)) &&
        u.fault == CommandState::Idle && route == Issue::None && !absent && (validTemperatures_ & bit) && readinessAcceptable(i, now)) {
      activeLost_ &= UnitMask(~bit); identityLost_ &= UnitMask(~bit);
    }
    if (u.zeroConfirmed || u.standbyConfirmed) energized_ &= UnitMask(~bit);
  }
  const uint32_t elapsed = uint32_t(now - thermalLast_); thermalLast_ = now;
  if (hot && hotPrevious_) {
    const uint32_t bounded = elapsed > staleMs() ? staleMs() : elapsed;
    hotMs_ = bounded >= alerts::errorMs - hotMs_ ? alerts::errorMs : hotMs_ + bounded;
  } else if (belowError && monitored) hotMs_ = 0;
  hotPrevious_ = hot;
  if (hot && hotMs_ >= alerts::errorMs) thermalStop(ThermalStop::Duration);
  cool_ = monitored && allCool && !hotContributors_;
  if (cool_) thermalLevel_ = ThermalLevel::Normal;
  if (resumePending_ && !cool_) { resumePending_ = false; protectionDirty_ = true; }
  if (previous != thermalLevel_ || previousValid != validTemperatures_) {
    for (uint8_t i = 0; i < count(); ++i)
      if (!(acknowledgedMask_ & (1U << i)) || acknowledgedCurrent_[i] != effectiveRaw(i)) {
        currentSynced_ &= UnitMask(~(1U << i)); control_[i].readyFrames = 0;
      }
  }
}
// Runs before jobs/refreshes. Existing positive jobs may finish their bounded ACK
// wait; their ACK never synchronizes a superseded target. Unsent jobs yield.
bool PsuController::correctCurrent(uint32_t now) {
  for (uint8_t i = 0; i < count(); ++i) {
    const auto& u = control_[i]; const UnitMask bit = UnitMask(1U << i);
    if (u.phase != ControlPhase::Online || u.fault != CommandState::Idle || !(acknowledgedMask_ & bit) ||
        !(voltageSynced_ & bit) || routingIssue(i, now) != Issue::None || !(config_.operating.currentMask & bit)) continue;
    const uint16_t target = effectiveRaw(i);
    if (target == acknowledgedCurrent_[i]) continue;
    // Only protection may overtake an apply. Increases wait for the job and all
    // normal release checks, so no staged or unqualified allocation leaks out.
    if (target > acknowledgedCurrent_[i] && (busy() || !releaseAllowed(now) || (restoreBlocked_ & bit))) continue;
    verifying_ = identityChecked_ = false;
    controlCommand(i, protocol::OnlineCurrent, target, now);
    controlRefresh_ = controlCorrection_ = controlActive_; return true;
  }
  return false;
}
void PsuController::serviceProtection(MemoryManager& memory, uint32_t now) {
  now_ = now;
  if (protectionToken_ && savingStop_ == ThermalStop::None && (!resumePending_ || !cool_) &&
      memory.reviseProtection(protectionToken_, config_.thermalStop)) savingStop_ = config_.thermalStop;
  if (memory.completedToken() != storageSeenToken_) {
    storageSeenToken_ = memory.completedToken(); storageStatus(memory.completedResult());
    if (protectionFailed_ && memory.completedResult() == StorageResult::Ok && !memory.saving()) {
      Configuration saved;
      if (memory.load(saved) == StorageResult::Ok && saved.thermalStop == config_.thermalStop && !saved.autoResume)
        protectionFailed_ = false;
    }
  }
  if (protectionToken_ && memory.completedToken() == protectionToken_) {
    protectionToken_ = 0;
    if (memory.completedResult() != StorageResult::Ok) {
      protectionFailed_ = true; protectionDirty_ = false; resumePending_ = false;
    } else if (savingStop_ == ThermalStop::None && resumePending_ && cool_ && config_.thermalStop != ThermalStop::None) {
      config_.thermalStop = ThermalStop::None; protectionDirty_ = false; resumePending_ = false;
      requestEnable(true, resumeSource_); thermalRevision_ = revision_;
    } else {
      // An OFF/hot edge can overtake a clear-save. Repair the journal before any
      // later resume, and never let the stale completion turn the output on.
      protectionDirty_ = savingStop_ != config_.thermalStop || resumePending_;
    }
  }
  if (!protectionToken_ && protectionDirty_ && !memory.saving()) {
    savingStop_ = resumePending_ && cool_ ? ThermalStop::None : config_.thermalStop;
    const auto result = memory.startProtectionSave(savingStop_);
    if (result == StorageResult::Ok) { protectionToken_ = memory.saveToken(); protectionDirty_ = false; }
    else if (result != StorageResult::Busy) {
      protectionDirty_ = false; protectionFailed_ = true; resumePending_ = false; storageStatus(result);
    }
  }
}
}
