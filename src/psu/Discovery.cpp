#include "Discovery.h"
#include <string.h>
namespace psu {
Psu* Discovery::address(uint8_t a) {
  for (auto& d : devices_) if (d.occupied && d.address == a) return &d;
  return nullptr;
}
const Psu* Discovery::identity(const Identity& id, uint32_t now) const {
  if (!identified(id)) return nullptr;
  const Psu* found = nullptr;
  for (const auto& d : devices_) if (d.fresh(now, limits::identityLeaseMs) && sameIdentity(id, d.identity)) {
    if (found) return nullptr; // Same identity at two live addresses is ambiguous.
    found = &d;
  }
  return found;
}
void Discovery::receive(const CanFrame& f, uint32_t now) {
  if ((!protocol::isReply(f) && !protocol::isCurrentBroadcast(f)) || !protocol::address(f.id)) { ++ignored_; return; }
  const uint8_t a = protocol::address(f.id);
  Psu* d = address(a);
  if (!d) {
    for (auto& entry : devices_) if (!entry.occupied || !entry.fresh(now, limits::identityLeaseMs * 2)) {
      d = &entry; *d = {}; d->occupied = true; d->address = a; d->epoch = ++revision_; break;
    }
    if (!d) { if (overflow_ != UINT16_MAX) ++overflow_; return; }
  }
  if (!d->fresh(now, limits::identityLeaseMs)) {
    d->identitySamples = 0; d->telemetry = {}; d->epoch = ++revision_;
  }
  d->receive(f, now);
  if (protocol::command(f.id) != protocol::infoCommand || f.data[0] != 0 || f.data[1] != 2) return;
  Identity id; memcpy(id.bytes, f.data + 2, 6);
  if (!identified(id)) { d->identitySamples = 0; d->epoch = ++revision_; return; }
  if (!sameIdentity(id, d->identity)) {
    d->identity = id; d->identitySamples = 1; d->telemetry = {}; d->epoch = ++revision_;
  } else if (d->identitySamples < 2 && uint32_t(now - d->lastIdentity) >= 100) {
    ++d->identitySamples; d->epoch = ++revision_;
  }
  d->lastIdentity = now;
}
void Discovery::tick(uint32_t now, uint32_t staleMs) {
  for (auto& d : devices_) {
    if (!d.occupied) continue;
    const bool live = d.fresh(now, staleMs);
    bool conflict = false;
    for (const auto& other : devices_)
      if (&d != &other && d.fresh(now, limits::identityLeaseMs) && other.fresh(now, limits::identityLeaseMs) &&
          identified(d.identity) && sameIdentity(d.identity, other.identity)) conflict = true;
    if (d.live != live || d.conflict != conflict) {
      d.live = live; d.conflict = conflict; d.epoch = ++revision_;
      if (!live) { d.identitySamples = 0; d.telemetry.dataSeen = false; }
    }
  }
}
}
