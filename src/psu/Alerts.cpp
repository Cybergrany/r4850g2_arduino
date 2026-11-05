#include "Alerts.h"
namespace psu {
AlertLevel alertLevel(AlertCode c) {
  if (c == AlertCode::None) return AlertLevel::None;
  return c >= AlertCode::ActiveLost && c != AlertCode::TemperatureWarning ? AlertLevel::Error : AlertLevel::Warning;
}
void AlertManager::reset(uint8_t source) { if (source < Count) cells_[source] = {}; }
void AlertManager::set(uint8_t source, AlertCode code, uint32_t now) {
  if (source >= Count) return;
  auto& c = cells_[source];
  if (code == AlertCode::None) {
    if (c.flags & Active) { c.flags &= uint8_t(~Active); c.changed = now; }
    else if (uint32_t(now - c.changed) >= alerts::rearmMs) c.flags |= Armed;
    return;
  }
  const bool escalation = (alertLevel(code) == AlertLevel::Error && !(c.flags & PeakError)) || (code == AlertCode::ThermalStop && !(c.flags & PeakStop));
  const bool newIncident = c.code == AlertCode::None || (c.flags & Armed) || escalation;
  if (newIncident || code != c.code) { if (++sequence_ == 0) ++sequence_; c.id = sequence_; }
  if (newIncident) c.flags = Unacked | (escalation ? Urgent : 0);
  c.flags = (c.flags | Active) & uint8_t(~Armed); c.code = code;
  if (alertLevel(code) == AlertLevel::Error) c.flags |= PeakError;
  if (code == AlertCode::ThermalStop) c.flags |= PeakStop;
}
void AlertManager::event(uint8_t source, AlertCode code, uint32_t now) {
  set(source, code, now); if (source < Count) cells_[source].changed = now;
}
void AlertManager::expire(uint8_t source, uint32_t now) {
  if (source < Count && uint32_t(now - cells_[source].changed) >= alerts::recentMs) set(source, AlertCode::None, now);
}
AlertState AlertManager::at(uint8_t source) const {
  AlertState s; s.source = source;
  if (source < Count) { const auto& c = cells_[source]; s.id = c.id; s.code = c.code; s.active = c.flags & Active; s.acknowledged = !(c.flags & Unacked); }
  return s;
}
bool AlertManager::visible(uint8_t source) const { return source < Count && (cells_[source].flags & (Active | Unacked)); }
uint8_t AlertManager::count() const { uint8_t n = 0; for (uint8_t i = 0; i < Count; ++i) if (visible(i)) ++n; return n; }
bool AlertManager::warning() const {
  for (const auto& c : cells_) if ((c.flags & Active) && alertLevel(c.code) == AlertLevel::Warning) return true;
  return false;
}
bool AlertManager::error() const {
  for (const auto& c : cells_) if ((c.flags & Active) && alertLevel(c.code) == AlertLevel::Error) return true;
  return false;
}
int8_t AlertManager::find(uint16_t id) const { for (uint8_t i = 0; i < Count; ++i) if (id && cells_[i].id == id && visible(i)) return i; return -1; }
bool AlertManager::acknowledge(uint16_t id) {
  const int8_t i = find(id); if (i < 0) return false;
  cells_[i].flags = (cells_[i].flags | Announced) & uint8_t(~(Unacked | Urgent)); return true;
}
void AlertManager::acknowledgeAll() { for (uint8_t i = 0; i < Count; ++i) acknowledge(cells_[i].id); }
void AlertManager::deferPendingNotifications() {
  for (auto& c : cells_) c.flags = (c.flags | Announced) & uint8_t(~Urgent);
}
uint16_t AlertManager::notification(uint32_t now) {
  int8_t best = -1;
  for (uint8_t i = 0; i < Count; ++i) {
    const auto& c = cells_[i];
    if (!(c.flags & Unacked) || (c.flags & Announced)) continue;
    if (popped_ && uint32_t(now - lastPopup_) < alerts::popupMs && !(c.flags & Urgent)) continue;
    if (best < 0 || alertLevel(c.code) > alertLevel(cells_[best].code) || (c.flags & Urgent)) best = i;
  }
  if (best < 0) return 0;
  cells_[best].flags = (cells_[best].flags | Announced) & uint8_t(~Urgent);
  lastPopup_ = now; popped_ = true; return cells_[best].id;
}
}
