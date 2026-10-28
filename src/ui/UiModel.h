#pragma once
#include "../psu/Presentation.h"
#include "../storage/MemoryManager.h"
#include "../config/UiConfig.h"
#include "UiAction.h"
namespace psu {
struct UiInput { int16_t delta; bool clicked, held; };
enum class UiPage : uint8_t { Groups, Units, Detail, Config, Confirm, Result, Editor, Tools, Calibration, TouchDebug, Scenarios,
  Availability, AvailabilityUnits, AvailabilityConfirm, AvailabilityResult };
enum class UiField : uint8_t { Voltage, Current, Tools };
enum class UiNotice : uint8_t {
  None, Changed, Busy, Blocked, Applying, Saving, Saved, PartialSaved,
  ApplyFailed, SaveFailed, SavedOlder, Cancelled
};
class UiModel {
 public:
  UiModel(PsuController& controller, MemoryManager& memory) : c_(controller), memory_(memory) {}
  void tick(uint32_t now);
  void input(UiInput event, uint32_t now);
  void action(UiAction action, uint32_t now);
  void resetNavigation();
  UiAction takeRequest() { const auto r = request_; request_ = {}; return r; }
  uint8_t toolSelection() const { return toolSelection_; }
  uint8_t scenarioSelection() const { return scenarioSelection_; }
  uint8_t availabilitySelection() const { return availabilitySelection_; }
  uint8_t availabilitySelected() const { return availabilitySelected_; }
  AvailabilityAction availabilityAction() const { return availabilityAction_; }
  const AvailabilityPlan& availabilityPlan() const { return availabilityPlan_; }
  Result availabilityResult() const { return availabilityResult_; }
  bool availabilityRemembered() const { return availabilityRemembered_; }
  bool availabilityPage() const { return page_ >= UiPage::Availability; }
  bool startupProbe() const { return c_.startupStatus().permit == PermitState::Inhibited && !c_.startupStatus().configurationReady; }
  bool startupFault() const { return c_.startupStatus().permit == PermitState::Fault; }
  bool startupRetry() const { return startupFault() || !c_.ready(); }
  bool cutoffFitted() const { return c_.startupStatus().permit != PermitState::NotFitted; }
  void nextGroupPage(); // Screen-only builds cycle the overview without issuing commands.
  UiPage page() const { return page_; }
  UiField field() const { return field_; }
  UiNotice notice() const { return notice_; }
  Issue blocker() const { return blocker_; }
  int8_t group() const { return group_; }
  uint8_t unit() const { return unit_; }
  bool editing() const { return editing_; }
  bool confirmingYes() const { return confirmYes_; }
  uint16_t editValue() const { return edit_; }
  uint16_t minimum() const { return field_ == UiField::Voltage ? limits::minVoltage : 0; }
  uint16_t maximum() const { return field_ == UiField::Voltage ? limits::maxVoltage : c_.groupCurrentMaximum(group_); }
  bool working() const { return notice_ == UiNotice::Applying || notice_ == UiNotice::Saving; }
  uint8_t groupCount() const;
  uint8_t groupRank() const;
  int8_t groupAt(uint8_t rank) const;
  uint8_t unitRank() const;
  uint8_t unitCount() const { return group_ < 0 ? 0 : population(c_.configuration().groups[group_].members); }
  uint8_t unitAt(uint8_t rank) const;
  const ApplyPlan& plan() const { return plan_; }
  StorageResult storageResult() const { return storageResult_; }
  uint8_t succeeded() const { return succeeded_; }
  uint8_t failed() const { return failed_; }
 private:
  void refresh(uint32_t now);
  void commit(uint32_t now);
  void queue(uint32_t now, bool confirmed);
  void result(UiNotice notice, Issue issue = Issue::None);
  void selectAvailability(uint32_t now);
  void reviewAvailability(uint8_t mask, uint32_t now);
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
  uint8_t succeeded_ = 0, failed_ = 0;
  uint16_t edit_ = 0, saveToken_ = 0;
  uint32_t editRevision_ = 0, operationSequence_ = 0, operationRevision_ = 0;
  bool editing_ = false, confirmYes_ = false;
  uint8_t toolSelection_ = 0, scenarioSelection_ = 0;
  UiAction request_;
  AvailabilityPlan availabilityPlan_;
  AvailabilityAction availabilityAction_ = AvailabilityAction::Accept;
  uint8_t availabilitySelected_ = 0, availabilitySelection_ = 0;
  uint32_t availabilitySequence_ = 0;
  Result availabilityResult_ = Result::Ok;
  bool availabilityWorking_ = false, availabilityRemembered_ = false;
};
}
