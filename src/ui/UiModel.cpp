#include "UiModel.h"
namespace psu {
namespace {
uint8_t move(uint8_t current, int16_t delta, uint8_t count) {
  if (!count) return 0;
  return (int32_t(current) + delta % count + count) % count;
}
}
uint8_t UiModel::groupCount() const {
  uint8_t n = 0; for (const auto& g : c_.configuration().groups) if (g.members) ++n; return n;
}
int8_t UiModel::groupAt(uint8_t rank) const {
  for (uint8_t i = 0; i < limits::maxGroups; ++i)
    if (c_.configuration().groups[i].members && !rank--) return i;
  return -1;
}
uint8_t UiModel::groupRank() const {
  uint8_t rank = 0;
  for (int8_t i = 0; i < group_; ++i) if (c_.configuration().groups[i].members) ++rank;
  return rank;
}
uint8_t UiModel::unitAt(uint8_t rank) const {
  if (group_ < 0) return 0;
  for (uint8_t i = 0; i < c_.count(); ++i)
    if ((c_.configuration().groups[group_].members & (1U << i)) && !rank--) return i;
  return 0;
}
uint8_t UiModel::unitRank() const {
  return group_ < 0 ? 0 : population(c_.configuration().groups[group_].members & uint8_t((1U << unit_) - 1));
}
uint16_t UiModel::draft() const {
  return field_ == UiField::Voltage ? c_.configuration().voltage : c_.configuration().groups[group_].current;
}
void UiModel::result(UiNotice n, Issue issue) {
  notice_ = n; blocker_ = issue; editing_ = false; page_ = UiPage::Result;
}
void UiModel::refresh(uint32_t now) {
  if ((group_ < 0 && groupCount()) || (group_ >= 0 && !c_.configuration().groups[group_].members)) {
    const bool cancelled = editing_ || page_ == UiPage::Confirm;
    group_ = groupAt(0); unit_ = unitAt(0); editing_ = false; page_ = UiPage::Groups;
    if (cancelled) result(UiNotice::Changed);
  }
  if (group_ >= 0 && !(c_.configuration().groups[group_].members & (1U << unit_))) unit_ = unitAt(0);
  if ((editing_ || page_ == UiPage::Confirm) && c_.configurationRevision() != editRevision_)
    result(UiNotice::Changed);
  if (page_ == UiPage::Confirm && (uint32_t(now - plan_.created) > limits::confirmationMs ||
      plan_.topologyRevision != c_.discovery().revision())) result(UiNotice::Changed);
}
void UiModel::tick(uint32_t now) {
  refresh(now);
  if (notice_ == UiNotice::Applying) {
    const auto& r = c_.report();
    if (r.sequence != operationSequence_ || c_.configurationRevision() != operationRevision_) {
      result(UiNotice::Changed); return; // Never save someone else's operation.
    }
    succeeded_ = r.succeeded; failed_ = r.failed;
    if (r.active) return;
    if (r.failed || r.succeeded != plan_.recipients) { result(UiNotice::ApplyFailed); return; }
    storageResult_ = memory_.startSave(c_.configuration());
    if (storageResult_ != StorageResult::Ok) { result(UiNotice::SaveFailed); return; }
    saveToken_ = memory_.saveToken();
    notice_ = UiNotice::Saving;
    return; // Let the display show SAVING before the first EEPROM byte.
  }
  if (notice_ == UiNotice::Saving) {
    if (memory_.completedToken() == saveToken_) {
      storageResult_ = memory_.completedResult();
      result(storageResult_ != StorageResult::Ok ? UiNotice::SaveFailed :
          c_.configurationRevision() != operationRevision_ ? UiNotice::SavedOlder :
          plan_.missing ? UiNotice::PartialSaved : UiNotice::Saved);
    }
  }
}
void UiModel::nextGroupPage() {
  if (page_ == UiPage::Groups && groupCount())
    group_ = groupAt((groupRank() / ui::groupsPerPage * ui::groupsPerPage + ui::groupsPerPage) % groupCount());
}
void UiModel::queue(uint32_t now, bool confirmed) {
  const auto r = c_.queue(plan_, confirmed, now);
  if (r != Result::Ok) {
    result(r == Result::Changed ? UiNotice::Changed : r == Result::Busy ? UiNotice::Busy : UiNotice::Blocked,
           c_.preview(plan_.operation, plan_.group, now, plan_.partial).blocker);
    return;
  }
  operationSequence_ = c_.report().sequence; operationRevision_ = c_.configurationRevision();
  succeeded_ = failed_ = 0;
  result(UiNotice::Applying);
}
void UiModel::commit(uint32_t now) {
  if (c_.configurationRevision() != editRevision_) { result(UiNotice::Changed); return; }
  const auto r = field_ == UiField::Voltage ? c_.setVoltage(edit_) : c_.setCurrent(group_, edit_);
  if (r != Result::Ok) { result(r == Result::Busy ? UiNotice::Busy : UiNotice::Blocked, Issue::Invalid); return; }
  editing_ = false; editRevision_ = c_.configurationRevision();
  plan_ = c_.preview(field_ == UiField::Voltage ? Operation::Voltage : Operation::GroupCurrent, group_, now);
  if (plan_.blocker != Issue::None) {
    if (field_ == UiField::Current && plan_.missing) {
      const auto partial = c_.preview(Operation::GroupCurrent, group_, now, true);
      if (partial.blocker == Issue::None) {
        plan_ = partial; confirmYes_ = false; notice_ = UiNotice::None; page_ = UiPage::Confirm; return;
      }
    }
    result(UiNotice::Blocked, plan_.blocker); return;
  }
  queue(now, false);
}
void UiModel::input(UiInput event, uint32_t now) {
  refresh(now); // Revalidate input without advancing EEPROM a second time this loop.
  if (event.held) {
    editing_ = false;
    if (page_ == UiPage::Groups && group_ >= 0 && !working()) { page_ = UiPage::Config; notice_ = UiNotice::None; }
    else { page_ = UiPage::Groups; }
    return;
  }
  if (page_ == UiPage::Groups) {
    if (event.delta && groupCount()) group_ = groupAt(move(groupRank(), event.delta, groupCount()));
    if (event.clicked && group_ >= 0) {
      if (!(c_.configuration().groups[group_].members & (1U << unit_))) unit_ = unitAt(0);
      page_ = UiPage::Units;
    }
  } else if (page_ == UiPage::Units) {
    if (event.delta) unit_ = unitAt(move(unitRank(), event.delta, population(c_.configuration().groups[group_].members)));
    if (event.clicked) page_ = UiPage::Detail;
  } else if (page_ == UiPage::Detail) {
    if (event.clicked) page_ = UiPage::Units;
  } else if (page_ == UiPage::Config) {
    if (event.delta) {
      if (editing_) {
        int32_t value = int32_t(edit_) + int32_t(event.delta) * (field_ == UiField::Voltage ? ui::voltageStep : ui::currentStep);
        edit_ = value < minimum() ? minimum() : value > maximum() ? maximum() : value;
      } else if (event.delta % 2) field_ = field_ == UiField::Voltage ? UiField::Current : UiField::Voltage;
    }
    if (event.clicked) {
      if (editing_) commit(now);
      else if (working() || c_.busy() || memory_.saving()) result(UiNotice::Busy);
      else { edit_ = draft(); editRevision_ = c_.configurationRevision(); editing_ = true; notice_ = UiNotice::None; }
    }
  } else if (page_ == UiPage::Confirm) {
    if (event.delta % 2) confirmYes_ = !confirmYes_;
    if (event.clicked) { if (confirmYes_) queue(now, true); else result(UiNotice::Cancelled); }
  } else if (page_ == UiPage::Result && event.clicked && !working()) {
    page_ = group_ < 0 ? UiPage::Groups : UiPage::Config;
  }
}
}
