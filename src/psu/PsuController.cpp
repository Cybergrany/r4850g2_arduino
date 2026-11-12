#include "PsuController.h"
#include "../config/Deployment.h"
#include <string.h>
namespace psu {
PsuController::PsuController(CanTransport& t) : transport_(t), config_(defaultConfiguration()) {}
bool PsuController::begin() {
  ready_ = transport_.begin();
#if PSU_BUS_PROFILE
  busProfile_.note(BusEvent::Boot,0,ready_,PSU_CAN_OBSERVER,now_);
#endif
  return ready_;
}
Result PsuController::configure(const Configuration& c, bool resume) {
  if (busy() || availabilitySaving()) return Result::Busy;
  if (!validConfig(c)) return Result::Invalid;
  const auto retainedStop = config_.thermalStop;
  reconcileSessions(c);
  config_ = c; clearRoutes();
  if (retainedStop != ThermalStop::None) {
    config_.thermalStop = retainedStop; config_.autoResume = false; resumePending_ = false;
    if (c.thermalStop != retainedStop || c.autoResume) protectionDirty_ = true;
  }
  if (config_.thermalStop != ThermalStop::None) for (uint8_t i=0; i<c.count; ++i)
    if (identified(c.units[i].identity)) hotContributors_ |= 1U << i;
  ++revision_; voltageSynced_ = restoreBlocked_ = 0;
  excluded_ = c.acceptedAbsent | c.disabled; sessionAccepted_ = sessionRequired_ = 0;
  ++availabilityRevision_;
  memset(groupChanged_, 0, sizeof(groupChanged_)); memset(groupEpisode_, 0, sizeof(groupEpisode_));
  memset(refreshed_, 0, sizeof(refreshed_)); controlRefresh_ = refreshCurrent_ = false;
  controlActive_ = controlWaiting_ = controlVerified_ = false;
  for (auto& u : control_) u = {};
  enable_.boot(resume && config_.autoResume && config_.thermalStop == ThermalStop::None);
  if (config_.thermalStop != ThermalStop::None) enable_.request(false, EnableSource::Protection);
  config_.autoResume = enable_.enabled();
  currentSynced_ = acknowledgedMask_ = validTemperatures_ = 0;
  verifying_ = identityChecked_ = false;
  report_ = {};
  running_ = resume && c.autoResume && c.operating.voltageMask && c.deploymentId == deployment::id ? c.operating.voltageMask : 0;
  restorePending_ = running_;
  memset(observedEpoch_, 0, sizeof(observedEpoch_));
  return Result::Ok;
}
Result PsuController::change(Configuration& c, UnitMask invalidate) {
  if (busy() || availabilitySaving()) return Result::Busy;
  c.operating.currentMask &= UnitMask(~invalidate);
  c.acceptedAbsent &= UnitMask(~invalidate); c.disabled &= UnitMask(~invalidate);
  if (!validConfig(c)) return Result::Invalid;
  reconcileSessions(c);
  config_ = c; clearRoutes(); ++revision_; running_ &= UnitMask(~invalidate);
  excluded_ &= UnitMask(~invalidate); sessionAccepted_ &= UnitMask(~invalidate); sessionRequired_ &= UnitMask(~invalidate);
  currentSynced_ &= UnitMask(~invalidate);
  restorePending_ &= UnitMask(~invalidate); restoreBlocked_ &= UnitMask(~invalidate);
  return Result::Ok;
}
Result PsuController::setCount(uint8_t n) {
  if (!n || n > PSU_MAX_UNITS) return Result::Invalid;
  auto c = config_; c.count = n;
  // Shrinking through existing membership is rejected; remove it explicitly first.
  c.operating.currentMask &= membersMask(n);
  c.operating.voltageMask &= membersMask(n);
  const auto result = change(c, UnitMask(membersMask(config_.count) ^ membersMask(n)));
  return result;
}
int8_t PsuController::groupIndex(const char* name) const {
  for (uint8_t i = 0; i < limits::maxGroups; ++i)
    if (config_.groups[i].name[0] && sameName(name, config_.groups[i].name)) return i;
  return -1;
}
Result PsuController::setGroup(const char* name, UnitMask mask) {
  if (!groupName(name) || !mask || (mask & ~membersMask(count()))) return Result::Invalid;
  int8_t index = groupIndex(name);
  if (index < 0) for (uint8_t i = 0; i < limits::maxGroups; ++i)
    if (!config_.groups[i].name[0]) { index = i; break; }
  if (index < 0) return Result::Invalid;
  auto c = config_; auto& g = c.groups[index];
  const UnitMask changed = g.members == mask ? 0 : g.members | mask;
  memset(g.name, 0, sizeof(g.name)); strcpy(g.name, name); g.members = mask;
  // Preserve individual targets; membership changes invalidate authorization.
  // Group totals are derived from those targets; no live writes happen here.
  return change(c, changed);
}
Result PsuController::removeGroup(uint8_t g) {
  if (g >= limits::maxGroups || !config_.groups[g].name[0]) return Result::Invalid;
  auto c = config_; UnitMask mask = c.groups[g].members; c.groups[g] = {};
  return change(c, mask);
}
Result PsuController::bind(uint8_t slot, const Identity& id, uint16_t rating, uint32_t now) {
  if (slot >= count() || !identified(id)) return Result::Invalid;
  const Psu* d = discovery_.identity(id, now);
  if (!d || !d->verified(now) || !d->live) return Result::Blocked;
  auto c = config_; c.units[slot] = {id, rating};
  c.operating.voltageMask &= UnitMask(~(1U << slot));
  const auto r = change(c, UnitMask(1U << slot));
  if (r == Result::Ok) {
    voltageSynced_ &= UnitMask(~(1U << slot)); observedEpoch_[slot] = 0;
    report_.units[slot] = {};
  }
  return r;
}
Result PsuController::unbind(uint8_t slot) {
  if (slot >= PSU_MAX_UNITS) return Result::Invalid;
  auto c = config_; c.units[slot].identity = {};
  c.operating.voltageMask &= UnitMask(~(1U << slot));
  const auto result = change(c, UnitMask(1U << slot));
  if (result == Result::Ok) {
    voltageSynced_ &= UnitMask(~(1U << slot)); observedEpoch_[slot] = 0;
    report_.units[slot] = {};
  }
  return result;
}
Result PsuController::setVoltage(uint16_t v, bool offline) {
  if (!offline) return set({}, SettingFields::Voltage, v);
  if (busy() || availabilitySaving()) return Result::Busy;
  if (v < limits::minOfflineVoltage || v > limits::maxVoltage) return Result::Invalid;
  config_.offlineVoltage = v; ++revision_; return Result::Ok;
}
Result PsuController::setCurrent(uint8_t g, uint16_t total, bool offline) {
  if (!offline) return set({ScopeKind::Group, g}, SettingFields::Current, total);
  if (busy() || availabilitySaving()) return Result::Busy;
  if (checkCurrent(g, total).issue != Issue::None) return Result::Invalid;
  config_.groups[g].offlineCurrent = total; ++revision_; return Result::Ok;
}
CurrentCheck PsuController::checkCurrent(uint8_t g, uint16_t total) const {
  CurrentCheck result = {};
  if (g >= limits::maxGroups || !config_.groups[g].members) { result.issue = Issue::Invalid; return result; }
  for (uint8_t i = 0; i < count(); ++i) if (config_.groups[g].members & (1U << i)) {
    const uint16_t maximum = currentMaximum(config_.units[i].ratedCurrent);
    const uint16_t share = allocation(config_.groups[g].members, total, i);
    if (share > maximum) return {Issue::Capacity, i, share, maximum};
  }
  return result;
}
uint16_t PsuController::settingMaximum(SettingScope scope, SettingFields field) const {
  if (!scopeMembers(config_, scope)) return 0;
  if (field == SettingFields::Voltage) return limits::maxVoltage;
  if (scope.kind == ScopeKind::Unit) return currentMaximum(config_.units[scope.index].ratedCurrent);
  if (scope.kind == ScopeKind::Group) return groupCurrentMaximum(scope.index);
  uint16_t maximum = UINT16_MAX; UnitMask assigned = 0;
  for (uint8_t g = 0; g < limits::maxGroups; ++g) if (config_.groups[g].members) {
    assigned |= config_.groups[g].members; const uint16_t m = groupCurrentMaximum(g); if (m < maximum) maximum = m;
  }
  return assigned == membersMask(count()) ? maximum : 0;
}
Result PsuController::set(SettingScope scope, SettingFields field, uint16_t value) {
  if (busy() || availabilitySaving()) return Result::Busy;
  const auto members = scopeMembers(config_, scope);
  if (!members || (field != SettingFields::Voltage && field != SettingFields::Current)) return Result::Invalid;
  if (field == SettingFields::Voltage) {
    if (value < limits::minVoltage || value > limits::maxVoltage) return Result::Invalid;
  } else {
    if (scope.kind == ScopeKind::Unit) { if (!allowedCurrent(value, config_.units[scope.index].ratedCurrent)) return Result::Invalid; }
    else {
      UnitMask assigned = 0;
      for (uint8_t g = 0; g < limits::maxGroups; ++g) if (config_.groups[g].members & members) {
        assigned |= config_.groups[g].members;
        if (checkCurrent(g, value).issue != Issue::None) return Result::Invalid;
      }
      if (assigned != members) return Result::Invalid;
    }
  }
  // Validation is complete; mutate only the requested fields, without a full snapshot.
  for (uint8_t i = 0; i < count(); ++i) if (members & (1U << i)) {
    if (field == SettingFields::Voltage) config_.voltage[i] = value;
    else if (scope.kind == ScopeKind::Unit) config_.current[i] = value;
    else config_.current[i] = allocation(config_.groups[groupFor(config_, i)].members, value, i);
  }
  ++revision_; return Result::Ok;
}
uint16_t PsuController::groupCurrentMaximum(uint8_t group) const {
  if (group >= limits::maxGroups || !config_.groups[group].members) return 0;
  const auto members = config_.groups[group].members;
  const uint8_t n = population(members); uint8_t rank = 0;
  uint16_t maximum = uint16_t(n) * limits::maxCurrent;
  // At total q*n+r, rank k receives q+(k<r). Its exact largest allowed total
  // is cap*n+k. Take the smallest member bound, including rounding remainders.
  for (uint8_t i = 0; i < count(); ++i) if (members & (1U << i)) {
    const uint16_t bound = currentMaximum(config_.units[i].ratedCurrent) * n + rank++;
    if (bound < maximum) maximum = bound;
  }
  return maximum;
}
void PsuController::reconcileSessions(const Configuration& next) {
  for (uint8_t i = 0; i < PSU_MAX_UNITS; ++i)
    if (next.deploymentId != config_.deploymentId || i >= next.count ||
        !sameIdentity(next.units[i].identity, config_.units[i].identity)) {
      sessionAh_[i] = 0; sessionSeen_ &= UnitMask(~(1U << i));
      clearFanOverride(i); alerts_.reset(i);
      const UnitMask keep = UnitMask(~(1U << i));
      hotContributors_ &= keep; sensorFaults_ &= keep; thermalIgnored_ &= keep; energized_ &= keep; activeLost_ &= keep; identityLost_ &= keep;
    }
}
Result PsuController::setPollInterval(uint16_t ms) {
  if (busy() || availabilitySaving()) return Result::Busy;
  if (ms < limits::minPollMs || ms > limits::maxPollMs) return Result::Invalid;
  config_.pollMs = ms; ++revision_; return Result::Ok;
}
void PsuController::clearRoutes() const {
  memset(routes_, unknownRoute, sizeof(routes_)); validSources_ = 0;
}
void PsuController::validateSource(uint8_t slot, const Psu& d, uint32_t now) const {
  const UnitMask bit = UnitMask(1) << slot;
  if (d.fresh(now, staleMs()) && d.verified(now)) validSources_ |= bit;
  else validSources_ &= UnitMask(~bit);
}
int8_t PsuController::resolveRoute(uint8_t slot, uint32_t now) const {
  if (routeTime_ != now || routeRevision_ != discovery_.revision()) {
    clearRoutes(); routeTime_ = now; routeRevision_ = discovery_.revision();
  }
  if (routes_[slot] == unknownRoute) {
    routes_[slot] = identified(config_.units[slot].identity) ?
        discovery_.identityIndex(config_.units[slot].identity, now) : unboundRoute;
    if (routes_[slot] >= 0) validateSource(slot, discovery_.device(routes_[slot]), now);
  }
  return routes_[slot];
}
const Psu* PsuController::deviceForSlot(uint8_t slot, uint32_t now) const {
  const int8_t index = routeIndex(slot, now);
  return index < 0 ? nullptr : &discovery_.device(index);
}
Issue PsuController::issue(uint8_t i, uint32_t now) const {
  if (discovery_.negotiating()) return Issue::IdentityPending;
  const auto route = routingIssue(i, now);
  if (route != Issue::None) return route;
  if (sensorFaults_ & (1U << i)) return Issue::TemperatureSensor;
  float temperature;
  if (!temperatures(i, temperature, now)) return Issue::TemperatureSensor;
  if (groupFor(config_, i) < 0) return Issue::Unassigned;
  if (control_[i].fault != CommandState::Idle) return Issue::ControlFault;
  const Psu* d = deviceForSlot(i, now);
  if (d->acState(now, staleMs()) == AcState::Absent) return Issue::NoAc;
  const auto& t = d->telemetry;
  if (!t.dataSeen || uint32_t(now - t.lastData) > staleMs()) return Issue::NoTelemetry;
  if (!t.broadcastSeen || uint32_t(now - t.lastBroadcast) > staleMs()) return Issue::NoTelemetry;
  if (!readinessAcceptable(i, now)) return Issue::NotReady;
  return Issue::None;
}
GroupStatus PsuController::groupStatus(uint8_t g, uint32_t now) const {
  GroupStatus s = {};
  if (g >= limits::maxGroups) return s;
  s.members = config_.groups[g].members; s.requestedCurrent = groupCurrent(config_, g);
  for (uint8_t i = 0; i < count(); ++i) if (s.members & (1U << i)) {
    const auto* d = deviceForSlot(i, now);
    if (d && d->fresh(now, staleMs())) s.responding |= 1U << i;
    const Issue why = issue(i, now);
    if (why == Issue::None) s.ready |= 1U << i;
    if (why == Issue::Missing) s.missing |= 1U << i;
    if (config_.operating.currentMask & (1U << i)) s.authorizedCurrent += config_.operating.current[i];
  }
  return s;
}
BusStatus PsuController::busStatus(uint32_t now) const {
  BusStatus s = {}; s.configured = count();
  for (uint8_t i = 0; i < Discovery::capacity; ++i) {
    const auto& d = discovery_.device(i);
    if (!d.fresh(now, staleMs())) continue;
    ++s.responding;
    if (d.verified(now)) ++s.verified;
    if (d.telemetry.broadcastSeen && uint32_t(now - d.telemetry.lastBroadcast) <= staleMs()) ++s.broadcasting;
  }
  return s;
}
bool PsuController::metric(uint8_t slot, protocol::Metric m, float& value, uint32_t now) const {
  if (m >= protocol::MetricCount) return false;
  const auto* d = deviceForSlot(slot, now);
  if (!d || !(d->telemetry.validMask & (1U << m))) return false;
  value = d->telemetry.values[m];
  if (m == protocol::CurrentCapacity) value *= config_.units[slot].ratedCurrent / 100.0f;
  return true;
}
bool PsuController::freshMetric(uint8_t slot, protocol::Metric m, float& value, uint32_t now) const {
  if (m >= protocol::MetricCount) return false;
  const auto* d = deviceForSlot(slot, now);
  if (!d || !(validSources_ & (1U << slot)) ||
      !(d->telemetry.freshMask & d->telemetry.validMask & (1U << m)) || uint32_t(now - d->telemetry.updated[m]) > staleMs()) return false;
  value = d->telemetry.values[m];
  if (m == protocol::CurrentCapacity) value *= config_.units[slot].ratedCurrent / 100.0f;
  return true;
}
ApplyPlan PsuController::preview(Operation op, int8_t group, uint32_t now, bool partial) const {
  const SettingScope scope = op == Operation::GroupCurrent || op == Operation::Restore ? SettingScope(ScopeKind::Group, group) : SettingScope();
  return buildPlan(op, scope, op == Operation::Voltage ? SettingFields::Voltage : op == Operation::GroupCurrent ? SettingFields::Current : SettingFields::Both, now, partial);
}
ApplyPlan PsuController::preview(SettingScope scope, SettingFields fields, uint32_t now, bool partial,
    const uint16_t* voltage, const uint16_t* current) const {
  return buildPlan(fields == SettingFields::Voltage ? Operation::Voltage : fields == SettingFields::Current ? Operation::GroupCurrent : Operation::All,
      scope, fields, now, partial, voltage, current);
}
ApplyPlan PsuController::buildPlan(Operation op, SettingScope scope, SettingFields fields, uint32_t now, bool partial,
    const uint16_t* voltage, const uint16_t* current) const {
  ApplyPlan p = {}; p.operation = op; p.scope = scope; p.fields = fields;
  p.group = scope.kind == ScopeKind::Group ? int8_t(scope.index) : -1; p.created = now;
  p.configurationRevision = revision_; p.topologyRevision = discovery_.revision(); p.partial = partial;
  p.prepared = !enable_.enabled() && op != Operation::Offline;
  p.members = scopeMembers(config_, scope);
  if (!p.members || (!p.prepared && !ready_) || (partial && fields != SettingFields::Current)) p.blocker = Issue::Invalid;
  if (busy() || availabilitySaving()) p.blocker = Issue::Busy;
  if (!p.prepared && !powerAvailable() && op != Operation::Offline) p.blocker = Issue::Cutoff;
  if (config_.deploymentId != deployment::id) p.blocker = Issue::DeploymentMismatch;
  for (uint8_t i = 0; i < count(); ++i) if (p.members & (1U << i)) {
    const int8_t g = groupFor(config_, i); const UnitMask bit = UnitMask(1) << i;
    p.voltage[i] = op == Operation::Offline ? config_.offlineVoltage : voltage ? voltage[i] : fields == SettingFields::Current ? config_.operating.voltage[i] : config_.voltage[i];
    p.current[i] = op == Operation::Offline ? (g >= 0 ? allocation(config_.groups[g].members, config_.groups[g].offlineCurrent, i) : 0) : current ? current[i] : config_.current[i];
    p.issues[i] = issue(i, now);
    if (!identified(config_.units[i].identity)) { p.blocker = Issue::Unbound; continue; }
    if (g < 0) { p.blocker = Issue::Unassigned; continue; }
    if (fields == SettingFields::Current && !(config_.operating.voltageMask & bit)) { p.blocker = Issue::VoltageUnsynced; continue; }
    if (p.voltage[i] < limits::minVoltage || p.voltage[i] > limits::maxVoltage || !allowedCurrent(p.current[i], config_.units[i].ratedCurrent)) { p.blocker = Issue::Capacity; continue; }
    // OFF authorizes targets only. Presence, faults, thermal and release checks
    // are evaluated again on the next explicit ON; no CAN writes occur here.
    if (p.prepared) { if (excluded_ & bit) p.excluded |= bit; continue; }
    if (op != Operation::Offline && (excluded_ & bit)) {
      if (availabilityReason(i, now) == AvailabilityReason::Fault) { p.blocker = Issue::ControlFault; continue; }
      if (acceptedAbsences() & bit) { p.excluded |= bit; continue; }
      p.blocker = Issue::ApprovalRequired; continue;
    }
    if (p.issues[i] == Issue::Missing) p.missing |= bit;
    if (p.issues[i] == Issue::None && control_[i].outputDemand == OutputDemand::Starting &&
        op != Operation::Offline && (fields != SettingFields::Current || p.current[i])) p.issues[i] = Issue::ReadinessPending;
    if (p.issues[i] == Issue::None && fields == SettingFields::Current && !(voltageSynced_ & bit)) p.issues[i] = Issue::VoltageUnsynced;
    if (p.issues[i] != Issue::None) {
      if (!(partial && p.issues[i] == Issue::Missing)) p.blocker = p.issues[i];
      continue;
    }
    p.recipients |= bit;
  }
  if (!p.prepared && !p.recipients && p.excluded != p.members && p.blocker == Issue::None) p.blocker = Issue::Missing;
  if (!p.prepared && p.blocker == Issue::None) for (uint8_t i = 0; i < count(); ++i) if (p.recipients & (1U << i)) {
    if (control_[i].fault != CommandState::Idle) { p.blocker = Issue::ControlFault; break; }
    if (control_[i].phase != ControlPhase::Held && control_[i].phase != ControlPhase::Online) p.blocker = Issue::Reconciling;
  }
  return p;
}
Result PsuController::queue(const ApplyPlan& p, bool confirmed, uint32_t now) {
  if (busy() || availabilitySaving()) return Result::Busy;
  if (p.operation == Operation::Restore) return Result::Invalid;
  if (p.configurationRevision != revision_ || p.topologyRevision != discovery_.revision() ||
      uint32_t(now - p.created) > limits::confirmationMs) return Result::Changed;
  // Revalidation uses the same scope and field selection as the review.
  const ApplyPlan fresh = buildPlan(p.operation, p.scope, p.fields, now, p.partial);
  if (fresh.blocker != Issue::None) return Result::Blocked;
  if (fresh.recipients != p.recipients || fresh.missing != p.missing || fresh.excluded != p.excluded || fresh.prepared != p.prepared) return Result::Changed;
  if (p.partial && p.missing && !confirmed) return Result::ConfirmRequired;
  job_ = fresh;
  if (p.operation != Operation::Offline) {
    const UnitMask authorized = p.partial ? fresh.recipients : fresh.members;
    for (uint8_t i = 0; i < count(); ++i) if (authorized & (1U << i)) {
      if (includes(p.fields, SettingFields::Voltage)) config_.operating.voltage[i] = fresh.voltage[i];
      if (includes(p.fields, SettingFields::Current)) config_.operating.current[i] = fresh.current[i];
    }
    if (includes(p.fields, SettingFields::Voltage)) { config_.operating.voltageMask |= authorized; voltageSynced_ &= UnitMask(~authorized); }
    if (includes(p.fields, SettingFields::Current)) { config_.operating.currentMask |= authorized; currentSynced_ &= UnitMask(~authorized); }
    running_ |= authorized;
  }
  for (uint8_t i = 0; i < count(); ++i) if (fresh.recipients & (1U << i)) {
    const auto* d = deviceForSlot(i, now); jobAddresses_[i] = d->address; jobEpochs_[i] = observedEpoch_[i] = d->epoch;
  }
  ++revision_;
  if (!fresh.prepared) { restoreBlocked_ &= UnitMask(~fresh.recipients); restorePending_ &= UnitMask(~fresh.recipients); }
  else restorePending_ |= fresh.members;
  beginReport(p.operation, fresh.members, fresh.recipients, fresh.missing | fresh.excluded);
  report_.prepared = fresh.prepared;
  if (!fresh.recipients || fresh.prepared) report_.active = false;
  jobStarted_ = false; pending_ = fresh.recipients; waiting_ = secondPhase_ = verifying_ = identityChecked_ = false;
  return Result::Ok;
}
void PsuController::beginReport(Operation op, UnitMask requested, UnitMask recipients, UnitMask skipped) {
  report_.sequence = ++reportSequence_;
  report_.active = true; report_.prepared = false; report_.operation = op;
  report_.requested = requested; report_.recipients = recipients; report_.skipped = skipped;
  report_.succeeded = report_.failed = 0;
  // Keep the last command result for units outside this operation. A later
  // successful group must not erase another group's failure from diagnostics.
  for (uint8_t i = 0; i < count(); ++i) if (recipients & (1U << i))
    report_.units[i] = {CommandState::Queued, 0, 0};
}
bool PsuController::send(const CanFrame& f, TxPurpose purpose) {
  sentThisTick_ = true;
#if PSU_BUS_PROFILE
  busProfile_.requested(f,uint8_t(purpose),discovery_.negotiating(),now_);
#endif
  if (ready_ && transport_.send(f)) {
    txPending_ = transport_.transmitState() == TransmitState::Pending;
#if PSU_BUS_PROFILE
    if (!txPending_) busProfile_.completed(transport_.transmitState() == TransmitState::Sent,now_);
#endif
    txPurpose_ = purpose; txJobSlot_ = jobSlot_; txJobSequence_ = report_.sequence;
    return true;
  }
  if (txFailures_ != UINT16_MAX) ++txFailures_;
#if PSU_BUS_PROFILE
  busProfile_.completed(false,now_);
#endif
  if (purpose == TxPurpose::ManualRead && readFailures_ != UINT16_MAX) ++readFailures_;
  return false;
}
void PsuController::serviceTransmit(uint32_t now) {
  transport_.service(now);
  if (!txPending_ || transport_.transmitState() == TransmitState::Pending) return;
  txPending_ = false;
#if PSU_BUS_PROFILE
  busProfile_.completed(transport_.transmitState() == TransmitState::Sent,now);
#endif
  if (transport_.transmitState() != TransmitState::Failed) return;
  if (txFailures_ != UINT16_MAX) ++txFailures_;
  if (txPurpose_ == TxPurpose::ManualRead && readFailures_ != UINT16_MAX) ++readFailures_;
  if (txPurpose_ == TxPurpose::Job && busy() && report_.sequence == txJobSequence_ && jobSlot_ == txJobSlot_) {
    report_.units[jobSlot_].state = CommandState::TransportError;
    advance(false);
  }
  if (txPurpose_ == TxPurpose::Control && controlActive_) controlComplete(CommandState::TransportError, now);
}
void PsuController::advance(bool success) {
  const UnitMask bit = UnitMask(1U << jobSlot_);
  if (!success) {
    report_.failed |= bit; restoreBlocked_ |= bit;
    if (thermalLevel_ != ThermalLevel::Normal && report_.units[jobSlot_].reg == protocol::OnlineCurrent)
      latchControlFault(jobSlot_, report_.units[jobSlot_].state, protocol::OnlineCurrent, now_);
  }
  else if (secondPhase_ || job_.operation == Operation::GroupCurrent || job_.operation == Operation::Voltage ||
      (job_.operation == Operation::Restore && !(config_.operating.currentMask & bit))) report_.succeeded |= bit;
  pending_ &= UnitMask(~bit); waiting_ = verifying_ = identityChecked_ = false;
}
void PsuController::receive(const CanFrame& f, uint32_t now) {
#if PSU_BUS_PROFILE
  busProfile_.received(f,now);
  if (protocol::isNegotiation(f) && !discovery_.negotiating()) busProfile_.negotiation(f,now);
  const auto* prior = discovery_.address(protocol::address(f.id));
  if (protocol::isReply(f) && protocol::command(f.id)==protocol::infoCommand && f.data[0]==0 && f.data[1]==2 &&
      (!prior || memcmp(prior->identity.bytes,f.data+2,6))) busProfile_.identity(protocol::address(f.id),f.data+2,now);
  if (protocol::isReply(f) && protocol::command(f.id)==protocol::dataCommand && f.data[0]==1 && f.data[1]==0x83 &&
      (!prior || !prior->telemetry.alarmSeen || prior->telemetry.alarmBits!=protocol::readBigEndian(f.data+4)))
    busProfile_.note(BusEvent::Status,protocol::address(f.id),protocol::readBigEndian(f.data+4),0,now);
  if (protocol::isCurrentBroadcast(f) && (!prior || !prior->telemetry.broadcastSeen || prior->telemetry.ready!=(f.data[3]==0)))
    busProfile_.note(BusEvent::Ready,protocol::address(f.id),f.data[3],0,now);
#endif
  if (protocol::isReply(f) && protocol::command(f.id)==protocol::infoCommand && f.data[0]==0 && f.data[1]==2)
    for (uint8_t i=0;i<count();++i) if (energized_ & (1U << i)) {
      const auto* previous=deviceForSlot(i,now);
      if (previous && previous->address==protocol::address(f.id) && memcmp(config_.units[i].identity.bytes,f.data+2,6)) {
        activeLost_ |= 1U << i; identityLost_ |= 1U << i;
      }
    }
  discovery_.receive(f, now);
  int8_t slot = -1;
  for (uint8_t i = 0; i < count(); ++i) {
    const auto* d = deviceForSlot(i, now);
    if (d && d->address == protocol::address(f.id)) { slot = i; validateSource(i, *d, now); break; }
  }
  const bool refreshAccepted = controlReceive(f, now);
  if (slot >= 0) readinessReceive(slot, f, now);
  if (slot >= 0 && protocol::isCurrentBroadcast(f)) {
    const auto* d = deviceForSlot(slot, now);
    if (d && d->verified(now)) {
      const uint16_t raw = uint16_t(f.data[6]) << 8 | f.data[7];
      sessionAh_[slot] += raw / protocol::ahCurrentDivisor * protocol::ahSampleSeconds / 3600;
      sessionSeen_ |= 1U << slot;
    }
  }
  // A routing address is not an identity. Recheck INFO immediately before each
  // setting write, including the current phase and nonvolatile operations.
  if (verifying_ && slot == jobSlot_ && protocol::isReply(f) &&
      protocol::command(f.id) == protocol::infoCommand && f.data[0] == 0 && f.data[1] == 2) {
    const auto* d = deviceForSlot(jobSlot_, now);
    if (d && d->epoch == jobEpochs_[jobSlot_] && d->verified(now) && issue(jobSlot_, now) == Issue::None)
      identityChecked_ = true;
  }
  if (waiting_ && slot == jobSlot_ && protocol::settingReply(f, report_.units[jobSlot_].reg, expected_)) {
    const auto* d = deviceForSlot(jobSlot_, now);
    if (d && d->epoch == jobEpochs_[jobSlot_] && d->verified(now) && issue(jobSlot_, now) == Issue::None) {
      const bool ok = f.data[0] == 1;
      report_.units[jobSlot_].state = ok ? CommandState::Success : CommandState::Rejected;
      if (ok && f.data[1] == protocol::OnlineVoltage) {
        voltageSynced_ |= 1U << jobSlot_;
      }
      if (ok && f.data[1] == protocol::OnlineCurrent) {
        currentAccepted(jobSlot_, expected_);
      }
      advance(ok);
    }
  }
  if (observer_) observer_(context_, slot, f, refreshAccepted);
}
void PsuController::runJob(uint32_t now) {
  if (discovery_.negotiating()) { cancelJob(); return; }
  if (!powerAvailable() && job_.operation != Operation::Offline) { cancelJob(); return; }
  if (job_.operation != Operation::Offline && !enable_.enabled()) { cancelJob(); return; }
  if (job_.operation != Operation::Offline && !controlsOnline(job_.recipients)) {
    if (jobStarted_) {
      for (uint8_t i = 0; i < count(); ++i) if (job_.recipients & (1U << i)) {
        if (control_[i].phase != ControlPhase::Online || control_[i].fault != CommandState::Idle) {
          report_.failed |= 1U << i; report_.succeeded &= UnitMask(~(1U << i)); report_.units[i].state = CommandState::Changed;
        }
      }
      const UnitMask incomplete = report_.recipients & UnitMask(~(report_.failed | report_.succeeded));
      report_.skipped |= incomplete;
      for (uint8_t i = 0; i < count(); ++i) if (incomplete & (1U << i)) report_.units[i].state = CommandState::Incomplete;
      report_.active = false; pending_ = 0; waiting_ = verifying_ = identityChecked_ = false; return;
    }
    for (uint8_t i = 0; i < count(); ++i) if (job_.recipients & (1U << i)) {
      if (control_[i].fault != CommandState::Idle || routingIssue(i, now) != Issue::None) {
        cancelJob(); return;
      }
    }
    const auto state = controlStatus(now);
    if (state.blocker != Issue::None) { cancelJob(); return; }
    return; // Reconciliation owns zero/fan/voltage/wake; user jobs cannot bypass it.
  }
  if (waiting_) {
    const auto* d = deviceForSlot(jobSlot_, now);
    if (!d || d->epoch != jobEpochs_[jobSlot_] || issue(jobSlot_, now) != Issue::None) {
      report_.units[jobSlot_].state = CommandState::Changed; advance(false);
    } else if (uint32_t(now - lastSend_) >= limits::ackTimeoutMs) {
      report_.units[jobSlot_].state = CommandState::Timeout; advance(false);
    }
  }
  if (waiting_) return;
  if (!pending_) {
    if (!secondPhase_ && !report_.failed && job_.operation != Operation::Voltage && job_.operation != Operation::GroupCurrent &&
        !(job_.operation == Operation::Restore && !(config_.operating.currentMask & job_.recipients))) {
      // Revalidate the entire voltage phase before starting any current writes.
      // An earlier voltage ACK is insufficient if that unit disappeared while
      // the remaining voltage recipients were still being processed.
      for (uint8_t i = 0; i < count(); ++i) if (job_.recipients & (1U << i)) {
        const auto* d = deviceForSlot(i, now);
        if (!d || d->epoch != jobEpochs_[i] || issue(i, now) != Issue::None) {
          report_.units[i].state = CommandState::Changed;
          report_.failed |= 1U << i; report_.succeeded &= UnitMask(~(1U << i));
          restoreBlocked_ |= 1U << i;
        }
      }
      if (!report_.failed) {
        secondPhase_ = true; pending_ = job_.recipients;
        if (job_.operation == Operation::Restore) pending_ &= config_.operating.currentMask;
      }
    }
    if (!pending_) {
      report_.active = false;
      const UnitMask incomplete = job_.recipients & UnitMask(~(report_.failed | report_.succeeded));
      report_.skipped |= incomplete;
      for (uint8_t i = 0; i < count(); ++i) if (incomplete & (1U << i)) report_.units[i].state = CommandState::Incomplete;
      return;
    }
  }
  if (controlActive_ || sentThisTick_ || txPending_ || uint32_t(now - lastSend_) < limits::commandGapMs) return;
  for (jobSlot_ = 0; !(pending_ & (1U << jobSlot_)); ++jobSlot_) {}
  const auto* d = deviceForSlot(jobSlot_, now);
  if (!d || d->epoch != jobEpochs_[jobSlot_] || d->address != jobAddresses_[jobSlot_] || issue(jobSlot_, now) != Issue::None) {
    report_.units[jobSlot_].state = CommandState::Changed; advance(false); return;
  }
  const bool current = secondPhase_ || job_.operation == Operation::GroupCurrent;
  const bool offline = job_.operation == Operation::Offline;
  if (current && !offline && job_.current[jobSlot_] >= control::fanOutputCurrentThreshold &&
      control_[jobSlot_].fanAcknowledged != FanMode::Max) return;
  const uint8_t reg = current ? (offline ? protocol::OfflineCurrent : protocol::OnlineCurrent) :
      (offline ? protocol::OfflineVoltage : protocol::OnlineVoltage);
  expected_ = current ? protocol::encodeCurrent(offline ? job_.current[jobSlot_] : limitedCurrent(jobSlot_, job_.current[jobSlot_]), config_.units[jobSlot_].ratedCurrent) : protocol::encodeVoltage(job_.voltage[jobSlot_]);
  if (!verifying_) {
    jobStarted_ = true;
    report_.units[jobSlot_] = {CommandState::VerifyingIdentity, reg, expected_};
    verifying_ = true; identityChecked_ = false; verificationStarted_ = now;
    if (!send(protocol::request(d->address, protocol::infoCommand), TxPurpose::Job)) {
      report_.units[jobSlot_].state = CommandState::TransportError; advance(false);
    }
    return;
  }
  if (!identityChecked_) {
    if (uint32_t(now - verificationStarted_) >= limits::ackTimeoutMs) {
      report_.units[jobSlot_].state = CommandState::IdentityTimeout; advance(false);
    }
    return;
  }
  report_.units[jobSlot_] = {CommandState::Waiting, reg, expected_};
  lastSend_ = now; waiting_ = send(protocol::setting(d->address, reg, expected_), TxPurpose::Job);
  if (!waiting_) { report_.units[jobSlot_].state = CommandState::TransportError; advance(false); }
  else if (reg == protocol::OnlineCurrent) currentSent(jobSlot_, expected_, now);
}
void PsuController::restore(uint32_t now) {
  if (!enable_.enabled() || !powerAvailable() || busy() || availabilitySaving() || !restorePending_ ||
      !participants() || !controlsOnline(requiredMembers() & UnitMask(~sensorFaults_))) return;
  // Binding/adopting or adding slots never authorizes voltage by itself. Keep
  // the pending installation observable until an explicit complete voltage apply.
  if ((config_.operating.voltageMask & requiredMembers()) != requiredMembers()) return;
  // Keep the existing availability barrier; voltage targets are per unit.
  for (uint8_t i = 0; i < count(); ++i) if ((requiredMembers() & UnitMask(~sensorFaults_) & (1U << i)) && issue(i, now) != Issue::None) return;
  UnitMask recipients = restorePending_ & participants() & config_.operating.voltageMask & UnitMask(~(restoreBlocked_ | sensorFaults_));
  for (uint8_t i = 0; i < count(); ++i)
    if (control_[i].outputDemand == OutputDemand::Starting) recipients &= UnitMask(~(1U << i));
  if (!recipients) return;
  // Restore the authorized profile, never staged edits. Voltage is acknowledged
  // by every returning recipient before any authorized current is restored.
  job_ = {}; job_.operation = Operation::Restore; job_.fields = SettingFields::Both;
  job_.recipients = job_.members = recipients;
  for (uint8_t i = 0; i < count(); ++i) if (recipients & (1U << i)) {
    const auto* d = deviceForSlot(i, now);
    jobAddresses_[i] = d->address; jobEpochs_[i] = d->epoch;
    job_.current[i] = config_.operating.current[i]; job_.voltage[i] = config_.operating.voltage[i];
  }
  pending_ = recipients; restorePending_ &= UnitMask(~recipients);
  beginReport(Operation::Restore, recipients, recipients);
  jobStarted_ = false;
  waiting_ = secondPhase_ = verifying_ = identityChecked_ = false;
}
void PsuController::tick(uint32_t now) {
  now_ = now;
  startupTick(now);
  if (!ready_) { thermalTick(now); alertTick(now); finishAvailability(now); return; }
  sentThisTick_ = false;
  serviceTransmit(now);
  CanFrame f;
  for (uint8_t n = 0; n < 32 && transport_.receive(f); ++n) receive(f, now);
#if PSU_BUS_PROFILE
  const bool negotiating = discovery_.negotiating();
#endif
  discovery_.tick(now, staleMs());
#if PSU_BUS_PROFILE
  if (negotiating && !discovery_.negotiating()) busProfile_.note(BusEvent::Quiet,0,0,0,now);
#endif
  thermalTick(now);
  controlTick(now);
  availabilityTick(now);
  // Revalidate saved decisions against this tick's received identities and
  // power state before making any newly authorized participant eligible.
  finishAvailability(now);
  restore(now);
  if (busy()) runJob(now);
  if (!busy() && !availabilitySaving()) refreshSettings(now);
  alertTick(now);
  // Identity and telemetry refresh continue even during a long multi-unit job.
  if (sentThisTick_ || txPending_ || uint32_t(now - lastProbe_) < limits::readRequestGapMs) return;
  // Round-robin due requests, without consuming a timeslot on an idle entry.
  // Established devices take priority over manual reads and unused addresses;
  // repeated manual polls must not starve identity leases or other members.
  for (uint8_t n = 0; n < Discovery::capacity; ++n) {
    const auto& entry = discovery_.device(probeIndex_);
    if (++probeIndex_ == Discovery::capacity) probeIndex_ = 0;
    if (!entry.occupied) continue;
    Psu* d = discovery_.address(entry.address);
    const uint32_t infoInterval = d->identitySamples < 2 ? limits::minPollMs : limits::identityRefreshMs;
    if (uint32_t(now - d->lastInfoRequest) >= infoInterval) {
      lastProbe_ = d->lastInfoRequest = now;
      send(protocol::request(d->address, protocol::infoCommand)); return;
    }
    if (uint32_t(now - d->lastDataRequest) >= config_.pollMs) {
      lastProbe_ = d->lastDataRequest = now;
      send(protocol::request(d->address, protocol::dataCommand)); return;
    }
  }
  if (requestedDescription_) {
    const uint8_t address = requestedDescription_; requestedDescription_ = 0;
    lastProbe_ = now; send(protocol::request(address, protocol::descriptionCommand), TxPurpose::ManualRead); return;
  }
  if (requestedPoll_) {
    for (uint8_t i = 0; i < PSU_MAX_UNITS; ++i) if (requestedPoll_ & (1U << i)) {
      requestedPoll_ &= UnitMask(~(1U << i));
      const auto* d = deviceForSlot(i, now);
      if (!d || !d->fresh(now, staleMs())) {
        if (readFailures_ != UINT16_MAX) ++readFailures_;
      } else {
        lastProbe_ = now; discovery_.address(d->address)->lastDataRequest = now;
        send(protocol::request(d->address), TxPurpose::ManualRead);
      }
      return;
    }
  }
  if (scanAddress_ && !busy() && uint32_t(now - lastScan_) >= limits::discoveryGapMs) {
    lastProbe_ = lastScan_ = now;
    send(protocol::request(scanAddress_, protocol::infoCommand));
    scanAddress_ = scanAddress_ == 127 ? 0 : scanAddress_ + 1;
  }
}
uint8_t PsuController::serviceReceive(uint32_t now) {
  if (!ready_) return 0;
  now_ = now;
  CanFrame frame; uint8_t received = 0;
  while (received < 4 && transport_.receive(frame)) { receive(frame, now); ++received; }
  if (received) {
#if PSU_BUS_PROFILE
    const bool negotiating = discovery_.negotiating();
#endif
    discovery_.tick(now, staleMs());
#if PSU_BUS_PROFILE
    if (negotiating && !discovery_.negotiating()) busProfile_.note(BusEvent::Quiet,0,0,0,now);
#endif
    thermalTick(now);
  }
  return received;
}
Result PsuController::requestDescription(uint8_t address) {
  if (!address || address > 127) return Result::Invalid;
  if (!ready_) return Result::TransportError;
  if (requestedDescription_) return Result::Busy;
  requestedDescription_ = address; return Result::Ok;
}
Result PsuController::requestPoll(UnitMask mask, uint32_t now) {
  if (!mask || (mask & ~membersMask(count()))) return Result::Invalid;
  if (!ready_) return Result::TransportError;
  if (requestedPoll_) return Result::Busy;
  for (uint8_t i = 0; i < count(); ++i) if (mask & (1U << i)) {
    const auto* d = deviceForSlot(i, now);
    if (!d || !d->fresh(now, staleMs())) return Result::Blocked;
  }
  requestedPoll_ = mask;
  return Result::Ok;
}
void PsuController::resetAmpHours() {
  memset(sessionAh_, 0, sizeof(sessionAh_)); sessionSeen_ = 0;
  for (uint8_t i = 0; i < Discovery::capacity; ++i) {
    const auto& d = discovery_.device(i);
    if (d.occupied) discovery_.address(d.address)->telemetry.ampHours = 0;
  }
}
}
