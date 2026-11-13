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
  r.blocker = profileBlocker(volts, amps, r.included, inclusion, now);
  return r;
}
Issue PsuController::profileBlocker(const uint16_t* volts, const uint16_t* amps,
    UnitMask included, bool inclusion, uint32_t now) const {
  // These plan temporaries are needed after EEPROM validation, not while its
  // record buffer is on the stack. Keep the two stages in separate frames.
  if (inclusion) {
    const auto participation = previewParticipation(membersMask(count()), membersMask(count()) & UnitMask(~included), true, now);
    if (participation.blocker != Issue::None) return participation.blocker;
  }
  const auto plan = preview({}, SettingFields::Both, now, false, volts, amps);
  return plan.blocker;
}
Result PsuController::loadProfile(MemoryManager& memory, const ProfileReview& r, uint32_t now) {
  if (busy() || availabilitySaving() || memory.saving()) return Result::Busy;
  if (r.revision != revision_ || r.topology != discovery_.revision() || r.availability != availabilityRevision_ ||
      uint32_t(now - r.created) > limits::confirmationMs) return Result::Changed;
  const auto fresh = previewProfile(memory, r.slot, r.restoreInclusion, now);
  if (fresh.storage != StorageResult::Ok || fresh.blocker != Issue::None) return Result::Blocked;
  if (fresh.generation != r.generation || fresh.included != r.included) return Result::Changed;
  // Release the temporary target arrays before preview/queue allocate plans.
  const auto staged = stageProfile(memory, r);
  if (staged != Result::Ok) return staged;
  return applyProfile(r, now);
}
Result PsuController::applyProfile(const ProfileReview& r, uint32_t now) {
  const auto plan = preview({}, SettingFields::Both, now);
  const auto result = queue(plan, false, now);
  if (result != Result::Ok) return result;
  profileLoad_ = {}; profileLoad_.operation = report_.sequence; profileLoad_.revision = revision_;
  profileLoad_.restoreParticipation = r.restoreInclusion; profileLoad_.participation = r.included;
  profileLoad_.topology = discovery_.revision(); profileLoad_.availability = availabilityRevision_;
  profileLoad_.prepared = plan.prepared; profileLoad_.state = ProfileLoadState::Applying;
  return Result::Ok;
}
Result PsuController::stageProfile(MemoryManager& memory, const ProfileReview& r) {
  uint16_t volts[PSU_MAX_UNITS] = {}, amps[PSU_MAX_UNITS] = {}; uint8_t mismatch;
  if (memory.profileTargets(r.slot, r.generation, config_, volts, amps, mismatch) != StorageResult::Ok) return Result::Changed;
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
    // The profile owns one final controller save. Participation is patched in
    // that private snapshot and only activated after verified completion.
    p.storage = p.restoreParticipation ? memory.startAvailabilitySave(config_,
        config_.acceptedAbsent & UnitMask(~membersMask(count())), membersMask(count()) & UnitMask(~p.participation)) : memory.startSave(config_);
    if (p.storage == StorageResult::Busy) return;
    if (p.storage != StorageResult::Ok) { p.state = ProfileLoadState::Failed; storageStatus(p.storage); return; }
    p.saveToken = memory.saveToken(); p.state = ProfileLoadState::Saving;
  } else if (p.state == ProfileLoadState::Saving && memory.completedToken() == p.saveToken) {
    p.storage = memory.completedResult(); p.older = revision_ != p.revision;
    if (p.restoreParticipation && p.storage == StorageResult::Ok) {
      const auto selection = previewParticipation(membersMask(count()), membersMask(count()) & UnitMask(~p.participation), true, now_);
      p.older |= p.topology != discovery_.revision() || p.availability != availabilityRevision_ || selection.blocker != Issue::None;
      if (!p.older) applyParticipation(selection.selected, selection.selected & UnitMask(~p.participation), true, false, now_);
    }
    storageStatus(p.storage, p.older);
    p.state = p.storage == StorageResult::Ok ? ProfileLoadState::Complete : ProfileLoadState::Failed;
  }
}
}
