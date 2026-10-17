#include "UiModel.h"
#include "../debug/Simulator.h"
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
  if (page_ == UiPage::Tools || page_ == UiPage::Scenarios) {
    if (event.held) { action({ActionKind::Back}, now); return; }
    if (page_ == UiPage::Scenarios) {
#if PSU_UI_DEBUG
      if (event.delta) scenarioSelection_ = move(scenarioSelection_, event.delta, uint8_t(DebugScenario::Count) + 2);
      if (event.clicked) action({ActionKind::ScenarioStart}, now);
#endif
    } else {
      const uint8_t count = (PSU_ENABLE_TOUCH ? 2 : 0) + (PSU_UI_DEBUG ? 1 : 0);
      if (event.delta) toolSelection_ = move(toolSelection_, event.delta, count);
      if (event.clicked && count) action({PSU_ENABLE_TOUCH && toolSelection_ == 0 ? ActionKind::Calibrate :
          PSU_ENABLE_TOUCH && toolSelection_ == 1 ? ActionKind::TouchDebug : ActionKind::Scenarios}, now);
    }
    return;
  }
  if (page_ == UiPage::Calibration || page_ == UiPage::TouchDebug) {
    if (event.held) action({ActionKind::Back}, now);
    return;
  }
  if (event.held) {
    if (page_==UiPage::Groups && group_<0 && (PSU_UI_DEBUG || PSU_ENABLE_TOUCH)) { page_=UiPage::Tools; return; }
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
  } else if (page_ == UiPage::Config || page_ == UiPage::Editor) {
    if (event.delta) {
      if (editing_) {
        int32_t value = int32_t(edit_) + int32_t(event.delta) * (field_ == UiField::Voltage ? ui::voltageStep : ui::currentStep);
        edit_ = value < minimum() ? minimum() : value > maximum() ? maximum() : value;
      } else field_ = UiField(move(uint8_t(field_), event.delta, (PSU_ENABLE_TOUCH || PSU_UI_DEBUG) ? 3 : 2));
    }
    if (event.clicked) {
      if (!editing_ && field_ == UiField::Tools) { action({ActionKind::Tools}, now); return; }
      if (editing_) commit(now);
      else if (working() || c_.busy() || memory_.saving()) result(UiNotice::Busy);
      else { edit_ = draft(); editRevision_ = c_.configurationRevision(); editing_ = true; notice_ = UiNotice::None;
#if PSU_ENABLE_TOUCH
        page_ = UiPage::Editor;
#endif
      }
    }
  } else if (page_ == UiPage::Confirm) {
    if (event.delta % 2) confirmYes_ = !confirmYes_;
    if (event.clicked) { if (confirmYes_) queue(now, true); else result(UiNotice::Cancelled); }
  } else if (page_ == UiPage::Result && event.clicked && !working()) {
    page_ = group_ < 0 ? UiPage::Groups : UiPage::Config;
  }
}
}

namespace psu {
void UiModel::resetNavigation() {
  page_ = UiPage::Groups; field_ = UiField::Current; notice_ = UiNotice::None;
  group_ = -1; unit_ = toolSelection_ = 0; editing_ = confirmYes_ = false; request_ = {};
}
void UiModel::action(UiAction a, uint32_t now) {
  refresh(now);
  switch (a.kind) {
    case ActionKind::OpenGroup:
      if (a.value < 0 || a.value >= groupCount()) return;
      editing_ = false; group_ = groupAt(a.value); unit_ = unitAt(0); page_ = UiPage::Units; break;
    case ActionKind::OpenUnit:
      if (group_ < 0 || a.value < 0 || a.value >= population(c_.configuration().groups[group_].members)) return;
      unit_ = unitAt(a.value); page_ = UiPage::Detail; break;
    case ActionKind::Previous: case ActionKind::Next: {
      const int8_t direction = a.kind == ActionKind::Next ? 1 : -1;
      if (page_ == UiPage::Groups && groupCount()) {
        const uint8_t pages = (groupCount()+ui::groupsPerPage-1)/ui::groupsPerPage;
        group_ = groupAt(move(groupRank()/ui::groupsPerPage, direction, pages)*ui::groupsPerPage);
      } else if (page_ == UiPage::Units && group_ >= 0) {
        const uint8_t pages = (population(c_.configuration().groups[group_].members)+ui::unitsPerPage-1)/ui::unitsPerPage;
        unit_ = unitAt(move(unitRank()/ui::unitsPerPage, direction, pages)*ui::unitsPerPage);
      }
      break;
    }
    case ActionKind::Configure:
      if (group_ >= 0 && !working()) { page_ = UiPage::Config; field_ = UiField::Current; editing_ = false; } break;
    case ActionKind::EditVoltage: case ActionKind::EditCurrent:
      if (group_ < 0 || working()) return;
      editing_ = false; page_ = UiPage::Config;
      field_ = a.kind == ActionKind::EditVoltage ? UiField::Voltage : UiField::Current;
      input({0,true,false}, now); break;
    case ActionKind::Adjust: if (editing_) input({a.value,false,false}, now); break;
    case ActionKind::Apply: if (editing_) commit(now); break;
    case ActionKind::Cancel: editing_ = false; page_ = group_ < 0 ? UiPage::Groups : UiPage::Config; break;
    case ActionKind::ConfirmNo: if (page_ == UiPage::Confirm) result(UiNotice::Cancelled); break;
    case ActionKind::ConfirmYes: if (page_ == UiPage::Confirm) queue(now,true); break;
    case ActionKind::Back:
      editing_ = false;
      page_ = page_ == UiPage::Detail ? UiPage::Units :
          (page_ == UiPage::Calibration || page_ == UiPage::TouchDebug || page_ == UiPage::Scenarios) ? UiPage::Tools : UiPage::Groups;
      break;
    case ActionKind::Tools: editing_ = false; page_ = UiPage::Tools; break;
    case ActionKind::Calibrate: case ActionKind::TouchDebug:
      if (working()) return; // Preserve ownership of an active apply/save completion.
      if (c_.busy() || memory_.saving()) { result(UiNotice::Busy); return; }
      editing_ = false; page_ = a.kind == ActionKind::Calibrate ? UiPage::Calibration : UiPage::TouchDebug;
      request_ = a; break;
    case ActionKind::Scenarios: editing_ = false; page_ = UiPage::Scenarios; break;
#if PSU_UI_DEBUG
    case ActionKind::ScenarioNext: case ActionKind::ScenarioPrevious:
      scenarioSelection_ = move(scenarioSelection_, a.kind == ActionKind::ScenarioNext ? 1 : -1, uint8_t(DebugScenario::Count)+2); break;
    case ActionKind::ScenarioStart:
      request_ = scenarioSelection_ == uint8_t(DebugScenario::Count) ? UiAction{ActionKind::DebugRestart} :
          scenarioSelection_ > uint8_t(DebugScenario::Count) ? UiAction{ActionKind::DebugReset} : UiAction{ActionKind::ScenarioStart,scenarioSelection_}; break;
#endif
    default: break;
  }
}
}
