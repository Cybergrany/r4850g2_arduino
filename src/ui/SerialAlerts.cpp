#include "SerialConsole.h"
#if PSU_ENABLE_SERIAL
#include "AlertText.h"
namespace psu {
void SerialConsole::printAlert(const AlertState& a, bool action) {
  io_.print(F("ALERT ")); io_.print(a.id);
  io_.print(alertLevel(a.code) == AlertLevel::Error ? F(" ERROR ") : F(" WARNING "));
  if (a.source < controller_.count()) {
    io_.print(F("PSU ")); io_.print(a.source + 1);
    const auto& c = controller_.configuration(); const int8_t group = groupFor(c, a.source);
    if (group >= 0) { io_.print(' '); io_.print(c.groups[group].name); }
  } else io_.print(F("SYSTEM"));
  io_.print(a.active ? F(" active ") : F(" cleared "));
  io_.print(a.acknowledged ? F("acknowledged: ") : F("unacknowledged: "));
  const auto fault = a.source < controller_.count() && a.active ? controller_.unitControl(a.source).fault : CommandState::Idle;
  io_.print(alertCause(a.code, fault));
  if (action && a.active) { io_.print(F("; ")); io_.print(alertAction(a.code, fault)); }
  io_.println();
}
void SerialConsole::cancelAlertBroadcast() {
  if (view_ != View::AlertBroadcast) return;
  pendingAlertSources_ = 0; view_ = View::None; lastAlertBroadcast_ = now_;
}
bool SerialConsole::broadcastAlerts(uint32_t now) {
  if (!alertInterval_ || (length_ && !discard_) || (view_ != View::None && view_ != View::AlertBroadcast)) return false;
  const auto& alerts = controller_.alerts();
  if (view_ == View::None) {
    if (uint32_t(now - lastAlertBroadcast_) < alertInterval_) return false;
    lastAlertBroadcast_ = now;
    const uint16_t changed = uint16_t(alerts.revision() - alertNotice_);
    uint16_t active = 0, recent = 0; uint8_t warnings = 0, errors = 0;
    for (uint8_t i = 0; i < AlertManager::Count; ++i) {
      const auto a = alerts.at(i);
      if (a.active) {
        active |= uint16_t(1) << i;
        if (alertLevel(a.code) == AlertLevel::Error) ++errors; else ++warnings;
      } else if (!a.acknowledged && uint16_t(alerts.revision() - a.id) < changed) recent |= uint16_t(1) << i;
    }
    // No event queue or catch-up bursts. Include a cleared incident once if it
    // was previously active, or appeared and cleared between summaries.
    pendingAlertSources_ = active | previousAlertSources_ | recent;
    previousAlertSources_ = active; alertNotice_ = alerts.revision();
    if (!pendingAlertSources_) return false;
    view_ = View::AlertBroadcast;
    beginOutput(); io_.print(F("ALERTS uptime-ms=")); io_.print(now);
    io_.print(F(" active WARNING=")); io_.print(warnings); io_.print(F(" ERROR=")); io_.print(errors);
    if (!active) io_.print(F(" (all clear)"));
    io_.println(F("; details: alerts / diag thermal")); return true;
  }
  // One bounded row per cooperative tick. Re-read live state so a change or
  // acknowledgement during a slow report is reflected when its row is printed.
  int8_t next = -1; uint8_t priority = 0;
  for (uint8_t i = 0; i < AlertManager::Count; ++i) if (pendingAlertSources_ & (uint16_t(1) << i)) {
    const auto a = alerts.at(i);
    if (a.code == AlertCode::None) { pendingAlertSources_ &= ~(uint16_t(1) << i); continue; }
    const uint8_t rank = uint8_t(alertLevel(a.code)) + (a.active ? 2 : 0);
    if (next < 0 || rank > priority) { next = i; priority = rank; }
  }
  if (next >= 0) {
    beginOutput(); printAlert(alerts.at(next), true);
    pendingAlertSources_ &= ~(uint16_t(1) << next);
  }
  if (!pendingAlertSources_) {
    view_ = View::None; lastAlertBroadcast_ = now; prompt();
  }
  return true;
}
}
#endif
