#include "PsuController.h"
#include <math.h>
namespace psu {
uint16_t PsuController::commandCurrent(uint8_t i, uint16_t authorized) const {
  const uint16_t limit = limitedCurrent(i,authorized);
  // A fault may occur before a peer was primed: keep that peer at zero.
  if ((sync_.members & (1U<<i)) && sync_.blocked && control_[i].zeroConfirmed) return 0;
  return (sync_.members & (1U<<i)) && jobPhase_ != JobPhase::Current && limit > currentSync::primeCa ? currentSync::primeCa : limit;
}
uint16_t PsuController::syncHoldRaw(uint8_t i, uint16_t target) const {
  // Already-running recipients wait at their accepted limit while new peers
  // prime. Keep the raw value exact: decode/re-encode on refresh would drift.
  const UnitMask bit=1U<<i;
  if (!sync_.members || jobPhase_==JobPhase::Current || !(job_.recipients & bit) || (sync_.members & bit)) return target;
  if (!(acknowledgedMask_ & bit)) return 0;
  return target < acknowledgedCurrent_[i] ? target : acknowledgedCurrent_[i];
}
void PsuController::cancelSync() {
#if PSU_BUS_PROFILE
  if (sync_.members) busProfile_.note(BusEvent::Sync,0,sync_.members,0,now_);
#endif
  if (sync_.members) ++syncGeneration_;
  sync_.members = sync_.observed = sync_.qualified = 0;
  sync_.started = sync_.blocked = false;
}
void PsuController::beginSync() {
  cancelSync();
  if (sync_.bypass || job_.operation == Operation::Offline || !includes(job_.fields,SettingFields::Current)) return;
  for (uint8_t i=0; i<count(); ++i) if ((job_.recipients & (1U<<i)) && control_[i].zeroConfirmed &&
      (acknowledgedMask_ & (1U<<i)) && !acknowledgedCurrent_[i] &&
      protocol::encodeCurrent(limitedCurrent(i,job_.current[i]),config_.units[i].ratedCurrent)) sync_.members |= 1U<<i;
  if (population(sync_.members) < 2) sync_.members = 0;
#if PSU_BUS_PROFILE
  if (sync_.members) busProfile_.note(BusEvent::Sync,0,sync_.members,1,now_);
#endif
}
void PsuController::jobPhase(JobPhase next) {
#if PSU_BUS_PROFILE
  if (sync_.members && jobPhase_ != next) busProfile_.note(BusEvent::Sync,0,sync_.members,uint8_t(next)+2,now_);
#endif
  if (sync_.members && jobPhase_ != next) ++syncGeneration_;
  jobPhase_ = next;
}
CurrentSyncStatus PsuController::syncStatus(uint32_t now) const {
  CurrentSyncStatus s; s.members = sync_.members; s.qualified = sync_.qualified; s.bypass = sync_.bypass;
  if (!s.members) return s;
  s.phase = sync_.blocked ? SyncPhase::Blocked : jobPhase_ == JobPhase::Voltage ? SyncPhase::Preparing :
      jobPhase_ == JobPhase::Prime ? SyncPhase::Priming : jobPhase_ == JobPhase::SyncWait ? SyncPhase::Waiting : SyncPhase::Releasing;
  s.elapsedMs = sync_.started ? uint32_t(now-sync_.since) : 0;
  s.timedOut = sync_.started && jobPhase_ != JobPhase::Current && s.elapsedMs >= currentSync::timeoutMs;
  return s;
}
SyncReview PsuController::reviewSync(uint32_t now) const {
  return {revision_,discovery_.revision(),availabilityRevision_,report_.sequence,now,sync_.members,syncGeneration_};
}
Result PsuController::bypassSync(const SyncReview& r, uint32_t now) {
  if (r.revision != revision_ || r.topology != discovery_.revision() || r.operation != report_.sequence ||
      r.availability != availabilityRevision_ || r.generation != syncGeneration_ || r.members != sync_.members || uint32_t(now-r.created) > limits::confirmationMs) return Result::Changed;
  // A bypass never changes enable, participation, thermal or fault state. The
  // scheduler revalidates those again before any final current can be sent.
  sync_.bypass = true; ++syncGeneration_;
#if PSU_BUS_PROFILE
  busProfile_.note(BusEvent::Sync,0,sync_.members,6,now);
#endif
  return Result::Ok;
}
void PsuController::requireSync() { if (sync_.bypass) { sync_.bypass = false; ++syncGeneration_; } }
void PsuController::checkSync(uint32_t now) {
  if (!sync_.members) return;
  for (uint8_t i=0; i<count(); ++i) if (job_.recipients & (1U<<i)) {
    const UnitMask bit = 1U<<i; const auto* d = deviceForSlot(i,now); float amps;
    if (!d || d->epoch != jobEpochs_[i] || !(participants() & bit) || issue(i,now) != Issue::None ||
        (report_.failed & bit) || (jobStarted_ && control_[i].phase != ControlPhase::Online)) {
      if (!(report_.failed & bit)) report_.units[i].state = CommandState::Changed;
      report_.failed |= bit; report_.succeeded &= UnitMask(~bit); restoreBlocked_ |= bit;
      sync_.blocked = true; sync_.observed &= UnitMask(~bit); sync_.qualified &= UnitMask(~bit);
    } else if (!currentSynchronized(i) || !freshMetric(i,protocol::OutputCurrent,amps,now)) {
      sync_.observed &= UnitMask(~bit); sync_.qualified &= UnitMask(~bit);
    }
  }
}
void PsuController::syncReceive(uint8_t i, const CanFrame& f, uint32_t now, bool gap) {
  const UnitMask bit = 1U<<i;
  if (!(sync_.members & bit) || jobPhase_ == JobPhase::Current || sync_.blocked || !protocol::isReply(f) ||
      protocol::command(f.id) != protocol::dataCommand || f.data[0] != 1 || f.data[1] != 0x81) return;
  const auto* d = deviceForSlot(i,now); float amps;
  const uint16_t raw = effectiveRaw(i);
  const float expected = raw * (config_.units[i].ratedCurrent / 102400.0f);
  if (gap || !raw || !currentSynchronized(i) || !(acknowledgedMask_ & bit) || acknowledgedCurrent_[i] != raw ||
      !d || d->epoch != jobEpochs_[i] || issue(i,now) != Issue::None ||
      !freshMetric(i,protocol::OutputCurrent,amps,now) || !isfinite(amps) ||
      amps < expected * (100-currentSync::tolerance)/100 || amps > expected * (100+currentSync::tolerance)/100) {
    sync_.observed &= UnitMask(~bit); sync_.qualified &= UnitMask(~bit); return;
  }
  if (!(sync_.observed & bit)) { sync_.observed |= bit; sync_.goodSince[i] = now; }
  if (uint32_t(now-sync_.goodSince[i]) >= currentSync::stableMs) sync_.qualified |= bit;
}
}
