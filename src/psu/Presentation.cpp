#include "Presentation.h"
namespace psu {
Reading summarize(const PsuController& c, uint8_t mask, protocol::Metric metric,
                  Aggregate aggregate, uint32_t now) {
  mask &= membersMask(c.count());
  Reading r = {0, 0, population(mask)};
  for (uint8_t i = 0; i < c.count(); ++i) if (mask & (1U << i)) {
    float value;
    if (!c.freshMetric(i, metric, value, now)) continue;
    if (aggregate == Aggregate::Maximum) { if (!r.observed || value > r.value) r.value = value; }
    else r.value += value;
    ++r.observed;
  }
  if (aggregate == Aggregate::Mean && r.observed) r.value /= r.observed;
  return r;
}
Reading sessionTotal(const PsuController& c, uint8_t mask) {
  mask &= membersMask(c.count());
  Reading r = {0, 0, population(mask)};
  for (uint8_t i = 0; i < c.count(); ++i) if (mask & (1U << i)) {
    r.value += c.sessionAmpHours(i);
    if (c.sessionObserved(i)) ++r.observed;
  }
  return r;
}
bool pendingSettings(const PsuController& c, uint8_t mask) {
  const auto& cfg = c.configuration();
  if (!cfg.operating.voltageAuthorized || cfg.voltage != cfg.operating.voltage) return true;
  for (uint8_t i = 0; i < c.count(); ++i) if (mask & (1U << i)) {
    const int8_t group = groupFor(cfg, i);
    if (!c.voltageSynchronized(i) || !c.currentSynchronized(i) ||
        !(cfg.operating.currentMask & (1U << i)) || group < 0 ||
        allocation(cfg.groups[group], i) != cfg.operating.current[i]) return true;
  }
  return false;
}
bool commandFailed(CommandState s) {
  return s == CommandState::Incomplete || s == CommandState::Rejected || s == CommandState::Timeout ||
      s == CommandState::IdentityTimeout || s == CommandState::TransportError || s == CommandState::Changed;
}
bool commandActive(CommandState s) {
  return s == CommandState::Queued || s == CommandState::VerifyingIdentity || s == CommandState::Waiting;
}
}
