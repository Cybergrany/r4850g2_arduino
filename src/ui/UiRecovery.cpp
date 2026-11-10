#include "UiModel.h"
#include "../config/Deployment.h"
namespace psu {
FlashText recoveryLabel(RecoveryKind k) {
  switch (k) {
    case RecoveryKind::Availability: return UI_TEXT("Config");
    case RecoveryKind::Unit: return UI_TEXT("Details");
    case RecoveryKind::Bus: return UI_TEXT("Diagnostics");
    case RecoveryKind::Temperatures: return UI_TEXT("Temps");
    case RecoveryKind::Storage: return UI_TEXT("Review save");
    case RecoveryKind::Calibration: return UI_TEXT("Calibrate");
    case RecoveryKind::Settings: return UI_TEXT("Settings");
    case RecoveryKind::Enable: return UI_TEXT("Enable");
    default: return UI_TEXT("Help");
  }
}
RecoveryLink UiModel::recoveryLink() const {
  RecoveryLink link = {RecoveryKind::None, PSU_MAX_UNITS};
  AlertCode code = AlertCode::None;
  if (alertOverlay_ == 2) {
    const auto live = c_.alerts().at(displayedAlert_.source);
    if (live.id != displayedAlert_.id) return link;
    code = displayedAlert_.code;
    if (displayedAlert_.source < c_.count()) link.slot = displayedAlert_.source;
  } else if (page_ == UiPage::Result && !working()) {
    if (notice_ == UiNotice::SaveFailed || notice_ == UiNotice::SavedOlder) code = AlertCode::Storage;
    else if (notice_ == UiNotice::ApplyFailed) {
      code = AlertCode::Command;
      for (uint8_t i = 0; i < c_.count(); ++i) if (failed_ & (1U << i)) { link.slot = i; break; }
    } else if (notice_ == UiNotice::Changed || notice_ == UiNotice::Cancelled) link.kind = RecoveryKind::Settings;
    else if (notice_ == UiNotice::Blocked) {
      switch (blocker_) {
        case Issue::Missing: case Issue::NoAc: case Issue::ApprovalRequired: case Issue::Settling: case Issue::NoParticipants:
          code = AlertCode::Missing; break;
        case Issue::Disabled: link.kind = RecoveryKind::Enable; break;
        case Issue::NoProfile: case Issue::VoltageUnsynced: link.kind = RecoveryKind::Settings; break;
        case Issue::ThermalStopped: code = AlertCode::ThermalStop; break;
        case Issue::TemperatureSensor: code = AlertCode::TemperatureSensor; break;
        case Issue::ControlFault: case Issue::NotReady: case Issue::Reconciling: code = AlertCode::Control; break;
        case Issue::Conflict: case Issue::IdentityPending: code = AlertCode::Identity; break;
        case Issue::Cutoff: code = AlertCode::Cutoff; break;
        case Issue::Busy: return link;
        default: link.kind = RecoveryKind::Help; break;
      }
      for (uint8_t i = 0; i < c_.count(); ++i)
        if ((plan_.members & (1U << i)) && plan_.issues[i] != Issue::None) { link.slot = i; break; }
    }
  }
  switch (code) {
    case AlertCode::Missing: case AlertCode::Excluded: case AlertCode::Returned: case AlertCode::Unstable:
      link.kind = RecoveryKind::Availability; break;
    case AlertCode::ActiveLost:
      if (link.slot < c_.count()) {
        const auto reason = c_.availabilityReason(link.slot, uiNow_);
        link.kind = reason == AvailabilityReason::NoCan || reason == AvailabilityReason::NoAc ?
            RecoveryKind::Availability : RecoveryKind::Unit;
      } else link.kind = RecoveryKind::Availability;
      break;
    case AlertCode::Command: case AlertCode::Control: case AlertCode::StopUnconfirmed:
      link.kind = RecoveryKind::Unit; break;
    case AlertCode::CanInit: case AlertCode::Cutoff: case AlertCode::ReceiveLoss: case AlertCode::DiscoveryFull: case AlertCode::Identity:
      link.kind = RecoveryKind::Bus; break;
    case AlertCode::TemperatureWarning: case AlertCode::TemperatureError: case AlertCode::ThermalStop: case AlertCode::TemperatureSensor:
      link.kind = RecoveryKind::Temperatures; break;
    case AlertCode::Storage: case AlertCode::OlderSave:
      link.kind = alertOverlay_ && displayedAlert_.source == AlertManager::LocalUi ? RecoveryKind::Calibration : RecoveryKind::Storage; break;
    case AlertCode::Touch: link.kind = RecoveryKind::Calibration; break;
    case AlertCode::Commissioning:
      link.kind = startupProbe() ? RecoveryKind::Availability : RecoveryKind::Settings;
      for (uint8_t i = 0; i < c_.count(); ++i) if (!identified(c_.configuration().units[i].identity) || groupFor(c_.configuration(), i) < 0)
        link.kind = RecoveryKind::Help;
      break;
    case AlertCode::Deployment: case AlertCode::Display: link.kind = RecoveryKind::Help; break;
    default: break;
  }
  if (link.kind == RecoveryKind::Availability && link.slot == PSU_MAX_UNITS) {
    const auto attention = c_.availabilityAttention(uiNow_);
    for (uint8_t i = 0; i < c_.count(); ++i) if (attention & (1U << i)) { link.slot = i; break; }
  }
  if (link.slot == PSU_MAX_UNITS && group_ >= 0) link.slot = unitAt(0);
  if ((link.kind == RecoveryKind::Availability || link.kind == RecoveryKind::Settings) && !groupCount()) link.kind = RecoveryKind::Help;
  if (link.kind == RecoveryKind::Unit && (link.slot >= c_.count() || groupFor(c_.configuration(), link.slot) < 0)) link.kind = RecoveryKind::Bus;
#if !PSU_ENABLE_TOUCH
  if (link.kind == RecoveryKind::Calibration) link.kind = RecoveryKind::Help;
#endif
  return link;
}
void UiModel::openRecovery(uint32_t now, RecoveryLink link) {
  // One bookmark, no modal navigation stack. An in-flight UI operation retains
  // its completion owner; show Busy rather than replacing that owner's page.
  if (working() || availabilityWorking_ || recoverySaving()) { alertResult_ = Result::Busy; return; }
  if (link.kind == RecoveryKind::None) link = recoveryLink();
  if (link.kind == RecoveryKind::None) return;
  if (!recovery_.active) {
    recovery_ = {};
    auto& r = recovery_;
    r.page = page_; r.field = field_; r.group = group_; r.unit = unit_;
    r.edit = edit_; r.editRevision = editRevision_; r.editing = editing_;
    r.notice = notice_; r.blocker = blocker_; r.operation = operationSequence_;
    r.incident = alertOverlay_ ? displayedAlert_ : AlertState{};
    r.overlay = alertOverlay_; r.alertSelection = alertSelection_;
  }
  auto& r = recovery_;
  r.active = true; r.kind = link.kind; r.slot = link.slot;
  r.result = Result::Ok; r.selection = 0; r.command = RecoveryCommand::None;
  r.revision = c_.configurationRevision();
  c_.alerts().deferPendingNotifications(); alertOverlay_ = 0;
  editing_ = false; notice_ = UiNotice::None; page_ = UiPage::Recovery;
  if (r.slot < c_.count() && groupFor(c_.configuration(), r.slot) >= 0) {
    group_ = groupFor(c_.configuration(), r.slot); unit_ = r.slot;
  } else if (group_ < 0) { group_ = groupAt(0); unit_ = unitAt(0); }
  if (r.kind == RecoveryKind::Availability) action({ActionKind::Startup}, now);
  else if (r.kind == RecoveryKind::Settings) { page_ = UiPage::Config; field_ = UiField::Voltage; }
  else if (r.kind == RecoveryKind::Unit) page_ = UiPage::Detail;
  else if (r.kind == RecoveryKind::Calibration) action({ActionKind::Calibrate}, now);
}
void UiModel::returnFromRecovery() {
  const auto r = recovery_; recovery_.active = false;
  // A deliberate apply in the recovery editor owns the new result; never
  // relabel it as the earlier failed apply or restore an obsolete confirmation.
  const bool newOperation = operationSequence_ != r.operation;
  group_ = r.group; unit_ = r.unit; field_ = r.field;
  if (!newOperation) {
    page_ = r.page; notice_ = r.notice; blocker_ = r.blocker;
    if (!r.overlay && page_ == UiPage::Result && r.saved && r.revision == c_.configurationRevision())
      notice_ = UiNotice::ConfigSaved; // A save-only recovery never claims a new apply.
    edit_ = r.edit; editRevision_ = r.editRevision; editing_ = r.editing;
    // Reuse the editor's protection-only revision exception and normal
    // configuration checks. Availability has one scratch plan; never restore
    // an earlier confirmation after that plan was used by the recovery flow.
    if (page_ == UiPage::AvailabilityConfirm) {
      page_ = UiPage::AvailabilityResult; availabilityResult_ = Result::Changed;
      availabilityRemembered_ = false; availabilitySelection_ = 0;
    }
    refresh(uiNow_);
  }
  displayedAlert_ = r.incident; alertOverlay_ = r.overlay; alertSelection_ = r.alertSelection;
  alertSince_ = uiNow_; alertResult_ = Result::Ok;
}
RecoveryCommand UiModel::availableRecoveryCommand() const {
  if (!recovering()) return RecoveryCommand::None;
  if (recovery_.kind == RecoveryKind::Storage) return RecoveryCommand::Save;
  if (recovery_.kind == RecoveryKind::Enable) return RecoveryCommand::Enable;
  if (recovery_.kind == RecoveryKind::Temperatures) {
    if (c_.thermalStatus().stop != ThermalStop::None) return RecoveryCommand::Enable;
    if (c_.thermalStatus().sensorFaults & (1U << recovery_.slot)) return RecoveryCommand::Sensors;
  }
  if (recovery_.kind == RecoveryKind::Unit || (recovery_.kind == RecoveryKind::Bus && startupRetry())) return RecoveryCommand::Retry;
  return RecoveryCommand::None;
}
void UiModel::reviewRecovery(RecoveryCommand command, uint32_t now) {
  auto& r = recovery_;
  if (working() || c_.busy() || memory_.saving()) { r.result = Result::Busy; return; }
  if (r.overlay == 2 && c_.alerts().at(r.incident.source).id != r.incident.id) { r.result = Result::Changed; return; }
  if (command == RecoveryCommand::Save && r.revision != c_.configurationRevision()) {
    r.revision = c_.configurationRevision(); r.result = Result::Changed; return;
  }
  r.command = command; r.reviewPage = page_; r.selection = 0; r.since = now;
  r.revision = c_.configurationRevision(); r.topology = c_.discovery().revision();
  r.availability = c_.availabilityRevision(); r.report = c_.report().sequence;
  r.result = Result::Ok; r.saved = false; page_ = UiPage::RecoveryConfirm;
}
bool UiModel::recoveryFresh(uint32_t now) const {
  const auto& r = recovery_;
  return uint32_t(now - r.since) <= limits::confirmationMs &&
      r.revision == c_.configurationRevision() && r.topology == c_.discovery().revision() &&
      r.availability == c_.availabilityRevision() && r.report == c_.report().sequence &&
      (r.overlay != 2 || c_.alerts().at(r.incident.source).id == r.incident.id);
}
void UiModel::recoveryTick(uint32_t now) {
  auto& r = recovery_;
  if (r.active && r.saved && r.revision != c_.configurationRevision()) {
    r.saved = false; r.result = Result::Changed;
  }
  if (r.saveToken && memory_.completedToken() == r.saveToken) {
    r.saveToken = 0;
    const auto status = memory_.completedResult();
    const bool older = r.revision != c_.configurationRevision();
    c_.storageStatus(status, older);
    r.result = status != StorageResult::Ok ? Result::Invalid : older ? Result::Changed : Result::Ok;
    r.saved = status == StorageResult::Ok && !older;
  }
  if (r.active && page_ == UiPage::RecoveryConfirm && !recoveryFresh(now)) {
    r.result = Result::Changed; r.command = RecoveryCommand::None;
    r.selection = 0; page_ = r.reviewPage;
  }
}
bool UiModel::recoveryAction(UiAction a, uint32_t now) {
  if (a.kind == ActionKind::Recover) { openRecovery(now); return true; }
  if (!recovering() && a.kind == ActionKind::AvailabilityPrimary && startupRetry()) {
    openRecovery(now, {RecoveryKind::Bus, unit_});
    if (recovering()) reviewRecovery(RecoveryCommand::Retry, now);
    return true;
  }
  if (!recovering()) return false;
  auto& r = recovery_;
  if (a.kind == ActionKind::Tools) { action({ActionKind::Back}, now); return true; }
  if (a.kind == ActionKind::Back) {
    if (working() || recoverySaving() || availabilityWorking_) { r.result = Result::Busy; return true; }
    if (page_ == UiPage::RecoveryConfirm) { page_ = r.reviewPage; r.command = RecoveryCommand::None; r.selection = 0; return true; }
    if (page_ == UiPage::Recovery || page_ == UiPage::Availability || page_ == UiPage::Detail ||
        page_ == UiPage::Config || page_ == UiPage::Editor || page_ == UiPage::Calibration || page_ == UiPage::Result ||
        page_ == UiPage::Groups || page_ == UiPage::Tools || page_ == UiPage::Units) { returnFromRecovery(); return true; }
  }
  if (page_ == UiPage::Recovery && (a.kind == ActionKind::Previous || a.kind == ActionKind::Next)) {
    r.slot = (r.slot < c_.count() ? r.slot : 0) + (a.kind == ActionKind::Next ? 1 : c_.count() - 1);
    r.slot %= c_.count(); return true;
  }
  if (a.kind == ActionKind::RecoveryReview || (a.kind == ActionKind::AvailabilityPrimary && startupRetry())) {
    const auto command = a.kind == ActionKind::RecoveryReview ? availableRecoveryCommand() : RecoveryCommand::Retry;
    if (command != RecoveryCommand::None) reviewRecovery(command, now);
    return true;
  }
  if (a.kind != ActionKind::RecoveryConfirm || page_ != UiPage::RecoveryConfirm) return false;
  if (now == r.since) return true;
  if (!recoveryFresh(now)) { r.result = Result::Changed; page_ = r.reviewPage; r.selection = 0; return true; }
  if (working() || c_.busy() || memory_.saving()) { r.result = Result::Busy; return true; }
  if (r.command == RecoveryCommand::Retry) { c_.reconcile(); r.result = Result::Ok; }
  else if (r.command == RecoveryCommand::Enable) r.result = c_.setAutoResume(true);
  else if (r.command == RecoveryCommand::Sensors) r.result = c_.retryTemperature(uint8_t(1U << r.slot), now);
  else if (r.command == RecoveryCommand::Save) {
    const auto saved = memory_.startSave(c_.configuration());
    r.result = saved == StorageResult::Ok ? Result::Ok : saved == StorageResult::Busy ? Result::Busy : Result::Invalid;
    if (saved == StorageResult::Ok) r.saveToken = memory_.saveToken();
    else c_.storageStatus(saved);
  }
  r.command = RecoveryCommand::None; r.selection = 0; page_ = r.reviewPage;
  return true;
}
}
