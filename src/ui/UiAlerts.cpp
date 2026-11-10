#include "UiModel.h"
namespace psu {
uint8_t UiModel::alertSourceAt(uint8_t rank) const {
  for (uint8_t i = 0; i < AlertManager::Count; ++i) if (c_.alerts().visible(i) && !rank--) return i;
  return AlertManager::Count;
}
void UiModel::notifications(uint32_t now) {
  if (alertOverlay_) {
    const auto current = c_.alerts().at(displayedAlert_.source);
    if (alertOverlay_ == 2 && !displayedAlert_.acknowledged && current.acknowledged) { dismissAlert(); return; }
#if !PSU_ENABLE_TOUCH && !PSU_ENABLE_ENCODER
    if (uint32_t(now - alertSince_) >= alerts::monitorPopupMs) { dismissAlert(); page_ = UiPage::Groups; }
#endif
    return;
  }
  if (page_ == UiPage::Calibration || recovering()) return;
  const uint16_t id = c_.alerts().notification(now);
  const int8_t source = c_.alerts().find(id);
  if (source < 0) return;
  alertResult_ = Result::Ok; displayedAlert_ = c_.alerts().at(source); alertOverlay_ = 2; alertSelection_ = 0; alertSince_ = now;
}
bool UiModel::alertAction(UiAction a, uint32_t now) {
  if (a.kind == ActionKind::Alerts) {
    alertOverlay_ = 1; alertSelection_ = 0; alertSince_ = now; return true;
  }
  if (!alertOverlay_) return false;
  if (now == alertSince_) return true;
  if (a.kind == ActionKind::Back) { dismissAlert(); return true; }
  if (alertOverlay_ == 1) {
    const uint8_t count = c_.alerts().count();
    if (count && (a.kind == ActionKind::Previous || a.kind == ActionKind::Next))
      alertSelection_ = (alertSelection_ + (a.kind == ActionKind::Next ? 1 : count - 1)) % count;
    if (a.kind == ActionKind::OpenAlert) {
      const uint8_t source = alertSourceAt(a.value);
      if (source < AlertManager::Count) {
        alertResult_ = Result::Ok; displayedAlert_ = c_.alerts().at(source); alertOverlay_ = 2; alertSelection_ = 0; alertSince_ = now;
      }
    }
    return true;
  }
  // Never let a stale button acknowledge/resume the incident replacing it.
  const auto current = c_.alerts().at(displayedAlert_.source);
  if (current.id != displayedAlert_.id) {
    if (a.kind == ActionKind::Acknowledge || a.kind == ActionKind::Resume || a.kind == ActionKind::RetrySensors || a.kind == ActionKind::Recover) {
      displayedAlert_ = current; alertSince_ = now; alertSelection_ = 0;
    }
    return true;
  }
  if (a.kind == ActionKind::Acknowledge) { c_.alerts().acknowledge(displayedAlert_.id); dismissAlert(); }
  if (a.kind == ActionKind::Recover) openRecovery(now);
  if (a.kind == ActionKind::Resume && displayedAlert_.code == AlertCode::ThermalStop)
    alertResult_ = c_.setAutoResume(true); // Clear-save is asynchronous. Detail shows its status.
  if (a.kind == ActionKind::RetrySensors && displayedAlert_.code == AlertCode::TemperatureSensor && displayedAlert_.source < c_.count())
    alertResult_ = c_.retryTemperature(UnitMask(1U << displayedAlert_.source), now);
  return true;
}
}
