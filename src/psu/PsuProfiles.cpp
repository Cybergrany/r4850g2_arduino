#include "PsuController.h"
#include <string.h>
namespace psu {
ProfileReview PsuController::previewProfile(MemoryManager& memory, uint8_t slot, bool inclusion, uint32_t now) const {
  ProfileReview r; r.slot = slot; r.restoreInclusion = inclusion; r.created = now;
  r.revision = revision_; r.topology = discovery_.revision(); r.availability = availabilityRevision_;
  ProfileInfo info; r.storage = memory.profileInfo(slot, info);
  if (r.storage != StorageResult::Ok) return r;
  r.generation = info.generation;
  if (busy() || availabilitySaving() || memory.saving()) { r.blocker = Issue::Busy; return r; }
  uint16_t volts[PSU_MAX_UNITS] = {}, amps[PSU_MAX_UNITS] = {};
  r.storage = memory.profileTargets(slot, info.generation, config_, volts, amps, r.mismatch, &r.included);
  if (r.storage != StorageResult::Ok) return r;
  if (inclusion && !info.hasInclusion) { r.storage = StorageResult::InvalidConfig; return r; }
  if (!inclusion) r.included = participants();
  const UnitMask changed = r.included ^ participants();
  if (changed) {
    // Changing participation is intentionally an OFF operation. Never remove a
    // live member from supervision while it can still be emitting current.
    if (enable_.enabled()) { r.blocker = Issue::Disabled; return r; }
    for (uint8_t i = 0; i < count(); ++i) if (changed & (1U << i)) {
      const auto reason = availabilityReason(i, now); const int8_t g = groupFor(config_, i);
      if (g < 0) { r.blocker = Issue::Unassigned; return r; }
      if (reason == AvailabilityReason::Fault) { r.blocker = Issue::ControlFault; return r; }
      if (r.included & (1U << i)) {
        const auto plan = previewAvailability(AvailabilityAction::Include, UnitMask(1) << i, false, now);
        if (plan.blocker != Issue::None) { r.blocker = plan.blocker; return r; }
      } else if (reason == AvailabilityReason::NoCan || reason == AvailabilityReason::NoAc) {
        const auto plan = previewAvailability(AvailabilityAction::Accept, UnitMask(1) << i, false, now);
        if (plan.blocker != Issue::None) { r.blocker = plan.blocker; return r; }
      } else if (reason != AvailabilityReason::Ready || control_[i].phase != ControlPhase::Held || !control_[i].zeroConfirmed) {
        r.blocker = Issue::Reconciling; return r;
      } else if (!availabilitySettled(g, now)) { r.blocker = Issue::Settling; return r; }
    }
  }
  const auto plan = preview({}, SettingFields::Both, now, false, volts, amps);
  r.blocker = plan.blocker;
  return r;
}
Result PsuController::loadProfile(MemoryManager& memory, const ProfileReview& r, uint32_t now) {
  if (busy() || availabilitySaving() || memory.saving()) return Result::Busy;
  if (r.revision != revision_ || r.topology != discovery_.revision() || r.availability != availabilityRevision_ ||
      uint32_t(now - r.created) > limits::confirmationMs) return Result::Changed;
  const auto fresh = previewProfile(memory, r.slot, r.restoreInclusion, now);
  if (fresh.storage != StorageResult::Ok || fresh.blocker != Issue::None) return Result::Blocked;
  if (fresh.generation != r.generation || fresh.included != r.included) return Result::Changed;
  return applyProfile(memory, r, now);
}
Result PsuController::applyProfile(MemoryManager& memory, const ProfileReview& r, uint32_t now) {
  // Release the temporary target arrays before preview/queue allocate plans.
  const auto staged = stageProfile(memory, r);
  if (staged != Result::Ok) return staged;
  const auto plan = preview({}, SettingFields::Both, now);
  const auto result = queue(plan, false, now);
  if (result != Result::Ok) return result;
  profileLoad_ = {}; profileLoad_.operation = report_.sequence; profileLoad_.revision = revision_;
  profileLoad_.prepared = plan.prepared; profileLoad_.state = ProfileLoadState::Applying;
  return Result::Ok;
}
Result PsuController::stageProfile(MemoryManager& memory, const ProfileReview& r) {
  uint16_t volts[PSU_MAX_UNITS] = {}, amps[PSU_MAX_UNITS] = {}; uint8_t mismatch;
  if (memory.profileTargets(r.slot, r.generation, config_, volts, amps, mismatch) != StorageResult::Ok) return Result::Changed;
  if (r.restoreInclusion) {
    const auto excluded = membersMask(count()) & UnitMask(~r.included);
    config_.disabled = excluded; excluded_ = excluded;
    sessionRequired_ |= r.included; sessionRequired_ &= UnitMask(~excluded);
    restorePending_ &= r.included;
    activeLost_ &= r.included; identityLost_ &= r.included; energized_ &= r.included;
    // A saved participation choice does not release a new thermal incident.
    // The existing explicit hot-unit absence acceptance remains separate.
    ++availabilityRevision_;
  }
  memcpy(config_.voltage, volts, count() * sizeof(volts[0])); memcpy(config_.current, amps, count() * sizeof(amps[0])); ++revision_;
  return Result::Ok;
}
void PsuController::profileTick(MemoryManager& memory) {
  auto& p = profileLoad_;
  if (p.state == ProfileLoadState::Applying) {
    if (report_.sequence != p.operation || revision_ != p.revision) {
      p.state = ProfileLoadState::Failed; p.storage = StorageResult::Changed; return;
    }
    if (report_.active) return;
    if (report_.failed || report_.succeeded != report_.recipients) { p.state = ProfileLoadState::Failed; return; }
    if (memory.saving()) return;
    p.storage = memory.startSave(config_);
    if (p.storage == StorageResult::Busy) return;
    if (p.storage != StorageResult::Ok) { p.state = ProfileLoadState::Failed; storageStatus(p.storage); return; }
    p.saveToken = memory.saveToken(); p.state = ProfileLoadState::Saving;
  } else if (p.state == ProfileLoadState::Saving && memory.completedToken() == p.saveToken) {
    p.storage = memory.completedResult(); p.older = revision_ != p.revision;
    storageStatus(p.storage, p.older);
    p.state = p.storage == StorageResult::Ok ? ProfileLoadState::Complete : ProfileLoadState::Failed;
  }
}
}
