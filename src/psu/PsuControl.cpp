#include "PsuController.h"
#include "FanPolicy.h"
#include "../config/Deployment.h"
#include <math.h>
namespace psu {
namespace {
bool after(uint32_t sample, uint32_t boundary, uint32_t now) {
  return uint32_t(now - sample) < uint32_t(now - boundary);
}
void preserveCooling(UnitControl& next, const UnitControl& previous, bool uncertainFan) {
  next.coolSince = previous.coolSince; next.cooling = previous.cooling;
  next.fanRequested = previous.fanRequested; next.fanReason = previous.fanReason;
  next.fanOverride = previous.fanOverride;
  next.fanAcknowledged = previous.fanFailed || uncertainFan ? FanMode::Unknown : previous.fanAcknowledged;
}
}
Issue PsuController::routingIssue(uint8_t i, uint32_t now) const {
  if (discovery_.negotiating()) return Issue::IdentityPending;
  if (config_.deploymentId != deployment::id) return Issue::DeploymentMismatch;
  if (i >= count()) return Issue::Invalid;
  if (!identified(config_.units[i].identity)) return Issue::Unbound;
  for (uint8_t j = 0; j < Discovery::capacity; ++j) {
    const auto& d = discovery_.device(j);
    if (d.conflict && d.fresh(now, limits::identityLeaseMs) && sameIdentity(d.identity, config_.units[i].identity))
      return Issue::Conflict;
  }
  const auto* d = deviceForSlot(i, now);
  if (!d || !d->fresh(now, staleMs())) return Issue::Missing;
  return d->verified(now) ? Issue::None : Issue::IdentityPending;
}
void PsuController::cancelJob() {
  if (busy()) {
    const uint8_t unfinished = report_.recipients & uint8_t(~report_.succeeded);
    report_.failed |= unfinished;
    for (uint8_t i = 0; i < count(); ++i) if (unfinished & (1U << i)) report_.units[i].state = CommandState::Changed;
    report_.active = false;
  }
  pending_ = 0; waiting_ = verifying_ = identityChecked_ = false;
}
void PsuController::reconcile() {
  if (!ready_) begin(); // Explicit retry also retries local CAN initialization.
  if (startup_.permit == PermitState::Fault) {
    startup_.permit = PermitState::Inhibited; startup_.since = now_;
  }
  controlRefresh_ = refreshCurrent_ = false;
  // Cooling has its own lifetime. A software stop/retry is not a new physical
  // PSU: preserve its hysteresis and the accepted persistent fan mode.
  const int8_t uncertainFan = controlActive_ && controlWaiting_ && controlRegister_ == protocol::FanMode ? controlSlot_ : -1;
  cancelJob(); controlActive_ = controlWaiting_ = controlVerified_ = false;
  voltageSynced_ = currentSynced_ = restoreBlocked_ = 0;
  restorePending_ = running_;
  for (uint8_t i = 0; i < count(); ++i) {
    auto& u = control_[i];
    clearFanOverride(i);
    UnitControl next;
    preserveCooling(next, u, i == uncertainFan);
    u = next;
  }
}
void PsuController::enableChanged(bool wasEnabled) {
  // Save persists the effective preference; switch edges never write EEPROM.
  config_.autoResume = enable_.enabled(); ++revision_;
  if (wasEnabled && !enable_.enabled()) {
    // A live OFF only changes current. Preserve known voltage, awake state and
    // cooling; no standby/wake cycle and no automatic clearing of faults.
    if (controlActive_ && controlWaiting_ && controlRegister_ == protocol::FanMode)
      control_[controlSlot_].fanAcknowledged = FanMode::Unknown;
    cancelJob(); controlActive_ = controlWaiting_ = controlVerified_ = false;
    currentSynced_ = 0; restorePending_ |= running_;
    for (uint8_t i = 0; i < count(); ++i) if (control_[i].fault == CommandState::Idle) {
      control_[i].zeroConfirmed = false; control_[i].attempts = 0;
      controlPhase(i, ControlPhase::Zero, 0);
    }
  }
  else if (!wasEnabled && enable_.enabled()) {
    for (uint8_t i = 0; i < count(); ++i)
      if (control_[i].fanOverride == FanOverride::Auto) clearFanOverride(i);
    running_ = config_.operating.voltageMask;
    restorePending_ |= running_;
  }
}
Result PsuController::setAutoResume(bool on) {
  if (on) startup_.commissioning = false;
  const bool was = enable_.enabled(); enable_.request(on, EnableSource::Serial);
  enableChanged(was); return Result::Ok;
}
void PsuController::sampleEnableInput(bool active, uint32_t now) {
  const bool was = enable_.enabled();
  if (enable_.sample(active, now)) enableChanged(was);
}
Issue PsuController::fanOverrideIssue(uint8_t i, FanOverride mode, uint32_t now) const {
  if (i >= count() || (mode != FanOverride::Policy && mode != FanOverride::Auto && mode != FanOverride::Max)) return Issue::Invalid;
  if (mode == FanOverride::Policy) return Issue::None; // Always permit clearing a test, even if a unit vanished.
  if (!ready_) return Issue::Invalid;
  const auto route = routingIssue(i, now);
  if (route != Issue::None) return route;
  const auto& u = control_[i];
  if (u.fanFailed) return Issue::ControlFault; // A test is not a fault-reset command.
  if (mode == FanOverride::Auto) {
    const auto* d = deviceForSlot(i, now); float amps;
    if (enable_.enabled() || u.fault != CommandState::Idle || u.phase != ControlPhase::Held ||
        !u.zeroConfirmed || !u.awakeConfirmed || !d || !d->telemetry.broadcastSeen || !readinessAcceptable(i, now) ||
        uint32_t(now - d->telemetry.lastBroadcast) > staleMs() ||
        !freshMetric(i, protocol::OutputCurrent, amps, now) || !isfinite(amps) || fabsf(amps) > control::stoppedCurrentAmps)
      return Issue::FanTestRequiresHold;
  }
  return Issue::None;
}
void PsuController::clearFanOverride(uint8_t i) {
  auto& u = control_[i];
  if (u.fanOverride == FanOverride::Policy) return;
  u.fanOverride = FanOverride::Policy;
  // Do not inherit a manually forced AUTO through the policy's deadband.
  u.fanRequested = FanMode::Max; u.fanReason = FanReason::Startup; u.cooling = false;
}
Result PsuController::setFanOverride(uint8_t mask, FanOverride mode, uint32_t now) {
  if (!mask || (mask & ~membersMask(count())) ||
      (mode != FanOverride::Policy && mode != FanOverride::Auto && mode != FanOverride::Max)) return Result::Invalid;
  if (busy()) return Result::Busy;
  for (uint8_t i = 0; i < count(); ++i) if ((mask & (1U << i)) && fanOverrideIssue(i, mode, now) != Issue::None)
    return Result::Blocked; // Validate the complete selection before changing any member.
  for (uint8_t i = 0; i < count(); ++i) if (mask & (1U << i)) {
    if (mode == FanOverride::Policy) clearFanOverride(i);
    else control_[i].fanOverride = mode;
    updateFan(i, now);
  }
  return Result::Ok;
}
uint16_t PsuController::reconciliationVoltage() const {
  return config_.operating.voltageAuthorized ? config_.operating.voltage : deployment::voltage;
}
bool PsuController::readinessAcceptable(uint8_t i, uint32_t now) const {
  const auto* d = deviceForSlot(i, now);
  if (!d) return false;
  if (d->telemetry.ready) return true;
  const auto& u = control_[i]; float amps;
  // A zero limit can remove the PSU's output-ready indication without losing
  // CAN or AC. Do not infer this exception from silence or arbitrary alarms.
  if (!d->idleStatus(now, staleMs()) || !u.awakeConfirmed || !u.reconciled ||
      (u.phase != ControlPhase::Held && u.phase != ControlPhase::Online) ||
      !freshMetric(i, protocol::OutputCurrent, amps, now) || !isfinite(amps)) return false;
  const uint32_t age = now - u.outputSince;
  switch (u.outputDemand) {
    case OutputDemand::Zero:
      return u.zeroConfirmed ? fabsf(amps) <= control::stoppedCurrentAmps : age < staleMs();
    case OutputDemand::Starting:
      return age < (staleMs() > control::startupReadyMs ? staleMs() : control::startupReadyMs);
    case OutputDemand::Stopping: return age < limits::ackTimeoutMs;
    default: return false;
  }
}
void PsuController::currentSent(uint8_t i, uint16_t raw, uint32_t now) {
  auto& u = control_[i];
  if (raw) {
    if (u.outputDemand != OutputDemand::Active && u.outputDemand != OutputDemand::Starting) {
      u.outputDemand = OutputDemand::Starting; u.outputSince = now;
    }
    u.zeroConfirmed = false;
  } else if (u.outputDemand != OutputDemand::Zero) {
    u.outputDemand = OutputDemand::Stopping; u.outputSince = now; u.zeroConfirmed = false;
  }
}
void PsuController::currentAccepted(uint8_t i, uint16_t raw) {
  if (!raw) control_[i].outputDemand = OutputDemand::Zero;
}
void PsuController::controlPhase(uint8_t slot, ControlPhase phase, uint32_t now) {
  auto& u = control_[slot]; u.phase = phase; u.since = now;
}
void PsuController::latchControlFault(uint8_t slot, CommandState state, uint8_t reg, uint32_t now) {
  auto& u = control_[slot];
  // An observation can fail while a fan/wake identity check or ACK is pending.
  // Its late completion must not overwrite the emergency-stop phase.
  if (controlActive_ && controlSlot_ == slot) {
    if (controlWaiting_ && controlRegister_ == protocol::FanMode) u.fanAcknowledged = FanMode::Unknown;
    controlActive_ = controlWaiting_ = controlVerified_ = false;
  }
  u.fault = state; u.faultRegister = reg; restoreBlocked_ |= 1U << slot;
  // Standby is only a bounded fault fallback when zero-current holding is not
  // confirmed. Never wake from this fallback without explicit reconciliation.
  const bool fallbackFailed = u.phase == ControlPhase::EmergencyStandby || u.phase == ControlPhase::VerifyStandby;
  u.attempts = 0; // A fallback is a new command step, including wake-OFF -> standby-ON.
  controlPhase(slot, !u.zeroConfirmed && !u.standbyConfirmed && !fallbackFailed ?
      ControlPhase::EmergencyStandby : ControlPhase::Fault, now);
  // A failed INFO query sent no actuation command. Let link expiry classify an
  // absent source; it must not turn a boot-time absence into a global AC trip.
  if (state != CommandState::IdentityTimeout && !u.zeroConfirmed && !u.standbyConfirmed && startup_.permit == PermitState::Allowed) hardStop();
}
void PsuController::updateFan(uint8_t i, uint32_t now) {
  auto& u = control_[i]; float temperature = 0, amps = 0;
  if (u.fanOverride == FanOverride::Auto && fanOverrideIssue(i, FanOverride::Auto, now) != Issue::None)
    clearFanOverride(i);
  if (u.fanOverride != FanOverride::Policy) {
    const bool maximum = u.fanOverride == FanOverride::Max;
    u.fanRequested = maximum ? FanMode::Max : FanMode::Auto;
    u.fanReason = maximum ? FanReason::ManualMax : FanReason::ManualAuto;
    return;
  }
  const bool data = freshMetric(i, protocol::OutputTemperature, temperature, now) && isfinite(temperature) &&
      temperature >= control::minimumTemperature && temperature <= control::maximumTemperature &&
      freshMetric(i, protocol::OutputCurrent, amps, now) && isfinite(amps) && amps >= 0;
  const uint16_t requested = (excluded_ & (1U << i)) || (!enable_.enabled() && (u.zeroConfirmed || u.standbyConfirmed)) ? 0 :
      (config_.operating.currentMask & (1U << i)) ? config_.operating.current[i] : 0;
  if (!data || routingIssue(i, now) != Issue::None) u.fanReason = FanReason::InvalidTelemetry;
  else if (temperature >= control::fanOutputTempThreshold) u.fanReason = FanReason::Temperature;
  else if (requested >= control::fanOutputCurrentThreshold) u.fanReason = FanReason::RequestedCurrent;
  else if (amps * 100 >= control::fanOutputCurrentThreshold) u.fanReason = FanReason::MeasuredCurrent;
  else {
    if (temperature <= control::fanOutputTempRelease && requested <= control::fanOutputCurrentRelease &&
        amps * 100 <= control::fanOutputCurrentRelease) {
      if (!u.cooling) { u.cooling = true; u.coolSince = now; }
      if (uint32_t(now - u.coolSince) >= control::fanReleaseMs) {
        u.fanRequested = FanMode::Auto; u.fanReason = FanReason::Cool; return;
      }
    } else u.cooling = false;
    u.fanReason = FanReason::Cooling; return; // Retain Auto/Max through the deadband.
  }
  u.cooling = false; u.fanRequested = FanMode::Max;
}
ExternalFanStatus PsuController::externalFanStatus(uint32_t now) const {
  ExternalFanStatus s;
  if (!ready_) return s;
  bool maximum = false;
  for (uint8_t i = 0; i < count(); ++i) {
    float temperature, amps;
    if (routingIssue(i, now) != Issue::None ||
        !freshMetric(i, protocol::OutputTemperature, temperature, now) || !isfinite(temperature) ||
        temperature < control::minimumTemperature || temperature > control::maximumTemperature ||
        !freshMetric(i, protocol::OutputCurrent, amps, now) || !isfinite(amps) || amps < 0) {
      s.slot = i; s.reason = FanReason::InvalidTelemetry; return s;
    }
    if (!i || temperature > s.maximumTemperature) s.maximumTemperature = temperature;
    const auto& u = control_[i];
    if (u.fanRequested != FanMode::Auto && !maximum) {
      maximum = true; s.slot = i; s.reason = u.fanReason;
    }
  }
  s.telemetryComplete = true;
  if (!maximum) { s.duty = externalFanCurve(s.maximumTemperature); s.reason = FanReason::Cool; }
  return s;
}
bool PsuController::controlsOnline(uint8_t mask) const {
  for (uint8_t i = 0; i < count(); ++i) if (mask & (1U << i))
    if ((excluded_ & (1U << i)) || control_[i].phase != ControlPhase::Online || control_[i].fault != CommandState::Idle) return false;
  return true;
}
bool PsuController::wakeAllowed(uint8_t i, uint32_t now) const {
  const auto& u = control_[i]; const auto* d = deviceForSlot(i, now); float amps;
  // Wake at zero is per unit and independent of enable/group completeness.
  // The installation-wide barrier still guards every positive-current restore.
  return powerAvailable() && routingIssue(i, now) == Issue::None && d && d->acState(now, staleMs()) != AcState::Absent &&
      u.fault == CommandState::Idle && u.zeroConfirmed &&
      (u.phase == ControlPhase::Prepared || u.phase == ControlPhase::Wake) &&
      u.fanAcknowledged == u.fanRequested && d && d->telemetry.broadcastSeen &&
      uint32_t(now - d->telemetry.lastBroadcast) <= staleMs() &&
      freshMetric(i, protocol::OutputCurrent, amps, now) && isfinite(amps) && fabsf(amps) <= control::stoppedCurrentAmps;
}
bool PsuController::releaseAllowed(uint32_t now) const {
  if (!powerAvailable() || availabilitySaving() || !participants() || !enable_.enabled() || !config_.operating.voltageAuthorized ||
      config_.operating.voltageMask != membersMask(count())) return false;
  for (uint8_t i = 0; i < count(); ++i) {
    if (!(requiredMembers() & (1U << i))) {
      if (availabilityReason(i, now) == AvailabilityReason::Fault) return false;
      continue;
    }
    if (excluded_ & (1U << i)) return false;
    const auto& u = control_[i];
    if (issue(i, now) != Issue::None || u.fault != CommandState::Idle || !(voltageSynced_ & (1U << i))) return false;
    if (u.phase != ControlPhase::Held && u.phase != ControlPhase::Online) return false;
    if (!u.awakeConfirmed) return false;
    if (u.phase != ControlPhase::Online && u.fanAcknowledged != u.fanRequested) return false;
    float amps;
    if (u.phase != ControlPhase::Online && (!u.zeroConfirmed || !freshMetric(i, protocol::OutputCurrent, amps, now) ||
        !isfinite(amps) || fabsf(amps) > control::stoppedCurrentAmps)) return false;
  }
  return true;
}
ControlStatus PsuController::controlStatus(uint32_t now) const {
  if (!powerAvailable()) return {Activity::StartBlocked, Issue::Cutoff, 0};
  if (!ready_) return {enable_.enabled() ? Activity::StartBlocked : Activity::StopUnconfirmed, Issue::Invalid, 0};
  ControlStatus s = {Activity::Reconciling, Issue::None, 0};
  bool stopped = true, held = true, online = true, starting = false;
  for (uint8_t i = 0; i < count(); ++i) {
    if (!(requiredMembers() & (1U << i)) && availabilityReason(i, now) != AvailabilityReason::Fault) continue;
    const auto& u = control_[i]; const Issue route = routingIssue(i, now);
    if ((excluded_ & (1U << i)) && s.blocker == Issue::None) { s.blocker = Issue::ApprovalRequired; s.slot = i; }
    const auto* device = deviceForSlot(i, now);
    if (device && device->acState(now, staleMs()) == AcState::Absent && s.blocker == Issue::None) { s.blocker = Issue::NoAc; s.slot = i; }
    float amps;
    stopped &= (u.zeroConfirmed || u.standbyConfirmed) && route == Issue::None && freshMetric(i, protocol::OutputCurrent, amps, now) &&
        isfinite(amps) && fabsf(amps) <= control::stoppedCurrentAmps;
    held &= u.phase == ControlPhase::Held && u.awakeConfirmed;
    online &= u.phase == ControlPhase::Online;
    starting |= u.phase == ControlPhase::Wake || u.phase == ControlPhase::Starting || u.outputDemand == OutputDemand::Starting;
    if (s.blocker == Issue::None && (route != Issue::None || u.fault != CommandState::Idle)) {
      s.blocker = route != Issue::None ? route : Issue::ControlFault; s.slot = i;
    }
    if (s.blocker == Issue::None && (u.phase == ControlPhase::Held || u.phase == ControlPhase::Prepared)) {
      const Issue why = issue(i, now);
      const bool inheritedStandby = u.phase == ControlPhase::Prepared && why == Issue::NotReady;
      if ((why != Issue::None && !inheritedStandby) || !freshMetric(i, protocol::OutputCurrent, amps, now)) {
        s.blocker = why != Issue::None ? why : Issue::NoTelemetry; s.slot = i;
      }
    }
  }
  if (!participants() && requiredMembers() == 0 && s.blocker == Issue::None)
    return {Activity::Holding, Issue::NoParticipants, 0};
  if (!enable_.enabled()) {
    s.activity = s.blocker != Issue::None ? (stopped ? Activity::StartBlocked : Activity::StopUnconfirmed) :
        stopped && held ? Activity::Holding : Activity::Stopping;
  } else if (s.blocker != Issue::None) s.activity = Activity::StartBlocked;
  else if (!config_.operating.voltageAuthorized || config_.operating.voltageMask != membersMask(count())) {
    s.activity = Activity::StartBlocked; s.blocker = Issue::NoProfile;
  } else if (online && !starting) s.activity = Activity::Running;
  else s.activity = starting ? Activity::Starting : Activity::Reconciling;
  return s;
}
void PsuController::controlCommand(uint8_t slot, uint8_t reg, uint16_t value, uint32_t now) {
  const auto* d = deviceForSlot(slot, now);
  if (!d || routingIssue(slot, now) != Issue::None) return;
  controlActive_ = true; controlWaiting_ = controlVerified_ = false;
  controlRefresh_ = false;
  controlSlot_ = slot; controlRegister_ = reg; controlValue_ = value;
  controlEpoch_ = d->epoch; controlStarted_ = now;
  auto& u = control_[slot];
  if (u.lastRegister != reg) u.attempts = 0;
  ++u.attempts; u.lastRegister = reg; u.last = CommandState::VerifyingIdentity;
  if (!send(protocol::request(d->address, protocol::infoCommand), TxPurpose::Control))
    controlComplete(CommandState::TransportError, now);
}
void PsuController::controlComplete(CommandState state, uint32_t now) {
  const bool refresh = controlRefresh_; controlRefresh_ = false;
  controlActive_ = controlWaiting_ = controlVerified_ = false;
  auto& u = control_[controlSlot_]; u.last = state;
  if (state != CommandState::Success) {
    if (refresh && state == CommandState::IdentityTimeout) {
      // No setting was sent. A supply disappearing during a background INFO
      // check is an availability change, not evidence that a stop failed.
      refreshed_[controlSlot_] = now; refreshCurrent_ = false; u.attempts = 0;
      return;
    }
    if (state == CommandState::Rejected || state == CommandState::Changed || u.attempts >= control::commandAttempts) {
      if (controlRegister_ == protocol::FanMode) u.fanFailed = true;
      if (u.fault == CommandState::Idle) latchControlFault(controlSlot_, state, controlRegister_, now);
      else controlPhase(controlSlot_, ControlPhase::Fault, now); // Keep the original cause while stopping/cooling after it.
    }
    return;
  }
  u.attempts = 0;
  if (controlRegister_ == protocol::OnlineCurrent) currentAccepted(controlSlot_, controlValue_);
  if (refresh) {
    if (controlRegister_ == protocol::OnlineVoltage) {
      if (controlValue_ == protocol::encodeVoltage(reconciliationVoltage())) voltageSynced_ |= 1U << controlSlot_;
      refreshCurrent_ = true; refreshSlot_ = controlSlot_;
    } else {
      if (controlValue_) currentSynced_ |= 1U << controlSlot_;
      refreshed_[controlSlot_] = now; refreshCurrent_ = false;
      refreshIndex_ = (controlSlot_ + 1) % count();
    }
    return;
  }
  if (controlRegister_ == protocol::Standby) {
    controlPhase(controlSlot_, controlValue_ ? ControlPhase::VerifyStandby : ControlPhase::Starting, now);
    u.zeroConfirmed = u.awakeConfirmed = u.standbyConfirmed = false;
    currentSynced_ &= uint8_t(~(1U << controlSlot_));
  } else if (controlRegister_ == protocol::FanMode) {
    u.fanAcknowledged = controlValue_ ? FanMode::Max : FanMode::Auto;
    if (u.phase == ControlPhase::Fan) controlPhase(controlSlot_, ControlPhase::Voltage, now);
  } else if (controlRegister_ == protocol::OnlineVoltage) {
    if (config_.operating.voltageAuthorized && controlValue_ == protocol::encodeVoltage(config_.operating.voltage))
      voltageSynced_ |= 1U << controlSlot_;
    controlPhase(controlSlot_, u.postReady && u.awakeConfirmed ? ControlPhase::Starting : ControlPhase::Prepared, now);
  } else if (controlRegister_ == protocol::OnlineCurrent) {
    u.zeroConfirmed = false; currentSynced_ &= uint8_t(~(1U << controlSlot_));
    controlPhase(controlSlot_, ControlPhase::Stopping, now);
  }
}
void PsuController::controlReceive(const CanFrame& f, uint32_t now) {
  if (!controlActive_) return;
  const auto* d = deviceForSlot(controlSlot_, now);
  if (!d || routingIssue(controlSlot_, now) != Issue::None || d->epoch != controlEpoch_ ||
      protocol::address(f.id) != d->address) return;
  if (!controlWaiting_ && protocol::isReply(f) && protocol::command(f.id) == protocol::infoCommand &&
      f.data[0] == 0 && f.data[1] == 2) controlVerified_ = true;
  if (controlWaiting_ && protocol::settingReply(f, controlRegister_, controlValue_))
    controlComplete(f.data[0] == 1 ? CommandState::Success : CommandState::Rejected, now);
}
void PsuController::controlTick(uint32_t now) {
  const uint32_t observation = uint32_t(config_.pollMs) * 3 > control::observationMs ? uint32_t(config_.pollMs) * 3 : control::observationMs;
  const uint32_t startupObservation = uint32_t(config_.pollMs) * 3 > control::startupReadyMs ? uint32_t(config_.pollMs) * 3 : control::startupReadyMs;
  for (uint8_t i = 0; i < count(); ++i) {
    auto& u = control_[i]; const auto* d = deviceForSlot(i, now);
    const uint32_t epoch = d && routingIssue(i, now) == Issue::None ? d->epoch : 0;
    if (observedEpoch_[i] != epoch) {
      const CommandState latched = startup_.permit == PermitState::Fault ? u.fault : CommandState::Idle;
      const uint8_t faultReg = u.faultRegister;
      u = {}; voltageSynced_ &= uint8_t(~(1U << i)); currentSynced_ &= uint8_t(~(1U << i));
      if (latched != CommandState::Idle) { u.fault = latched; u.faultRegister = faultReg; u.phase = ControlPhase::Fault; }
      if (running_ & (1U << i)) restorePending_ |= 1U << i;
      restoreBlocked_ &= uint8_t(~(1U << i)); observedEpoch_[i] = epoch;
      if (controlActive_ && controlSlot_ == i) controlActive_ = controlWaiting_ = controlVerified_ = false;
    }
    if (!epoch) { updateFan(i, now); continue; }
    if (u.phase == ControlPhase::AwaitIdentity) controlPhase(i, ControlPhase::Zero, now);
    if (u.phase == ControlPhase::Online && u.outputDemand == OutputDemand::Starting) {
      if ((currentSynced_ & (1U << i)) && d->telemetry.ready &&
          uint32_t(now - d->telemetry.lastBroadcast) <= staleMs() &&
          after(d->telemetry.lastBroadcast, u.outputSince, now)) u.outputDemand = OutputDemand::Active;
      else if (uint32_t(now - u.outputSince) >= startupObservation)
        latchControlFault(i, CommandState::ReadinessTimeout, protocol::OnlineCurrent, now);
    }
    if (u.phase == ControlPhase::Online && u.outputDemand == OutputDemand::Zero && !u.zeroConfirmed) {
      float amps;
      if (freshMetric(i, protocol::OutputCurrent, amps, now) && isfinite(amps) &&
          after(d->telemetry.updated[protocol::OutputCurrent], u.outputSince, now) &&
          fabsf(amps) <= control::stoppedCurrentAmps) u.zeroConfirmed = true;
      else if (uint32_t(now - u.outputSince) >= observation)
        latchControlFault(i, CommandState::ObservationTimeout, protocol::OnlineCurrent, now);
    }
    // Check a violated zero hold before readiness recovery can clear its proof.
    float heldCurrent;
    if ((u.zeroConfirmed || u.standbyConfirmed) && u.fault == CommandState::Idle && freshMetric(i, protocol::OutputCurrent, heldCurrent, now) &&
        isfinite(heldCurrent) && fabsf(heldCurrent) > control::stoppedCurrentAmps) {
      u.zeroConfirmed = u.standbyConfirmed = false;
      latchControlFault(i, CommandState::Changed, protocol::OnlineCurrent, now);
    }
    // Known AC transitions still invalidate the discovery epoch. Output-ready
    // alone may drop at a confirmed zero limit without restarting reconciliation.
    if ((u.phase == ControlPhase::Online || u.phase == ControlPhase::Held) &&
        d->telemetry.broadcastSeen && !readinessAcceptable(i, now)) {
      UnitControl next;
      preserveCooling(next, u, controlActive_ && controlWaiting_ && controlSlot_ == i && controlRegister_ == protocol::FanMode);
      u = next; controlPhase(i, ControlPhase::Zero, now);
      if (controlActive_ && controlSlot_ == i) controlActive_ = controlWaiting_ = controlVerified_ = false;
      voltageSynced_ &= uint8_t(~(1U << i)); currentSynced_ &= uint8_t(~(1U << i));
      restorePending_ |= running_ & (1U << i);
    }
    if (u.phase == ControlPhase::Stopping || u.phase == ControlPhase::Starting || u.phase == ControlPhase::VerifyStandby) {
      float amps;
      const bool observed = freshMetric(i, protocol::OutputCurrent, amps, now) && isfinite(amps) &&
          after(d->telemetry.updated[protocol::OutputCurrent], u.since, now);
      const bool zero = observed && fabsf(amps) <= control::stoppedCurrentAmps;
      // Some G2s remain not-ready at zero. A cold startup still gets its full
      // allowance before fresh benign status can stand in for output readiness.
      // The post-startup pass then reasserts zero/voltage without restarting wake.
      const bool idle = d->idleStatus(now, staleMs()) && after(d->telemetry.lastAlarm, u.since, now) &&
          (u.postReady || uint32_t(now - u.since) >= startupObservation);
      if (zero && (u.phase != ControlPhase::Starting || (d->acState(now, staleMs()) != AcState::Absent &&
          uint32_t(now - d->telemetry.lastBroadcast) <= staleMs() &&
          (d->telemetry.ready || idle) && after(d->telemetry.lastBroadcast, u.since, now)))) {
        if (u.phase == ControlPhase::VerifyStandby) {
          u.standbyConfirmed = true;
          controlPhase(i, ControlPhase::Fault, now);
        } else {
          u.zeroConfirmed = true;
          if (u.phase == ControlPhase::Starting) {
            u.awakeConfirmed = true;
            if (!u.postReady) {
              u.postReady = true; u.zeroConfirmed = false; u.fanAcknowledged = FanMode::Unknown;
              controlPhase(i, ControlPhase::Zero, now);
            } else {
              u.reconciled = true; refreshed_[i] = now; controlPhase(i, ControlPhase::Held, now);
            }
          } else controlPhase(i, u.awakeConfirmed && u.reconciled ? ControlPhase::Held : ControlPhase::Fan, now);
        }
      } else if ((d->acState(now, staleMs()) != AcState::Absent || (observed && !zero)) &&
          uint32_t(now - u.since) >= (u.phase == ControlPhase::Starting && (!observed || zero) ? startupObservation : observation)) {
        if (u.fault != CommandState::Idle) controlPhase(i, ControlPhase::Fault, now);
        else latchControlFault(i, CommandState::ObservationTimeout, protocol::OnlineCurrent, now);
      }
    }
    if (u.phase == ControlPhase::Held && config_.operating.voltageAuthorized && !(voltageSynced_ & (1U << i)))
      controlPhase(i, ControlPhase::Voltage, now);
    updateFan(i, now);
    if (u.phase == ControlPhase::Fan && u.fanAcknowledged == u.fanRequested) controlPhase(i, ControlPhase::Voltage, now);
    if (u.phase == ControlPhase::Prepared && u.awakeConfirmed) controlPhase(i, ControlPhase::Held, now);
  }
  if (controlActive_) {
    const auto* d = deviceForSlot(controlSlot_, now);
    if (!d || routingIssue(controlSlot_, now) != Issue::None || d->epoch != controlEpoch_) {
      controlComplete(CommandState::Changed, now); return;
    }
    if (uint32_t(now - controlStarted_) >= limits::ackTimeoutMs) {
      controlComplete(controlWaiting_ ? CommandState::Timeout : CommandState::IdentityTimeout, now); return;
    }
    if (!controlWaiting_ && controlVerified_ && !txPending_ && uint32_t(now - lastSend_) >= limits::commandGapMs) {
      if (controlRefresh_) {
        const uint16_t desired = controlRegister_ == protocol::OnlineVoltage ? protocol::encodeVoltage(reconciliationVoltage()) :
            protocol::encodeCurrent(enable_.enabled() && !(excluded_ & (1U << controlSlot_)) &&
                control_[controlSlot_].phase == ControlPhase::Online && (config_.operating.currentMask & (1U << controlSlot_)) ?
                config_.operating.current[controlSlot_] : 0, config_.units[controlSlot_].ratedCurrent);
        if (desired != controlValue_ || !powerAvailable() || (control_[controlSlot_].phase != ControlPhase::Online && control_[controlSlot_].phase != ControlPhase::Held)) {
          controlActive_ = controlRefresh_ = refreshCurrent_ = false; control_[controlSlot_].attempts = 0; return;
        }
      }
      // A temperature/current change may have overtaken a pending AUTO request.
      // Re-evaluate before transmission; an already transmitted command is
      // followed by MAX and cannot unlock a high-current job until that ACK.
      if (controlRegister_ == protocol::FanMode && bool(controlValue_) != (control_[controlSlot_].fanRequested == FanMode::Max)) {
        controlActive_ = false; control_[controlSlot_].attempts = 0; return;
      }
      // A wake clears hardware standby only after this unit has accepted zero.
      if (controlRegister_ == protocol::Standby && !controlValue_ && !wakeAllowed(controlSlot_, now)) {
        controlActive_ = false; control_[controlSlot_].attempts = 0;
        controlPhase(controlSlot_, ControlPhase::Prepared, now); return;
      }
      const auto frame = controlRegister_ == protocol::Standby || controlRegister_ == protocol::FanMode ?
          protocol::toggle(d->address, controlRegister_, controlValue_ != 0) : protocol::setting(d->address, controlRegister_, controlValue_);
      auto& u = control_[controlSlot_]; u.last = CommandState::Waiting;
      if (controlRegister_ == protocol::Standby) u.zeroConfirmed = u.awakeConfirmed = u.standbyConfirmed = false;
      controlStarted_ = lastSend_ = now; controlWaiting_ = true;
      if (!send(frame, TxPurpose::Control)) controlComplete(CommandState::TransportError, now);
      else if (controlRegister_ == protocol::OnlineCurrent) currentSent(controlSlot_, controlValue_, now);
    }
    return;
  }
  if (txPending_ || waiting_ || verifying_ || sentThisTick_ || uint32_t(now - lastSend_) < limits::commandGapMs) return;
  // Fault fallback takes priority; normal stops never send standby ON.
  for (uint8_t i = 0; i < count(); ++i) if (control_[i].phase == ControlPhase::EmergencyStandby && routingIssue(i, now) == Issue::None) {
    controlCommand(i, protocol::Standby, 1, now); return;
  }
  // Zero all reachable managed units before voltage/wake preparation.
  for (uint8_t i = 0; i < count(); ++i) if (control_[i].phase == ControlPhase::Zero && routingIssue(i, now) == Issue::None) {
    controlCommand(i, protocol::OnlineCurrent, 0, now); return;
  }
  for (uint8_t i = 0; i < count(); ++i) {
    const auto& u = control_[i];
    if (routingIssue(i, now) != Issue::None || u.fanFailed) continue;
    // Cooling continues through every phase, including an output-control fault.
    // Only a latched fan-command failure suppresses further fan writes.
    if (u.fanRequested != u.fanAcknowledged) {
      controlCommand(i, protocol::FanMode, u.fanRequested == FanMode::Max, now); return;
    }
  }
  for (uint8_t i = 0; i < count(); ++i) {
    if (routingIssue(i, now) != Issue::None || control_[i].fault != CommandState::Idle) continue;
    if (control_[i].phase == ControlPhase::Voltage) {
      controlCommand(i, protocol::OnlineVoltage, protocol::encodeVoltage(reconciliationVoltage()), now); return;
    }
  }
  for (uint8_t i = 0; i < count(); ++i) if (control_[i].phase == ControlPhase::Prepared && wakeAllowed(i, now)) {
    controlPhase(i, ControlPhase::Wake, now); controlCommand(i, protocol::Standby, 0, now); return;
  }
  if (releaseAllowed(now)) for (uint8_t i = 0; i < count(); ++i) if ((participants() & (1U << i)) && control_[i].phase == ControlPhase::Held) {
    // Still physically at the accepted zero limit until an actual current write.
    controlPhase(i, ControlPhase::Online, now);
  }
}
void PsuController::refreshSettings(uint32_t now) {
  if (!powerAvailable() || busy() || controlActive_ || txPending_ || sentThisTick_ || discovery_.negotiating() ||
      uint32_t(now - lastSend_) < limits::commandGapMs) return;
  for (uint8_t n = 0; n < count(); ++n) {
    const uint8_t i = refreshCurrent_ ? refreshSlot_ : uint8_t((refreshIndex_ + n) % count());
    const auto* d = deviceForSlot(i, now); const auto& u = control_[i];
    if ((restoreBlocked_ & (1U << i)) || issue(i, now) != Issue::None ||
        u.outputDemand == OutputDemand::Starting || u.outputDemand == OutputDemand::Stopping ||
        (u.phase != ControlPhase::Held && u.phase != ControlPhase::Online)) {
      if (refreshCurrent_) { refreshCurrent_ = false; return; }
      continue;
    }
    if (refreshCurrent_ && d->epoch != refreshEpoch_) { refreshCurrent_ = false; return; }
    if (!refreshCurrent_ && uint32_t(now - refreshed_[i]) < control::volatileRefreshMs) continue;
    const uint16_t current = enable_.enabled() && !(excluded_ & (1U << i)) && u.phase == ControlPhase::Online &&
        (config_.operating.currentMask & (1U << i)) ? config_.operating.current[i] : 0;
    if (refreshCurrent_ && current >= control::fanOutputCurrentThreshold && u.fanAcknowledged != FanMode::Max) return;
    if (refreshCurrent_ && config_.operating.voltageAuthorized && !(voltageSynced_ & (1U << i))) { refreshCurrent_ = false; return; }
    if (!refreshCurrent_) { refreshEpoch_ = d->epoch; refreshSlot_ = i; }
    controlCommand(i, refreshCurrent_ ? protocol::OnlineCurrent : protocol::OnlineVoltage,
        refreshCurrent_ ? protocol::encodeCurrent(current, config_.units[i].ratedCurrent) : protocol::encodeVoltage(reconciliationVoltage()), now);
    controlRefresh_ = controlActive_; return;
  }
}
}
