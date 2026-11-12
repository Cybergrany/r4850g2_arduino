#include "PsuController.h"
#include "../config/Deployment.h"
namespace psu {
void PsuController::configureStartup(bool installed, bool trusted, uint32_t now) {
  startupConfigured_ = true; now_ = acReleased_ = now;
  startup_.permit = installed ? PermitState::Inhibited : PermitState::NotFitted;
  startup_.configurationReady = trusted; startup_.commissioning = false; startup_.since = now;
#if PSU_BUS_PROFILE
  busProfile_.note(BusEvent::Permit,0,uint8_t(startup_.permit),trusted,now);
#endif
}
void PsuController::startupTick(uint32_t now) {
  if (startup_.permit == PermitState::Allowed && config_.deploymentId != deployment::id) hardStop();
  if (startup_.permit == PermitState::Inhibited && ready_ && startup_.configurationReady &&
      config_.deploymentId == deployment::id && uint32_t(now - startup_.since) >= control::inhibitMs) {
    startup_.permit = PermitState::Allowed; startup_.since = acReleased_ = now;
#if PSU_BUS_PROFILE
    busProfile_.note(BusEvent::Permit,0,uint8_t(startup_.permit),0,now);
#endif
    // AC return is a new hardware state even when DC keeps CAN alive.
    reconcile();
  }
}
Result PsuController::hardStop() {
  if (startup_.permit == PermitState::NotFitted) return Result::Invalid;
  if (startup_.permit != PermitState::Fault) startup_.since = now_;
  startup_.permit = PermitState::Fault;
#if PSU_BUS_PROFILE
  busProfile_.note(BusEvent::Permit,0,uint8_t(startup_.permit),0,now_);
#endif
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
UnitMask PsuController::availabilityAttention(uint32_t now) const {
  if (!powerAvailable() || (startupConfigured_ && uint32_t(now - acReleased_) < control::availabilityGraceMs)) return 0;
  UnitMask attention = 0;
  for (uint8_t i = 0; i < count(); ++i) {
    const int8_t g = groupFor(config_, i);
    if (g < 0 || !availabilitySettled(g, now)) continue;
    const auto reason = availabilityReason(i, now); const UnitMask bit = UnitMask(1U << i);
    if (((reason == AvailabilityReason::NoCan || reason == AvailabilityReason::NoAc) && !(acceptedAbsences() & bit)) ||
        (reason == AvailabilityReason::Ready && (excluded_ & bit) && !(deliberateExclusions() & bit))) attention |= bit;
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
    const auto bit = UnitMask(1U << i);
    if ((acceptedAbsences() & bit) && reason != AvailabilityReason::Ready && !(excluded_ & bit)) {
      excluded_ |= bit; ++availabilityRevision_;
    }
  }
}
ParticipationStatus PsuController::participationStatus(uint8_t slot) const {
  const UnitMask bit = UnitMask(1U << slot);
  const bool excluded = excluded_ & bit;
  const bool legacy = excluded && !(deliberateExclusions() & bit);
  const bool remembered = excluded ? (config_.disabled & bit) != 0 : ((config_.disabled | config_.acceptedAbsent) & bit) == 0;
  return {excluded, remembered, legacy};
}
AvailabilityStatus PsuController::availabilityStatus(uint8_t group, uint32_t now) const {
  AvailabilityStatus s;
  if (group >= limits::maxGroups || !config_.groups[group].members) { s.state = AvailabilityState::Review; return s; }
  bool fault = !ready_ || !startup_.configurationReady || startup_.permit == PermitState::Fault ||
      config_.thermalStop != ThermalStop::None || config_.deploymentId != deployment::id;
  bool reconciling = !powerAvailable();
  const auto members = config_.groups[group].members;
  for (uint8_t i = 0; i < count(); ++i) if (members & (1U << i)) {
    const auto reason = availabilityReason(i, now);
    s.ready += reason == AvailabilityReason::Ready;
    s.absent += reason == AvailabilityReason::NoCan || reason == AvailabilityReason::NoAc;
    const auto incident = alerts_.at(i);
    fault |= (incident.active && alertLevel(incident.code) == AlertLevel::Error) || reason == AvailabilityReason::Fault ||
        (enable_.enabled() && (requiredMembers() & (1U << i)) && !(config_.operating.voltageMask & (1U << i)));
    reconciling |= reason == AvailabilityReason::Reconciling;
  }
  if (fault || (availabilityAttention(now) & members) || availabilityUnstable(group, now)) s.state = AvailabilityState::Review;
  else if (!reconciling && availabilitySettled(group, now) &&
      (!startupConfigured_ || uint32_t(now - acReleased_) >= control::availabilityGraceMs)) s.state = AvailabilityState::Settled;
  return s;
}
AvailabilityPlan PsuController::previewParticipation(UnitMask mask, UnitMask excluded, bool remember, uint32_t now) const {
  AvailabilityPlan p;
  p.action = AvailabilityAction::Selection; p.requested = p.selected = mask; p.remember = remember; p.created = now;
  p.configurationRevision = revision_; p.topologyRevision = discovery_.revision(); p.availabilityRevision = availabilityRevision_;
  p.participants = (participants() & UnitMask(~mask)) | (mask & UnitMask(~excluded));
  if (!mask || (mask & ~membersMask(count())) || (excluded & ~mask)) { p.blocker = Issue::Invalid; return p; }
  if (busy() || availabilitySaving()) p.blocker = Issue::Busy;
  if (config_.deploymentId != deployment::id) p.blocker = Issue::DeploymentMismatch;
  if (!ready_) p.blocker = Issue::Invalid;
  if (!powerAvailable() || discovery_.negotiating()) p.blocker = Issue::Cutoff;
  if (startupConfigured_ && uint32_t(now - acReleased_) < control::availabilityGraceMs) p.blocker = Issue::Settling;
  for (uint8_t i = 0; i < count(); ++i) if (mask & (1U << i)) {
    const UnitMask bit = UnitMask(1U << i); const int8_t g = groupFor(config_, i);
    if (!identified(config_.units[i].identity) || g < 0) { p.blocker = Issue::Unbound; continue; }
    const auto reason = availabilityReason(i, now);
    if (reason == AvailabilityReason::Fault) { p.blocker = Issue::ControlFault; continue; }
    if (!availabilitySettled(g, now)) p.blocker = Issue::Settling;
    // An unchanged live participant need not stop just because a missing peer
    // is explicitly excluded. Any changed available participant must be held.
    if (((excluded ^ excluded_) & bit) && reason != AvailabilityReason::NoCan && reason != AvailabilityReason::NoAc) {
      if (enable_.enabled()) p.blocker = Issue::ParticipationHold;
      else if (reason != AvailabilityReason::Ready || control_[i].phase != ControlPhase::Held ||
          !control_[i].zeroConfirmed || !control_[i].awakeConfirmed) p.blocker = Issue::Reconciling;
    } else if (reason == AvailabilityReason::Reconciling) p.blocker = Issue::Reconciling;
  }
  return p;
}
AvailabilityPlan PsuController::previewAvailability(AvailabilityAction action, UnitMask mask, bool remember, uint32_t now) const {
  if (action == AvailabilityAction::Probe) {
    AvailabilityPlan p; p.action = action; p.created = now; p.remember = remember;
    p.configurationRevision = revision_; p.topologyRevision = discovery_.revision(); p.availabilityRevision = availabilityRevision_;
    p.participants = participants();
    if (busy() || availabilitySaving()) p.blocker = Issue::Busy;
    if (config_.deploymentId != deployment::id) p.blocker = Issue::DeploymentMismatch;
    if (startup_.permit == PermitState::NotFitted || !ready_ || remember) p.blocker = Issue::Invalid;
    return p;
  }
  UnitMask selected = mask;
  if (action == AvailabilityAction::Accept) {
    selected = 0;
    for (uint8_t i = 0; i < count(); ++i) if (mask & (1U << i)) {
      const auto reason = availabilityReason(i, now);
      if (reason == AvailabilityReason::NoCan || reason == AvailabilityReason::NoAc) selected |= UnitMask(1U << i);
    }
  }
  auto p = previewParticipation(selected, action == AvailabilityAction::Accept || action == AvailabilityAction::Exclude ? selected : 0, remember, now);
  p.action = action; p.requested = mask;
  if (!mask || (mask & ~membersMask(count())) || action == AvailabilityAction::Selection) p.blocker = Issue::Invalid;
  // Bulk acceptance must not silently skip a fault or ambiguous identity.
  if (action == AvailabilityAction::Accept) for (uint8_t i = 0; i < count(); ++i) if (mask & (1U << i)) {
    if (!identified(config_.units[i].identity) || groupFor(config_, i) < 0) p.blocker = Issue::Unbound;
    else if (availabilityReason(i, now) == AvailabilityReason::Fault) p.blocker = Issue::ControlFault;
  }
  return p;
}
void PsuController::applyParticipation(UnitMask mask, UnitMask excluded, bool remember, bool releaseHot, uint32_t now) {
  const UnitMask included = mask & UnitMask(~excluded);
  const UnitMask added = included & excluded_;
  if (releaseHot) for (uint8_t i = 0; i < count(); ++i) if (excluded & (1U << i)) {
    const auto reason = availabilityReason(i, now);
    if (reason == AvailabilityReason::NoCan || reason == AvailabilityReason::NoAc) {
      thermalIgnored_ |= UnitMask(1U << i); hotContributors_ &= UnitMask(~(1U << i));
    }
  }
  excluded_ = (excluded_ & UnitMask(~mask)) | excluded;
  sessionAccepted_ = (sessionAccepted_ & UnitMask(~mask)) | excluded;
  sessionRequired_ = (sessionRequired_ & UnitMask(~mask)) | included;
  if (remember) {
    config_.acceptedAbsent &= UnitMask(~mask); // Explicit intent replaces legacy absence permission.
    config_.disabled = (config_.disabled & UnitMask(~mask)) | excluded;
    sessionAccepted_ &= UnitMask(~mask); sessionRequired_ &= UnitMask(~mask);
  }
  restorePending_ &= UnitMask(~excluded);
  restorePending_ |= added & config_.operating.voltageMask;
  running_ |= added & config_.operating.voltageMask;
  energized_ &= UnitMask(~excluded); activeLost_ &= UnitMask(~excluded); identityLost_ &= UnitMask(~excluded);
  ++revision_; ++availabilityRevision_;
}
void PsuController::applyAvailability(const AvailabilityPlan& p) {
  if (p.action != AvailabilityAction::Probe) applyParticipation(p.selected, p.selected & UnitMask(~p.participants), p.remember, true, now_);
  else {
    setAutoResume(false);
    startup_.configurationReady = true; startup_.commissioning = true;
    startup_.permit = PermitState::Inhibited; startup_.since = now_;
    reconcile(); ++revision_; ++availabilityRevision_;
  }
}
Result PsuController::confirmAvailability(const AvailabilityPlan& p, MemoryManager& memory, uint32_t now) {
  if (availabilitySaving() || busy()) return Result::Busy;
  if (p.configurationRevision != revision_ || p.topologyRevision != discovery_.revision() ||
      p.availabilityRevision != availabilityRevision_ || uint32_t(now - p.created) > limits::confirmationMs) return Result::Changed;
  const auto fresh = p.action == AvailabilityAction::Selection ?
      previewParticipation(p.requested, p.requested & UnitMask(~p.participants), p.remember, now) :
      previewAvailability(p.action, p.requested, p.remember, now);
  if (fresh.blocker != Issue::None) return Result::Blocked;
  if (p.selected != fresh.selected || p.participants != fresh.participants) return Result::Changed;
  ++availabilityReport_.sequence; availabilityReport_.remembered = false;
  availabilityReport_.storage = StorageResult::Ok; availabilityReport_.result = Result::Ok;
  if (!p.remember) { applyAvailability(p); return Result::Ok; }
  const UnitMask accepted = config_.acceptedAbsent & UnitMask(~p.selected);
  const UnitMask disabled = (config_.disabled & UnitMask(~p.selected)) | (p.selected & UnitMask(~p.participants));
  availabilityReport_.storage = memory.startAvailabilitySave(config_, accepted, disabled);
  if (availabilityReport_.storage != StorageResult::Ok) {
    storageStatus(availabilityReport_.storage);
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
  if (availabilityReport_.storage != StorageResult::Ok) { storageStatus(availabilityReport_.storage); availabilityReport_.result = Result::Blocked; return; }
  availabilityReport_.remembered = true;
  const auto& p = savedAvailability_;
  const auto fresh = p.action == AvailabilityAction::Selection ?
      previewParticipation(p.requested, p.requested & UnitMask(~p.participants), p.remember, now) :
      previewAvailability(p.action, p.requested, p.remember, now);
  if (p.configurationRevision != revision_ || p.topologyRevision != discovery_.revision() ||
      p.availabilityRevision != availabilityRevision_ || uint32_t(now - p.created) > limits::confirmationMs ||
      fresh.blocker != Issue::None || fresh.selected != p.selected || fresh.participants != p.participants) {
    storageStatus(StorageResult::Ok, true); availabilityReport_.result = Result::Changed; return;
  }
  applyAvailability(p);
}
}
