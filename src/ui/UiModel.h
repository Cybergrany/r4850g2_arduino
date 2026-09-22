#pragma once
#include "../psu/Presentation.h"
#include "../storage/MemoryManager.h"
#include "../config/UiConfig.h"
namespace psu {
struct UiInput { int16_t delta; bool clicked, held; };
enum class UiPage : uint8_t { Groups, Units, Detail, Config, Confirm, Result };
enum class UiField : uint8_t { Voltage, Current };
enum class UiNotice : uint8_t {
  None, Changed, Busy, Blocked, Applying, Saving, Saved, PartialSaved,
  ApplyFailed, SaveFailed, SavedOlder, Cancelled
};
class UiModel {
 public:
  UiModel(PsuController& controller, MemoryManager& memory) : c_(controller), memory_(memory) {}
  void tick(uint32_t now);
  void input(UiInput event, uint32_t now);
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
};
}
