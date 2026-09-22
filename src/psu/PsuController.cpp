#include "PsuController.h"
#include "../config/Deployment.h"
#include <string.h>
namespace psu {
PsuController::PsuController(CanTransport& t) : transport_(t), config_(defaultConfiguration()) {}
bool PsuController::begin() { ready_ = transport_.begin(); return ready_; }
Result PsuController::configure(const Configuration& c, bool resume) {
  if (busy()) return Result::Busy;
  if (!validConfig(c)) return Result::Invalid;
  reconcileSessions(c);
  config_ = c; ++revision_; voltageSynced_ = restoreBlocked_ = 0;
  currentSynced_ = 0;
  voltageCommissioned_ = verifying_ = identityChecked_ = false;
  report_ = {};
  running_ = resume && c.autoResume && c.operating.voltageAuthorized && c.deploymentId == deployment::id ? c.operating.voltageMask : 0;
  restorePending_ = running_;
  memset(observedEpoch_, 0, sizeof(observedEpoch_));
  return Result::Ok;
}
Result PsuController::change(const Configuration& next, uint8_t invalidate) {
  if (busy()) return Result::Busy;
  Configuration c = next;
  c.operating.currentMask &= uint8_t(~invalidate);
  if (!validConfig(c)) return Result::Invalid;
  reconcileSessions(c);
  config_ = c; ++revision_; running_ &= uint8_t(~invalidate);
  currentSynced_ &= uint8_t(~invalidate);
  restorePending_ &= uint8_t(~invalidate); restoreBlocked_ &= uint8_t(~invalidate);
  return Result::Ok;
}
Result PsuController::setCount(uint8_t n) {
  if (!n || n > PSU_MAX_UNITS) return Result::Invalid;
  auto c = config_; c.count = n;
  // Shrinking through existing membership is rejected; remove it explicitly first.
  c.operating.currentMask &= membersMask(n);
  c.operating.voltageMask &= membersMask(n);
  const bool changed = n != count();
  const auto result = change(c, uint8_t(membersMask(config_.count) ^ membersMask(n)));
  if (result == Result::Ok && changed) voltageCommissioned_ = false;
  return result;
}
int8_t PsuController::groupIndex(const char* name) const {
  for (uint8_t i = 0; i < limits::maxGroups; ++i)
    if (config_.groups[i].name[0] && sameName(name, config_.groups[i].name)) return i;
  return -1;
}
Result PsuController::setGroup(const char* name, uint8_t mask) {
  if (!groupName(name) || !mask || (mask & ~membersMask(count()))) return Result::Invalid;
  int8_t index = groupIndex(name);
  if (index < 0) for (uint8_t i = 0; i < limits::maxGroups; ++i)
    if (!config_.groups[i].name[0]) { index = i; break; }
  if (index < 0) return Result::Invalid;
  auto c = config_; auto& g = c.groups[index];
  const uint8_t changed = g.members == mask ? 0 : g.members | mask;
  memset(g.name, 0, sizeof(g.name)); strcpy(g.name, name); g.members = mask;
  // Preserve the requested totals; membership changes invalidate authorization.
  // A new explicit apply previews the new shares; no live writes happen here.
  return change(c, changed);
}
Result PsuController::removeGroup(uint8_t g) {
  if (g >= limits::maxGroups || !config_.groups[g].name[0]) return Result::Invalid;
  auto c = config_; uint8_t mask = c.groups[g].members; c.groups[g] = {};
  return change(c, mask);
}
Result PsuController::bind(uint8_t slot, const Identity& id, uint16_t rating, uint32_t now) {
  if (slot >= count() || !identified(id)) return Result::Invalid;
  const Psu* d = discovery_.identity(id, now);
  if (!d || !d->verified(now) || !d->live) return Result::Blocked;
  auto c = config_; c.units[slot] = {id, rating};
  c.operating.voltageMask &= uint8_t(~(1U << slot));
  const auto r = change(c, uint8_t(1U << slot));
  if (r == Result::Ok) {
    voltageSynced_ &= uint8_t(~(1U << slot)); observedEpoch_[slot] = 0;
    voltageCommissioned_ = false;
    report_.units[slot] = {};
  }
  return r;
}
Result PsuController::unbind(uint8_t slot) {
  if (slot >= PSU_MAX_UNITS) return Result::Invalid;
  auto c = config_; c.units[slot].identity = {};
  c.operating.voltageMask &= uint8_t(~(1U << slot));
  const auto result = change(c, uint8_t(1U << slot));
  if (result == Result::Ok) {
    voltageSynced_ &= uint8_t(~(1U << slot)); observedEpoch_[slot] = 0;
    report_.units[slot] = {};
    if (slot < count()) voltageCommissioned_ = false;
  }
  return result;
}
Result PsuController::setVoltage(uint16_t v, bool offline) {
  auto c = config_; if (offline) c.offlineVoltage = v; else c.voltage = v; return change(c);
}
Result PsuController::setCurrent(uint8_t g, uint16_t total, bool offline) {
  if (checkCurrent(g, total).issue != Issue::None) return Result::Invalid;
  auto c = config_; if (offline) c.groups[g].offlineCurrent = total; else c.groups[g].current = total;
  return change(c);
}
CurrentCheck PsuController::checkCurrent(uint8_t g, uint16_t total) const {
  CurrentCheck result = {};
  if (g >= limits::maxGroups || !config_.groups[g].members) { result.issue = Issue::Invalid; return result; }
  auto group = config_.groups[g]; group.current = total;
  for (uint8_t i = 0; i < count(); ++i) if (group.members & (1U << i)) {
    const uint16_t maximum = currentMaximum(config_.units[i].ratedCurrent);
    const uint16_t share = allocation(group, i);
    if (share > maximum) return {Issue::Capacity, i, share, maximum};
  }
  return result;
}
uint16_t PsuController::groupCurrentMaximum(uint8_t group) const {
  if (group >= limits::maxGroups || !config_.groups[group].members) return 0;
  uint16_t low = 0, high = uint16_t(population(config_.groups[group].members)) * limits::maxCurrent;
  // Allocation is monotonic. Reuse validation rather than approximating a sum
  // of ratings (which is wrong for equally shared groups with unequal ratings).
  while (low < high) {
    const uint16_t mid = low + (uint32_t(high) - low + 1) / 2;
    if (checkCurrent(group, mid).issue == Issue::None) low = mid;
    else high = mid - 1;
  }
  return low;
}
void PsuController::reconcileSessions(const Configuration& next) {
  for (uint8_t i = 0; i < PSU_MAX_UNITS; ++i)
    if (next.deploymentId != config_.deploymentId || i >= next.count ||
        !sameIdentity(next.units[i].identity, config_.units[i].identity)) {
      sessionAh_[i] = 0; sessionSeen_ &= uint8_t(~(1U << i));
    }
}
Result PsuController::setAutoResume(bool on) { auto c = config_; c.autoResume = on; return change(c); }
Result PsuController::setPollInterval(uint16_t ms) { auto c = config_; c.pollMs = ms; return change(c); }
const Psu* PsuController::deviceForSlot(uint8_t slot, uint32_t now) const {
  return slot < count() ? discovery_.identity(config_.units[slot].identity, now) : nullptr;
}
Issue PsuController::issue(uint8_t i, uint32_t now) const {
  if (config_.deploymentId != deployment::id) return Issue::DeploymentMismatch;
  if (i >= count()) return Issue::Invalid;
  if (!identified(config_.units[i].identity)) return Issue::Unbound;
  if (groupFor(config_, i) < 0) return Issue::Unassigned;
  // Distinguish duplicate identity from absence: it must never be overrideable.
  for (uint8_t j = 0; j < Discovery::capacity; ++j) {
    const auto& d = discovery_.device(j);
    if (d.conflict && d.fresh(now, limits::identityLeaseMs) && sameIdentity(d.identity, config_.units[i].identity)) return Issue::Conflict;
  }
  const Psu* d = deviceForSlot(i, now);
  if (!d || !d->fresh(now, staleMs())) return Issue::Missing;
  if (!d->verified(now)) return Issue::IdentityPending;
  const auto& t = d->telemetry;
  if (!t.dataSeen || uint32_t(now - t.lastData) > staleMs()) return Issue::NoTelemetry;
  if (!t.broadcastSeen || uint32_t(now - t.lastBroadcast) > staleMs()) return Issue::NoTelemetry;
  if (!t.ready) return Issue::NotReady;
  return Issue::None;
}
GroupStatus PsuController::groupStatus(uint8_t g, uint32_t now) const {
  GroupStatus s = {};
  if (g >= limits::maxGroups) return s;
  s.members = config_.groups[g].members; s.requestedCurrent = config_.groups[g].current;
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
  return d && d->verified(now) && d->fresh(now, staleMs()) &&
      (d->telemetry.freshMask & (1U << m)) && uint32_t(now - d->telemetry.updated[m]) <= staleMs() && metric(slot, m, value, now);
}
ApplyPlan PsuController::preview(Operation op, int8_t group, uint32_t now, bool partial) const {
  ApplyPlan p = {}; p.operation = op; p.group = group; p.created = now;
  p.configurationRevision = revision_; p.topologyRevision = discovery_.revision(); p.partial = partial;
  if (!ready_) p.blocker = Issue::Invalid;
  if (busy()) p.blocker = Issue::Busy;
  if (config_.deploymentId != deployment::id) p.blocker = Issue::DeploymentMismatch;
  p.members = membersMask(count()); p.voltage = op == Operation::Offline ? config_.offlineVoltage : config_.voltage;
  if (op == Operation::GroupCurrent || op == Operation::Restore) {
    if (group < 0 || group >= limits::maxGroups || !config_.groups[group].members) { p.blocker = Issue::Invalid; return p; }
    p.members = config_.groups[group].members; p.voltage = config_.operating.voltage;
    if (!config_.operating.voltageAuthorized || (op == Operation::GroupCurrent && !voltageCommissioned_)) p.blocker = Issue::VoltageUnsynced;
  } else if (partial) p.blocker = Issue::Invalid;
  for (uint8_t i = 0; i < count(); ++i) if (p.members & (1U << i)) {
    const int8_t g = groupFor(config_, i);
    p.current[i] = op == Operation::Restore ? config_.operating.current[i] :
        g >= 0 ? allocation(config_.groups[g], i, op == Operation::Offline) : 0;
    p.issues[i] = issue(i, now);
    if (p.issues[i] == Issue::Missing) p.missing |= 1U << i;
    if (p.issues[i] == Issue::None && op == Operation::GroupCurrent && !(voltageSynced_ & (1U << i))) p.issues[i] = Issue::VoltageUnsynced;
    if (p.issues[i] != Issue::None) {
      if (!(partial && op == Operation::GroupCurrent && p.issues[i] == Issue::Missing)) p.blocker = p.issues[i];
      continue;
    }
    const Psu* d = deviceForSlot(i, now);
    p.addresses[i] = d->address; p.epochs[i] = d->epoch; p.recipients |= 1U << i;
  }
  if (!p.recipients && p.blocker == Issue::None) p.blocker = Issue::Missing;
  return p;
}
Result PsuController::queue(const ApplyPlan& p, bool confirmed, uint32_t now) {
  if (busy()) return Result::Busy;
  if (p.operation == Operation::Restore) return Result::Invalid; // Internal only.
  if (p.configurationRevision != revision_ || p.topologyRevision != discovery_.revision() ||
      uint32_t(now - p.created) > limits::confirmationMs) return Result::Changed;
  const ApplyPlan fresh = preview(p.operation, p.group, now, p.partial);
  if (fresh.blocker != Issue::None) return Result::Blocked;
  if (fresh.recipients != p.recipients || fresh.missing != p.missing) return Result::Changed;
  if (p.partial && p.missing && !confirmed) return Result::ConfirmRequired;
  job_ = fresh;
  if (p.operation == Operation::All || p.operation == Operation::Voltage) {
    config_.operating.voltage = fresh.voltage; config_.operating.voltageAuthorized = true;
    config_.operating.voltageMask = fresh.recipients;
    voltageCommissioned_ = false;
    voltageSynced_ = 0; running_ |= fresh.recipients;
  }
  if (p.operation == Operation::All || p.operation == Operation::GroupCurrent) {
    currentSynced_ &= uint8_t(~fresh.recipients);
    for (uint8_t i = 0; i < count(); ++i) if (fresh.recipients & (1U << i)) config_.operating.current[i] = fresh.current[i];
    config_.operating.currentMask |= fresh.recipients; running_ |= fresh.recipients;
  }
  for (uint8_t i = 0; i < count(); ++i) if (fresh.recipients & (1U << i)) observedEpoch_[i] = fresh.epochs[i];
  ++revision_; restoreBlocked_ &= uint8_t(~fresh.recipients); restorePending_ &= uint8_t(~fresh.recipients);
  beginReport(p.operation, fresh.members, fresh.recipients, fresh.missing);
  pending_ = fresh.recipients; waiting_ = secondPhase_ = verifying_ = identityChecked_ = false;
  return Result::Ok;
}
void PsuController::beginReport(Operation op, uint8_t requested, uint8_t recipients, uint8_t skipped) {
  report_.sequence = ++reportSequence_;
  report_.active = true; report_.operation = op;
  report_.requested = requested; report_.recipients = recipients; report_.skipped = skipped;
  report_.succeeded = report_.failed = 0;
  // Keep the last command result for units outside this operation. A later
  // successful group must not erase another group's failure from diagnostics.
  for (uint8_t i = 0; i < count(); ++i) if (recipients & (1U << i))
    report_.units[i] = {CommandState::Queued, 0, 0};
}
bool PsuController::send(const CanFrame& f) {
  if (ready_ && transport_.send(f)) return true;
  if (txFailures_ != UINT16_MAX) ++txFailures_;
  return false;
}
void PsuController::advance(bool success) {
  const uint8_t bit = uint8_t(1U << jobSlot_);
  if (!success) { report_.failed |= bit; restoreBlocked_ |= bit; }
  else if (secondPhase_ || job_.operation == Operation::GroupCurrent || job_.operation == Operation::Voltage ||
      (job_.operation == Operation::Restore && !(config_.operating.currentMask & bit))) report_.succeeded |= bit;
  pending_ &= uint8_t(~bit); waiting_ = verifying_ = identityChecked_ = false;
}
void PsuController::receive(const CanFrame& f, uint32_t now) {
  discovery_.receive(f, now);
  int8_t slot = -1;
  for (uint8_t i = 0; i < count(); ++i) {
    const auto* d = deviceForSlot(i, now);
    if (d && d->address == protocol::address(f.id)) { slot = i; break; }
  }
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
    if (d && d->epoch == job_.epochs[jobSlot_] && d->verified(now) && issue(jobSlot_, now) == Issue::None)
      identityChecked_ = true;
  }
  if (waiting_ && slot == jobSlot_ && protocol::isReply(f) && protocol::command(f.id) == protocol::setCommand &&
      (f.data[0] == 1 || f.data[0] == 0x21) && f.data[1] == report_.units[jobSlot_].reg &&
      protocol::readBigEndian(f.data + 4) == expected_) {
    const auto* d = deviceForSlot(jobSlot_, now);
    if (d && d->epoch == job_.epochs[jobSlot_] && d->verified(now) && issue(jobSlot_, now) == Issue::None) {
      const bool ok = f.data[0] == 1;
      report_.units[jobSlot_].state = ok ? CommandState::Success : CommandState::Rejected;
      if (ok && f.data[1] == protocol::OnlineVoltage) {
        voltageSynced_ |= 1U << jobSlot_;
        if (voltageSynced_ == membersMask(count())) voltageCommissioned_ = true;
      }
      if (ok && f.data[1] == protocol::OnlineCurrent) currentSynced_ |= 1U << jobSlot_;
      advance(ok);
    }
  }
  if (observer_) observer_(context_, slot, f);
}
void PsuController::runJob(uint32_t now) {
  if (waiting_) {
    const auto* d = deviceForSlot(jobSlot_, now);
    if (!d || d->epoch != job_.epochs[jobSlot_] || issue(jobSlot_, now) != Issue::None) {
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
        if (!d || d->epoch != job_.epochs[i] || issue(i, now) != Issue::None) {
          report_.units[i].state = CommandState::Changed;
          report_.failed |= 1U << i; report_.succeeded &= uint8_t(~(1U << i));
          restoreBlocked_ |= 1U << i;
        }
      }
      if (!report_.failed) {
        secondPhase_ = true; pending_ = job_.recipients;
        if (job_.operation == Operation::Restore) pending_ &= config_.operating.currentMask;
      } else if (job_.operation == Operation::All) voltageCommissioned_ = false;
    }
    if (!pending_) {
      report_.active = false;
      const uint8_t incomplete = job_.recipients & uint8_t(~(report_.failed | report_.succeeded));
      report_.skipped |= incomplete;
      for (uint8_t i = 0; i < count(); ++i) if (incomplete & (1U << i)) report_.units[i].state = CommandState::Incomplete;
      return;
    }
  }
  if (uint32_t(now - lastSend_) < limits::commandGapMs) return;
  for (jobSlot_ = 0; !(pending_ & (1U << jobSlot_)); ++jobSlot_) {}
  const auto* d = deviceForSlot(jobSlot_, now);
  if (!d || d->epoch != job_.epochs[jobSlot_] || d->address != job_.addresses[jobSlot_] || issue(jobSlot_, now) != Issue::None) {
    report_.units[jobSlot_].state = CommandState::Changed; advance(false); return;
  }
  const bool current = secondPhase_ || job_.operation == Operation::GroupCurrent;
  const bool offline = job_.operation == Operation::Offline;
  const uint8_t reg = current ? (offline ? protocol::OfflineCurrent : protocol::OnlineCurrent) :
      (offline ? protocol::OfflineVoltage : protocol::OnlineVoltage);
  expected_ = current ? protocol::encodeCurrent(job_.current[jobSlot_], config_.units[jobSlot_].ratedCurrent) : protocol::encodeVoltage(job_.voltage);
  if (!verifying_) {
    report_.units[jobSlot_] = {CommandState::VerifyingIdentity, reg, expected_};
    verifying_ = true; identityChecked_ = false; verificationStarted_ = now;
    if (!send(protocol::request(d->address, protocol::infoCommand))) {
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
  lastSend_ = now; waiting_ = send(protocol::setting(d->address, reg, expected_));
  if (!waiting_) { report_.units[jobSlot_].state = CommandState::TransportError; advance(false); }
}
void PsuController::restore(uint32_t now) {
  for (uint8_t i = 0; i < count(); ++i) {
    const auto* d = deviceForSlot(i, now);
    const uint32_t epoch = d && issue(i, now) == Issue::None ? d->epoch : 0;
    if (epoch != observedEpoch_[i]) {
      voltageSynced_ &= uint8_t(~(1U << i));
      currentSynced_ &= uint8_t(~(1U << i));
      if (running_ & (1U << i)) restorePending_ |= 1U << i;
      // A real disconnect/identity epoch transition permits one recovery attempt.
      restoreBlocked_ &= uint8_t(~(1U << i));
      observedEpoch_[i] = epoch;
    }
  }
  if (busy() || !restorePending_) return;
  // Binding/adopting or adding slots never authorizes voltage by itself. Keep
  // the pending installation observable until an explicit complete voltage apply.
  if (config_.operating.voltageMask != membersMask(count())) return;
  // Global voltage is verified across the complete installation before restore.
  for (uint8_t i = 0; i < count(); ++i) if (issue(i, now) != Issue::None) return;
  const uint8_t recipients = restorePending_ & uint8_t(~restoreBlocked_);
  if (!recipients) return;
  // Restore the authorized profile, never staged edits. Voltage is acknowledged
  // by every returning recipient before any authorized current is restored.
  job_ = {}; job_.operation = Operation::Restore; job_.voltage = config_.operating.voltage;
  job_.recipients = job_.members = recipients;
  for (uint8_t i = 0; i < count(); ++i) if (recipients & (1U << i)) {
    const auto* d = deviceForSlot(i, now);
    job_.addresses[i] = d->address; job_.epochs[i] = d->epoch;
    job_.current[i] = config_.operating.current[i];
  }
  pending_ = recipients; restorePending_ &= uint8_t(~recipients);
  beginReport(Operation::Restore, recipients, recipients);
  waiting_ = secondPhase_ = verifying_ = identityChecked_ = false;
}
void PsuController::tick(uint32_t now) {
  if (!ready_) return;
  CanFrame f;
  for (uint8_t n = 0; n < 32 && transport_.receive(f); ++n) receive(f, now);
  discovery_.tick(now, staleMs());
  restore(now);
  if (busy()) runJob(now);
  // Identity and telemetry refresh continue even during a long multi-unit job.
  if (uint32_t(now - lastProbe_) < limits::discoveryGapMs) return;
  lastProbe_ = now;
  probeTurn_ = !probeTurn_;
  if (scanAddress_ && probeTurn_ && !busy()) {
    send(protocol::request(scanAddress_, protocol::infoCommand));
    scanAddress_ = scanAddress_ == 127 ? 0 : scanAddress_ + 1; return;
  }
  // Skip unused discovery slots without spending a scheduling interval on each.
  uint8_t searched = 0;
  while (!discovery_.device(probeIndex_).occupied && searched++ < Discovery::capacity)
    probeIndex_ = (probeIndex_ + 1) % Discovery::capacity;
  const auto& entry = discovery_.device(probeIndex_);
  probeIndex_ = (probeIndex_ + 1) % Discovery::capacity;
  if (!entry.occupied) return;
  Psu* d = discovery_.address(entry.address);
  // Independent schedules: a long telemetry interval must not starve either
  // identity refresh or telemetry by selecting INFO on every visit.
  const uint32_t infoInterval = d->identitySamples < 2 ? limits::minPollMs : limits::identityRefreshMs;
  if (uint32_t(now - d->lastInfoRequest) >= infoInterval) {
    d->lastInfoRequest = now;
    send(protocol::request(d->address, protocol::infoCommand));
  } else if (uint32_t(now - d->lastDataRequest) >= config_.pollMs) {
    d->lastDataRequest = now;
    send(protocol::request(d->address, protocol::dataCommand));
  }
}
Result PsuController::requestDescription(uint8_t address) {
  if (!address || address > 127) return Result::Invalid;
  return send(protocol::request(address, protocol::descriptionCommand)) ? Result::Ok : Result::TransportError;
}
Result PsuController::requestPoll(uint8_t mask, uint32_t now) {
  if (!mask || (mask & ~membersMask(count()))) return Result::Invalid;
  for (uint8_t i = 0; i < count(); ++i) if (mask & (1U << i)) {
    const auto* d = deviceForSlot(i, now);
    if (!d) return Result::Blocked;
    if (!send(protocol::request(d->address))) return Result::TransportError;
  }
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
