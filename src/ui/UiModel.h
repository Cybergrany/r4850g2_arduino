#pragma once
#include "../psu/Presentation.h"
#include "../storage/MemoryManager.h"
#include "../config/UiConfig.h"
#include "UiAction.h"
#include "UiRecovery.h"
namespace psu {
struct UiInput { int16_t delta; bool clicked, held; };
enum class UiPage : uint8_t { Groups, Units, Detail, Config, Confirm, Result, Editor, Tools, Calibration, TouchDebug, Scenarios,
  Availability, AvailabilityUnits, AvailabilityConfirm, AvailabilityResult, Alerts, AlertDetail, Recovery, RecoveryConfirm,
  ConfigMenu, GroupSelect, UnitSelect, ProfileList, ProfileOptions, ProfileName, ProfileReview, ScopeConfirm };
enum class UiField : uint8_t { Voltage, Current, Tools };
enum class UiNotice : uint8_t {
  None, Changed, Busy, Blocked, Applying, Saving, Saved, PartialSaved,
  ApplyFailed, SaveFailed, SavedOlder, Cancelled, ConfigSaved, ProfileSaved, Prepared
};
class UiModel {
 public:
  UiModel(PsuController& controller, MemoryManager& memory) : c_(controller), memory_(memory) {}
  void tick(uint32_t now);
  void notifications(uint32_t now);
  void input(UiInput event, uint32_t now);
  void action(UiAction action, uint32_t now);
  void resetNavigation();
  UiAction takeRequest() { const auto r = request_; request_ = {}; return r; }
  uint8_t toolSelection() const { return toolSelection_; }
  uint8_t scenarioSelection() const { return scenarioSelection_; }
  uint8_t availabilitySelection() const { return availabilitySelection_; }
  UnitMask availabilitySelected() const { return availabilitySelected_; }
  AvailabilityAction availabilityAction() const { return availabilityAction_; }
  const AvailabilityPlan& availabilityPlan() const { return availabilityPlan_; }
  Result availabilityResult() const { return availabilityResult_; }
  bool availabilityRemembered() const { return availabilityRemembered_; }
  bool availabilityPage() const { return !alertOverlay_ && (page_ == UiPage::Availability || page_ == UiPage::AvailabilityUnits ||
      page_ == UiPage::AvailabilityConfirm || page_ == UiPage::AvailabilityResult); }
  bool alertPage() const { return alertOverlay_ != 0; }
  uint8_t alertSelection() const { return alertSelection_; }
  Result alertResult() const { return alertResult_; }
  AlertState displayedAlert() const { return displayedAlert_; }
  uint16_t alertDisplayId() const { return alertOverlay_ == 2 ? displayedAlert_.id : 0; }
  uint8_t alertSourceAt(uint8_t rank) const;
  void dismissAlert() { c_.alerts().deferPendingNotifications(); alertOverlay_ = 0; }
  bool startupProbe() const { return c_.startupStatus().permit == PermitState::Inhibited && !c_.startupStatus().configurationReady; }
  bool startupFault() const { return c_.startupStatus().permit == PermitState::Fault; }
  bool startupRetry() const { return startupFault() || !c_.ready(); }
  bool cutoffFitted() const { return c_.startupStatus().permit != PermitState::NotFitted; }
  void nextGroupPage(); // Screen-only builds cycle the overview without issuing commands.
  UiPage page() const { return alertOverlay_ == 1 ? UiPage::Alerts : alertOverlay_ == 2 ? UiPage::AlertDetail : page_; }
  UiField field() const { return field_; }
  UiNotice notice() const { return notice_; }
  Issue blocker() const { return blocker_; }
  int8_t group() const { return group_; }
  uint8_t unit() const { return unit_; }
  bool editing() const { return editing_; }
  bool confirmingYes() const { return confirmYes_; }
  uint16_t editValue() const { return edit_; }
  uint16_t minimum() const { return field_ == UiField::Voltage ? limits::minVoltage : 0; }
  uint16_t maximum() const { return c_.settingMaximum(scope_, field_ == UiField::Voltage ? SettingFields::Voltage : SettingFields::Current); }
  SettingScope scope() const { return scope_; }
  SettingSummary configured(SettingFields field) const { return settingSummary(c_.configuration(), scope_, field); }
  UnitMask settingsMembers() const { return scopeMembers(c_.configuration(), scope_); }
  bool settingsPage() const;
  uint8_t settingsSelection() const { return settingsSelection_; }
  uint8_t listPage() const { return listPage_; }
  uint8_t listCount() const;
  uint8_t listRows() const;
  bool profileOperation() const { return profileFlow_; }
  bool savingProfile() const { return saveProfileChoice_; }
  bool profileInclusion() const { return profileReview_.restoreInclusion; }
  bool storedInclusion() const;
  bool profileSummary(uint16_t& minV, uint16_t& maxV, uint16_t& totalA) const;
  uint8_t profileSlot() const { return profileReview_.slot; }
  const char* profileName() const { return profileName_; }
  uint8_t nameCursor() const { return nameCursor_; }
  bool nameEditing() const { return nameEditing_; }
  StorageResult profileInfo(uint8_t slot, ProfileInfo& info) const { return memory_.profileInfo(slot, info); }
  const ProfileReview& profileReview() const { return profileReview_; }
  bool working() const { return notice_ == UiNotice::Applying || notice_ == UiNotice::Saving; }
  uint8_t groupCount() const;
  uint8_t groupRank() const;
  int8_t groupAt(uint8_t rank) const;
  uint8_t unitRank() const;
  uint8_t unitCount() const { return group_ < 0 ? 0 : population(c_.configuration().groups[group_].members); }
  uint8_t unitAt(uint8_t rank) const;
  const ApplyPlan& plan() const { return plan_; }
  StorageResult storageResult() const { return storageResult_; }
  UnitMask succeeded() const { return succeeded_; }
  UnitMask failed() const { return failed_; }
  RecoveryLink recoveryLink() const;
  bool recovering() const { return recovery_.active; }
  RecoveryKind recoveryKind() const { return recovery_.kind; }
  AlertCode recoveryCause() const { return recovery_.incident.code; }
  uint8_t recoverySlot() const { return recovery_.slot; }
  uint8_t recoverySelection() const { return recovery_.selection; }
  RecoveryCommand recoveryCommand() const { return recovery_.command; }
  RecoveryCommand availableRecoveryCommand() const;
  Result recoveryResult() const { return recovery_.result; }
  bool recoverySaving() const { return recovery_.saveToken != 0; }
  bool recoverySaved() const { return recovery_.saved; }
 private:
  void openRecovery(uint32_t now, RecoveryLink link = {RecoveryKind::None, PSU_MAX_UNITS});
  __attribute__((noinline)) bool recoveryAction(UiAction action, uint32_t now);
  void returnFromRecovery();
  void reviewRecovery(RecoveryCommand command, uint32_t now);
  bool recoveryFresh(uint32_t now) const;
  void recoveryTick(uint32_t now);
  struct Recovery {
    uint32_t revision = 0, topology = 0, availability = 0, since = 0, report = 0;
    uint32_t editRevision = 0, operation = 0;
    uint16_t edit = 0, saveToken = 0;
    AlertState incident;
    UiPage page = UiPage::Groups, reviewPage = UiPage::Recovery;
    UiField field = UiField::Current;
    SettingScope scope;
    UiNotice notice = UiNotice::None;
    Issue blocker = Issue::None;
    int8_t group = -1;
    uint8_t unit = 0, overlay = 0, alertSelection = 0, slot = 0, selection = 0;
    RecoveryKind kind = RecoveryKind::None;
    RecoveryCommand command = RecoveryCommand::None;
    Result result = Result::Ok;
    bool active = false, editing = false, saved = false;
  } recovery_;
  uint32_t uiNow_ = 0;
  __attribute__((noinline)) bool alertAction(UiAction action, uint32_t now);
  uint32_t alertSince_ = 0;
  AlertState displayedAlert_;
  Result alertResult_ = Result::Ok;
  uint8_t alertOverlay_ = 0, alertSelection_ = 0;
  void refresh(uint32_t now);
  void commit(uint32_t now);
  // Keep mutually exclusive navigation/profile temporaries off each other's
  // AVR stack paths, including when LTO is enabled.
  __attribute__((noinline)) bool settingsAction(UiAction action, uint32_t now);
  __attribute__((noinline)) void navigationAction(UiAction action, uint32_t now);
  void settingsInput(UiInput input, uint32_t now);
  void openSettings(SettingScope scope);
  __attribute__((noinline)) void reviewProfile(uint32_t now);
  __attribute__((noinline)) void confirmProfile(uint32_t now);
  void chooseProfile(uint8_t slot, uint32_t now);
  void settingsTick(uint32_t now);
  void queue(uint32_t now, bool confirmed);
  void result(UiNotice notice, Issue issue = Issue::None);
  void selectAvailability(uint32_t now);
  void reviewAvailability(UnitMask mask, uint32_t now);
  void confirmAvailability(uint32_t now);
  uint16_t draft() const;
  PsuController& c_;
  MemoryManager& memory_;
  ApplyPlan plan_ = {};
  UiPage page_ = UiPage::Groups;
  UiField field_ = UiField::Current;
  UiNotice notice_ = UiNotice::None;
  Issue blocker_ = Issue::None;
  StorageResult storageResult_ = StorageResult::Ok;
  int8_t group_ = -1;
  uint8_t unit_ = 0;
  UnitMask succeeded_ = 0, failed_ = 0;
  uint16_t edit_ = 0, saveToken_ = 0;
  uint32_t editRevision_ = 0, operationSequence_ = 0, operationRevision_ = 0;
  bool editing_ = false, confirmYes_ = false;
  uint8_t toolSelection_ = 0, scenarioSelection_ = 0;
  UiAction request_;
  AvailabilityPlan availabilityPlan_;
  AvailabilityAction availabilityAction_ = AvailabilityAction::Accept;
  UnitMask availabilitySelected_ = 0;
  uint8_t availabilitySelection_ = 0;
  uint32_t availabilitySequence_ = 0;
  Result availabilityResult_ = Result::Ok;
  bool availabilityWorking_ = false, availabilityRemembered_ = false;
  SettingScope scope_{ScopeKind::Group, 0};
  ProfileReview profileReview_;
  char profileName_[profiles::nameLength + 1] = {};
  char nameBefore_[profiles::nameLength + 1] = {};
  uint8_t settingsSelection_ = 0, listPage_ = 0, nameCursor_ = 0;
  bool saveProfileChoice_ = false, nameEditing_ = false, profileFlow_ = false, profileLoading_ = false;
};
}
