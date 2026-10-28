#include "PsuController.h"
#include "../config/Deployment.h"
namespace psu {
void PsuController::configureStartup(bool installed, bool trusted, uint32_t now) {
  startupConfigured_ = true; now_ = acReleased_ = now;
  startup_.permit = installed ? PermitState::Inhibited : PermitState::NotFitted;
  startup_.configurationReady = trusted; startup_.commissioning = false; startup_.since = now;
}
void PsuController::startupTick(uint32_t now) {
  if (startup_.permit == PermitState::Allowed && config_.deploymentId != deployment::id) hardStop();
  if (startup_.permit == PermitState::Inhibited && ready_ && startup_.configurationReady &&
      config_.deploymentId == deployment::id && uint32_t(now - startup_.since) >= control::inhibitMs) {
    startup_.permit = PermitState::Allowed; startup_.since = acReleased_ = now;
    // AC return is a new hardware state even when DC keeps CAN alive.
    reconcile();
  }
}
Result PsuController::hardStop() {
  if (startup_.permit == PermitState::NotFitted) return Result::Invalid;
  if (startup_.permit != PermitState::Fault) startup_.since = now_;
  startup_.permit = PermitState::Fault;
  cancelJob(); controlActive_ = controlWaiting_ = controlVerified_ = false;
  voltageSynced_ = currentSynced_ = 0; restorePending_ |= running_;
  for (auto& u : control_) {
    u.zeroConfirmed = u.awakeConfirmed = u.postReady = false;
    if (u.fault == CommandState::Idle) u.phase = ControlPhase::Zero;
  }
  ++availabilityRevision_;
  return Result::Ok;
}
AvailabilityReason PsuController::availabilityReason(uint8_t slot, uint32_t now) const {
  if (slot >= count()) return AvailabilityReason::Fault;
  if (control_[slot].fault != CommandState::Idle || control_[slot].fanFailed) return AvailabilityReason::Fault;
  const Issue route = routingIssue(slot, now);
  if (route == Issue::Missing) return AvailabilityReason::NoCan;
  if (route == Issue::IdentityPending) return AvailabilityReason::Reconciling;
  if (route != Issue::None || groupFor(config_, slot) < 0) return AvailabilityReason::Fault;
  const auto* d = deviceForSlot(slot, now);
  if (d->acState(now, staleMs()) == AcState::Absent) return AvailabilityReason::NoAc;
  if (issue(slot, now) != Issue::None) return AvailabilityReason::Reconciling;
  const auto& u = control_[slot];
  return (u.phase == ControlPhase::Held && u.zeroConfirmed && u.awakeConfirmed) || u.phase == ControlPhase::Online ?
      AvailabilityReason::Ready : AvailabilityReason::Reconciling;
}
bool PsuController::availabilitySettled(uint8_t group, uint32_t now) const {
  if (group >= limits::maxGroups || !config_.groups[group].members || discovery_.negotiating()) return false;
  const uint32_t interval = uint32_t(config_.pollMs) * 2 > control::availabilitySettleMs ?
      uint32_t(config_.pollMs) * 2 : control::availabilitySettleMs;
  return uint32_t(now - groupChanged_[group]) >= interval;
}
bool PsuController::availabilityUnstable(uint8_t group, uint32_t now) const {
  return group < limits::maxGroups && !availabilitySettled(group, now) &&
      uint32_t(now - groupEpisode_[group]) >= control::unstableMs;
}
uint8_t PsuController::availabilityAttention(uint32_t now) const {
  if (!powerAvailable() || (startupConfigured_ && uint32_t(now - acReleased_) < control::availabilityGraceMs)) return 0;
  uint8_t attention = 0;
  for (uint8_t i = 0; i < count(); ++i) {
    const int8_t g = groupFor(config_, i);
    if (g < 0 || !availabilitySettled(g, now)) continue;
    const auto reason = availabilityReason(i, now); const uint8_t bit = uint8_t(1U << i);
    if (((reason == AvailabilityReason::NoCan || reason == AvailabilityReason::NoAc) && !(acceptedAbsences() & bit)) ||
        (reason == AvailabilityReason::Ready && (excluded_ & bit))) attention |= bit;
  }
  return attention;
}
void PsuController::availabilityTick(uint32_t now) {
  for (uint8_t g = 0; g < limits::maxGroups; ++g)
    if (availabilitySettled(g, now)) groupEpisode_[g] = now;
  for (uint8_t i = 0; i < count(); ++i) {
    const auto reason = availabilityReason(i, now);
    if (reason != availabilityReasons_[i]) {
      availabilityReasons_[i] = reason; ++availabilityRevision_;
      const int8_t g = groupFor(config_, i);
      if (g >= 0) groupChanged_[g] = now;
    }
    const auto bit = uint8_t(1U << i);
    if ((acceptedAbsences() & bit) && reason != AvailabilityReason::Ready && !(excluded_ & bit)) {
      excluded_ |= bit; ++availabilityRevision_;
    }
  }
}
AvailabilityPlan PsuController::previewAvailability(AvailabilityAction action, uint8_t mask, bool remember, uint32_t now) const {
  AvailabilityPlan p;
  p.action = action; p.requested = mask; p.remember = remember; p.created = now;
  p.configurationRevision = revision_; p.topologyRevision = discovery_.revision();
  p.availabilityRevision = availabilityRevision_; p.participants = participants();
  if (busy() || availabilitySaving()) p.blocker = Issue::Busy;
  if (config_.deploymentId != deployment::id) p.blocker = Issue::DeploymentMismatch;
  if (action == AvailabilityAction::Probe) {
    if (startup_.permit == PermitState::NotFitted || !ready_ || remember) p.blocker = Issue::Invalid;
    return p;
  }
  if (!mask || (mask & ~membersMask(count())) || (action == AvailabilityAction::Include && remember)) {
    p.blocker = Issue::Invalid; return p;
  }
  if (!powerAvailable() || discovery_.negotiating()) p.blocker = Issue::Cutoff;
  if (action == AvailabilityAction::Accept && startupConfigured_ &&
      uint32_t(now - acReleased_) < control::availabilityGraceMs) p.blocker = Issue::Settling;
  for (uint8_t i = 0; i < count(); ++i) if (mask & (1U << i)) {
    const uint8_t bit = uint8_t(1U << i);
    const int8_t g = groupFor(config_, i);
    if (!identified(config_.units[i].identity) || g < 0) { p.blocker = Issue::Unbound; continue; }
    if (action == AvailabilityAction::Require) {
      p.selected |= bit; continue;
    }
    const auto reason = availabilityReason(i, now);
    if (reason == AvailabilityReason::Fault) { p.blocker = Issue::ControlFault; continue; }
    if (action == AvailabilityAction::Accept) {
      if (reason != AvailabilityReason::NoCan && reason != AvailabilityReason::NoAc) continue;
    } else {
      if (!(excluded_ & bit)) continue;
      if (reason != AvailabilityReason::Ready || control_[i].phase != ControlPhase::Held) {
        p.blocker = Issue::Reconciling; continue;
      }
    }
    if (!availabilitySettled(g, now)) p.blocker = Issue::Settling;
    p.selected |= bit;
  }
  if (!p.selected && p.blocker == Issue::None) p.blocker = Issue::Invalid;
  if (action == AvailabilityAction::Accept) p.participants &= uint8_t(~p.selected);
  if (action == AvailabilityAction::Include) p.participants |= p.selected;
  return p;
}
void PsuController::applyAvailability(const AvailabilityPlan& p) {
  if (p.action == AvailabilityAction::Accept) {
    sessionAccepted_ |= p.selected; sessionRequired_ &= uint8_t(~p.selected); excluded_ |= p.selected;
    if (p.remember) config_.acceptedAbsent |= p.selected;
  } else if (p.action == AvailabilityAction::Include) {
    excluded_ &= uint8_t(~p.selected);
    restorePending_ |= p.selected & config_.operating.voltageMask;
    running_ |= p.selected & config_.operating.voltageMask;
  } else if (p.action == AvailabilityAction::Require) {
    sessionRequired_ |= p.selected; sessionAccepted_ &= uint8_t(~p.selected);
    if (p.remember) config_.acceptedAbsent &= uint8_t(~p.selected);
    // Revoking a waiver never implicitly admits a currently excluded unit.
  } else {
    setAutoResume(false);
    startup_.configurationReady = true; startup_.commissioning = true;
    startup_.permit = PermitState::Inhibited; startup_.since = now_;
    reconcile();
  }
  ++revision_; ++availabilityRevision_;
}
Result PsuController::confirmAvailability(const AvailabilityPlan& p, MemoryManager& memory, uint32_t now) {
  if (availabilitySaving() || busy()) return Result::Busy;
  if (p.configurationRevision != revision_ || p.topologyRevision != discovery_.revision() ||
      p.availabilityRevision != availabilityRevision_ || uint32_t(now - p.created) > limits::confirmationMs) return Result::Changed;
  const auto fresh = previewAvailability(p.action, p.requested, p.remember, now);
  if (fresh.blocker != Issue::None) return Result::Blocked;
  if (p.selected != fresh.selected || p.participants != fresh.participants) return Result::Changed;
  ++availabilityReport_.sequence; availabilityReport_.remembered = false;
  availabilityReport_.storage = StorageResult::Ok; availabilityReport_.result = Result::Ok;
  if (!p.remember) { applyAvailability(p); return Result::Ok; }
  Configuration snapshot = config_;
  if (p.action == AvailabilityAction::Accept) snapshot.acceptedAbsent |= p.selected;
  else snapshot.acceptedAbsent &= uint8_t(~p.selected);
  availabilityReport_.storage = memory.startSave(snapshot);
  if (availabilityReport_.storage != StorageResult::Ok) {
    availabilityReport_.result = Result::Blocked; return Result::Blocked;
  }
  availabilityMemory_ = &memory; availabilitySaveToken_ = memory.saveToken();
  savedAvailability_ = p; availabilityReport_.saving = true;
  return Result::Ok;
}
void PsuController::finishAvailability(uint32_t now) {
  if (!availabilitySaving() || !availabilityMemory_ || availabilityMemory_->completedToken() != availabilitySaveToken_) return;
  availabilityReport_.saving = false;
  availabilityReport_.storage = availabilityMemory_->completedResult();
  if (availabilityReport_.storage != StorageResult::Ok) { availabilityReport_.result = Result::Blocked; return; }
  availabilityReport_.remembered = true;
  const auto& p = savedAvailability_;
  const auto fresh = previewAvailability(p.action, p.requested, p.remember, now);
  if (p.configurationRevision != revision_ || p.topologyRevision != discovery_.revision() ||
      p.availabilityRevision != availabilityRevision_ || uint32_t(now - p.created) > limits::confirmationMs ||
      fresh.blocker != Issue::None || fresh.selected != p.selected || fresh.participants != p.participants) {
    availabilityReport_.result = Result::Changed; return;
  }
  applyAvailability(p);
}
}
