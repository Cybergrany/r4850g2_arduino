#include "Discovery.h"
#include "../config/ControlConfig.h"
#include <string.h>
namespace psu {
uint32_t Discovery::changed(uint32_t now) {
  changedAt_ = now; settled_ = false;
  return ++revision_;
}
uint32_t Discovery::settleRemaining(uint32_t now) const {
  if (settled_) return 0;
  if (negotiating_) return control::addressStableMs;
  const uint32_t elapsed = uint32_t(now - changedAt_);
  return elapsed < control::addressStableMs ? control::addressStableMs - elapsed : 0;
}
uint32_t Discovery::quietRemaining(uint32_t now) const {
  if (!negotiating_) return 0;
  const uint32_t elapsed = uint32_t(now - changedAt_);
  return elapsed < control::negotiationQuietMs ? control::negotiationQuietMs - elapsed : 0;
}
void Discovery::invalidate(uint32_t now) {
  for (auto& d : devices_) d = {};
  changed(now);
}
Psu* Discovery::address(uint8_t a) {
  for (auto& d : devices_) if (d.occupied && d.address == a) return &d;
  return nullptr;
}
int8_t Discovery::identityIndex(const Identity& id, uint32_t now) const {
  if (!identified(id)) return -1;
  int8_t found = -1;
  for (uint8_t i = 0; i < capacity; ++i) {
    const auto& d = devices_[i];
    if (!d.fresh(now, limits::identityLeaseMs) || !sameIdentity(id, d.identity)) continue;
    if (found >= 0) return -2; // Never choose one of two live copies.
    found = i;
  }
  return found;
}
const Psu* Discovery::identity(const Identity& id, uint32_t now) const {
  const int8_t index = identityIndex(id, now);
  return index < 0 ? nullptr : &devices_[index];
}
void Discovery::receive(const CanFrame& f, uint32_t now) {
  if (protocol::isNegotiation(f)) {
    if (!negotiating_) invalidate(now);
    settled_ = false; changedAt_ = now;
    negotiating_ = true; return;
  }
  // Do not corroborate routes from traffic emitted before negotiation settles.
  // Both INFO samples must belong to the new stable address epoch.
  if (negotiating_) return;
  if ((!protocol::isReply(f) && !protocol::isCurrentBroadcast(f)) || !protocol::address(f.id)) { ++ignored_; return; }
  const uint8_t a = protocol::address(f.id);
  Psu* d = address(a);
  if (!d) {
    for (auto& entry : devices_) if (!entry.occupied || !entry.fresh(now, limits::identityLeaseMs * 2)) {
      d = &entry; *d = {}; d->occupied = true; d->address = a; d->epoch = changed(now); break;
    }
    if (!d) { overflowEvent_ = true; if (overflow_ != UINT16_MAX) ++overflow_; return; }
  }
  if (!d->fresh(now, limits::identityLeaseMs)) {
    d->identitySamples = 0; d->telemetry = {}; d->epoch = changed(now);
  }
  const bool hadAlarm = d->telemetry.alarmSeen;
  const uint32_t previousAlarm = d->telemetry.alarmBits;
  d->receive(f, now);
  if (protocol::isReply(f) && protocol::command(f.id) == protocol::dataCommand && f.data[0] == 1 && f.data[1] == 0x83 &&
      (((previousAlarm ^ d->telemetry.alarmBits) & (uint32_t(1) << 29)) ||
       (!hadAlarm && (d->telemetry.alarmBits & (uint32_t(1) << 29))))) {
    d->epoch = changed(now);
    d->telemetry.freshMask = 0;
    d->telemetry.dataSeen = d->telemetry.broadcastSeen = false;
  }
  if (protocol::command(f.id) != protocol::infoCommand || f.data[0] != 0 || f.data[1] != 2) return;
  Identity id; memcpy(id.bytes, f.data + 2, 6);
  if (!identified(id)) { d->identitySamples = 0; d->epoch = changed(now); return; }
  if (!sameIdentity(id, d->identity)) {
    d->identity = id; d->identitySamples = 1; d->telemetry = {}; d->epoch = changed(now);
  } else if (d->identitySamples < 2 && uint32_t(now - d->lastIdentity) >= 100) {
    ++d->identitySamples; d->epoch = changed(now);
  }
  d->lastIdentity = now;
}
void Discovery::tick(uint32_t now, uint32_t staleMs) {
  if (negotiating_ && uint32_t(now - changedAt_) >= control::negotiationQuietMs) {
    negotiating_ = false; changed(now);
  }
  // Compute each pair once. No route/telemetry is cached here: expiry and
  // duplicate detection still run on every tick, including timer rollover.
  uint16_t freshIdentities = 0, conflicts = 0;
  static_assert(capacity <= 16, "Discovery pair masks must fit");
  for (uint8_t i = 0; i < capacity; ++i)
    if (devices_[i].fresh(now, limits::identityLeaseMs) && identified(devices_[i].identity)) freshIdentities |= uint16_t(1) << i;
  for (uint8_t i = 0; i < capacity; ++i) if (freshIdentities & (uint16_t(1) << i))
    for (uint8_t j = i + 1; j < capacity; ++j)
      if ((freshIdentities & (uint16_t(1) << j)) && sameIdentity(devices_[i].identity, devices_[j].identity))
        conflicts |= (uint16_t(1) << i) | (uint16_t(1) << j);
  for (uint8_t i = 0; i < capacity; ++i) {
    auto& d = devices_[i];
    if (!d.occupied) continue;
    // Expire freshness permanently until that field is received again. Keeping
    // only a timestamp would let a months-old value revive after millis wraps.
    for (uint8_t m = 0; m < protocol::MetricCount; ++m)
      if (uint32_t(now - d.telemetry.updated[m]) > staleMs) d.telemetry.freshMask &= uint16_t(~(1U << m));
    if (d.telemetry.alarmSeen && uint32_t(now - d.telemetry.lastAlarm) > staleMs) d.telemetry.alarmSeen = false;
    const bool live = d.fresh(now, staleMs);
    const bool conflict = conflicts & (uint16_t(1) << i);
    if (d.live != live || d.conflict != conflict) {
      d.live = live; d.conflict = conflict; d.epoch = changed(now);
      if (!live) { d.identitySamples = 0; d.telemetry.dataSeen = false; }
    }
  }
  // Latch completion so a long-stable bus does not become unsettled at rollover.
  if (!negotiating_ && !settled_ && uint32_t(now - changedAt_) >= control::addressStableMs) settled_ = true;
}
}
