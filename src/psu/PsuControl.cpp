#include "PsuController.h"
#include "FanPolicy.h"
#include "../config/Deployment.h"
#include <math.h>
namespace psu {
namespace {
bool after(uint32_t sample, uint32_t boundary, uint32_t now) {
  return uint32_t(now - sample) < uint32_t(now - boundary);
}
}
Issue PsuController::routingIssue(uint8_t i, uint32_t now) const {
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
  // Cooling has its own lifetime. A software stop/retry is not a new physical
  // PSU: preserve its hysteresis and the accepted persistent fan mode.
  const int8_t uncertainFan = controlActive_ && controlWaiting_ && controlRegister_ == protocol::FanMode ? controlSlot_ : -1;
  cancelJob(); controlActive_ = controlWaiting_ = controlVerified_ = false;
  voltageSynced_ = currentSynced_ = restoreBlocked_ = 0;
  restorePending_ = running_;
  for (uint8_t i = 0; i < count(); ++i) {
    auto& u = control_[i];
    UnitControl next;
    next.coolSince = u.coolSince; next.cooling = u.cooling;
    next.fanRequested = u.fanRequested; next.fanReason = u.fanReason;
    next.fanAcknowledged = u.fanFailed || i == uncertainFan ? FanMode::Unknown : u.fanAcknowledged;
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
    running_ = config_.operating.voltageMask;
    restorePending_ |= running_;
  }
}
Result PsuController::setAutoResume(bool on) {
  const bool was = enable_.enabled(); enable_.request(on, EnableSource::Serial);
  enableChanged(was); return Result::Ok;
}
void PsuController::sampleEnableInput(bool active, uint32_t now) {
  const bool was = enable_.enabled();
  if (enable_.sample(active, now)) enableChanged(was);
}
uint16_t PsuController::reconciliationVoltage() const {
  return config_.operating.voltageAuthorized ? config_.operating.voltage : deployment::voltage;
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
}
void PsuController::updateFan(uint8_t i, uint32_t now) {
  auto& u = control_[i]; float temperature = 0, amps = 0;
  const bool data = freshMetric(i, protocol::OutputTemperature, temperature, now) && isfinite(temperature) &&
      temperature >= control::minimumTemperature && temperature <= control::maximumTemperature &&
      freshMetric(i, protocol::OutputCurrent, amps, now) && isfinite(amps) && amps >= 0;
  const uint16_t requested = !enable_.enabled() && (u.zeroConfirmed || u.standbyConfirmed) ? 0 :
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
    if (control_[i].phase != ControlPhase::Online || control_[i].fault != CommandState::Idle) return false;
  return true;
}
bool PsuController::wakeAllowed(uint8_t i, uint32_t now) const {
  const auto& u = control_[i]; const auto* d = deviceForSlot(i, now); float amps;
  // Wake at zero is per unit and independent of enable/group completeness.
  // The installation-wide barrier still guards every positive-current restore.
  return routingIssue(i, now) == Issue::None && u.fault == CommandState::Idle && u.zeroConfirmed &&
      (u.phase == ControlPhase::Prepared || u.phase == ControlPhase::Wake) &&
      u.fanAcknowledged == u.fanRequested && d && d->telemetry.broadcastSeen &&
      uint32_t(now - d->telemetry.lastBroadcast) <= staleMs() &&
      freshMetric(i, protocol::OutputCurrent, amps, now) && isfinite(amps) && fabsf(amps) <= control::stoppedCurrentAmps;
}
bool PsuController::releaseAllowed(uint32_t now) const {
  if (!enable_.enabled() || !config_.operating.voltageAuthorized ||
      config_.operating.voltageMask != membersMask(count())) return false;
  for (uint8_t i = 0; i < count(); ++i) {
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
  if (!ready_) return {enable_.enabled() ? Activity::StartBlocked : Activity::StopUnconfirmed, Issue::Invalid, 0};
  ControlStatus s = {Activity::Reconciling, Issue::None, 0};
  bool stopped = true, held = true, online = true, starting = false;
  for (uint8_t i = 0; i < count(); ++i) {
    const auto& u = control_[i]; const Issue route = routingIssue(i, now);
    float amps;
    stopped &= (u.zeroConfirmed || u.standbyConfirmed) && route == Issue::None && freshMetric(i, protocol::OutputCurrent, amps, now) &&
        isfinite(amps) && fabsf(amps) <= control::stoppedCurrentAmps;
    held &= u.phase == ControlPhase::Held && u.awakeConfirmed;
    online &= u.phase == ControlPhase::Online;
    starting |= u.phase == ControlPhase::Wake || u.phase == ControlPhase::Starting;
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
  if (!enable_.enabled()) {
    s.activity = s.blocker != Issue::None ? (stopped ? Activity::StartBlocked : Activity::StopUnconfirmed) :
        stopped && held ? Activity::Holding : Activity::Stopping;
  } else if (s.blocker != Issue::None) s.activity = Activity::StartBlocked;
  else if (!config_.operating.voltageAuthorized || config_.operating.voltageMask != membersMask(count())) {
    s.activity = Activity::StartBlocked; s.blocker = Issue::NoProfile;
  } else if (online) s.activity = Activity::Running;
  else s.activity = starting ? Activity::Starting : Activity::Reconciling;
  return s;
}
void PsuController::controlCommand(uint8_t slot, uint8_t reg, uint16_t value, uint32_t now) {
  const auto* d = deviceForSlot(slot, now);
  if (!d || routingIssue(slot, now) != Issue::None) return;
  controlActive_ = true; controlWaiting_ = controlVerified_ = false;
  controlSlot_ = slot; controlRegister_ = reg; controlValue_ = value;
  controlEpoch_ = d->epoch; controlStarted_ = now;
  auto& u = control_[slot];
  if (u.lastRegister != reg) u.attempts = 0;
  ++u.attempts; u.lastRegister = reg; u.last = CommandState::VerifyingIdentity;
  if (!send(protocol::request(d->address, protocol::infoCommand), TxPurpose::Control))
    controlComplete(CommandState::TransportError, now);
}
void PsuController::controlComplete(CommandState state, uint32_t now) {
  controlActive_ = controlWaiting_ = controlVerified_ = false;
  auto& u = control_[controlSlot_]; u.last = state;
  if (state != CommandState::Success) {
    if (state == CommandState::Rejected || state == CommandState::Changed || u.attempts >= control::commandAttempts) {
      if (controlRegister_ == protocol::FanMode) u.fanFailed = true;
      if (u.fault == CommandState::Idle) latchControlFault(controlSlot_, state, controlRegister_, now);
      else controlPhase(controlSlot_, ControlPhase::Fault, now); // Keep the original cause while stopping/cooling after it.
    }
    return;
  }
  u.attempts = 0;
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
    controlPhase(controlSlot_, ControlPhase::Prepared, now);
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
  for (uint8_t i = 0; i < count(); ++i) {
    auto& u = control_[i]; const auto* d = deviceForSlot(i, now);
    const uint32_t epoch = d && routingIssue(i, now) == Issue::None ? d->epoch : 0;
    if (observedEpoch_[i] != epoch) {
      u = {}; voltageSynced_ &= uint8_t(~(1U << i)); currentSynced_ &= uint8_t(~(1U << i));
      if (running_ & (1U << i)) restorePending_ |= 1U << i;
      restoreBlocked_ &= uint8_t(~(1U << i)); observedEpoch_[i] = epoch;
      if (controlActive_ && controlSlot_ == i) controlActive_ = controlWaiting_ = controlVerified_ = false;
    }
    if (!epoch) { updateFan(i, now); continue; }
    if (u.phase == ControlPhase::AwaitIdentity) controlPhase(i, ControlPhase::Zero, now);
    // An unexpected AC/readiness loss invalidates volatile settings even if CAN
    // stays powered from the DC bus, including while held awake at zero.
    if ((u.phase == ControlPhase::Online || u.phase == ControlPhase::Held) &&
        d->telemetry.broadcastSeen && !d->telemetry.ready) {
      u = {}; controlPhase(i, ControlPhase::Zero, now);
      if (controlActive_ && controlSlot_ == i) controlActive_ = controlWaiting_ = controlVerified_ = false;
      voltageSynced_ &= uint8_t(~(1U << i)); currentSynced_ &= uint8_t(~(1U << i));
      restorePending_ |= running_ & (1U << i);
    }
    float heldCurrent;
    if ((u.zeroConfirmed || u.standbyConfirmed) && u.fault == CommandState::Idle && freshMetric(i, protocol::OutputCurrent, heldCurrent, now) &&
        isfinite(heldCurrent) && fabsf(heldCurrent) > control::stoppedCurrentAmps) {
      u.zeroConfirmed = u.standbyConfirmed = false;
      latchControlFault(i, CommandState::Changed, protocol::OnlineCurrent, now);
    }
    if (u.phase == ControlPhase::Stopping || u.phase == ControlPhase::Starting || u.phase == ControlPhase::VerifyStandby) {
      float amps;
      const bool observed = freshMetric(i, protocol::OutputCurrent, amps, now) && isfinite(amps) &&
          after(d->telemetry.updated[protocol::OutputCurrent], u.since, now);
      const bool zero = observed && fabsf(amps) <= control::stoppedCurrentAmps;
      if (zero && (u.phase != ControlPhase::Starting || (d->telemetry.ready && after(d->telemetry.lastBroadcast, u.since, now)))) {
        if (u.phase == ControlPhase::VerifyStandby) {
          u.standbyConfirmed = true;
          controlPhase(i, ControlPhase::Fault, now);
        } else {
          u.zeroConfirmed = true;
          if (u.phase == ControlPhase::Starting) u.awakeConfirmed = true;
          controlPhase(i, u.awakeConfirmed ? ControlPhase::Held : ControlPhase::Fan, now);
        }
      } else if (uint32_t(now - u.since) >= observation) {
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
  if (releaseAllowed(now)) for (uint8_t i = 0; i < count(); ++i) if (control_[i].phase == ControlPhase::Held) {
    control_[i].zeroConfirmed = false; controlPhase(i, ControlPhase::Online, now);
  }
}
}
