#include "UiModel.h"
#include "UiLayout.h"
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
  if (page_ == UiPage::Availability && group_ >= 0 && !working()) {
    auto action = AvailabilityAction::Accept;
    for (uint8_t i = 0; i < c_.count(); ++i)
      if ((c_.configuration().groups[group_].members & c_.excluded() & (1U << i)) &&
          c_.availabilityReason(i, now) == AvailabilityReason::Ready) action = AvailabilityAction::Include;
    if (action != availabilityAction_) { availabilityAction_ = action; selectAvailability(now); }
  }
  if (availabilityWorking_) {
    const auto& r = c_.availabilityReport();
    if (r.sequence != availabilitySequence_) { availabilityWorking_ = false; notice_ = UiNotice::Changed; availabilityResult_ = Result::Changed; return; }
    if (r.saving) return;
    availabilityWorking_ = false; notice_ = UiNotice::None;
    availabilityResult_ = r.result; availabilityRemembered_ = r.remembered; storageResult_ = r.storage;
    return;
  }
  if (page_ == UiPage::AvailabilityConfirm &&
      (availabilityPlan_.configurationRevision != c_.configurationRevision() ||
       availabilityPlan_.topologyRevision != c_.discovery().revision() ||
       availabilityPlan_.availabilityRevision != c_.availabilityRevision() ||
       uint32_t(now - availabilityPlan_.created) > limits::confirmationMs)) {
    availabilityResult_ = Result::Changed; availabilityRemembered_ = false;
    page_ = UiPage::AvailabilityResult; return;
  }
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
          (plan_.missing || plan_.excluded) ? UiNotice::PartialSaved : UiNotice::Saved);
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
  if (availabilityPage() || page_ == UiPage::Tools) {
    if (event.held) { action({ActionKind::Back}, now); return; }
    UiButton buttons[UiLayout::capacity]; const uint8_t count = UiLayout::buttons(*this, buttons);
    auto& selected = page_ == UiPage::Tools ? toolSelection_ : availabilitySelection_;
    if (event.delta) selected = move(selected, event.delta, count);
    if (selected >= count) selected = 0;
    if (event.clicked && count) action(buttons[selected].action, now);
    return;
  }
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
    if (page_==UiPage::Groups && group_<0) { page_=UiPage::Tools; return; }
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
      } else field_ = UiField(move(uint8_t(field_), event.delta, 3));
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
  availabilityWorking_ = false; availabilitySelection_ = availabilitySelected_ = 0;
}
void UiModel::selectAvailability(uint32_t now) {
  availabilitySelected_ = 0; availabilitySelection_ = 0; unit_ = unitAt(0);
  if (group_ < 0) return;
  const uint8_t members = c_.configuration().groups[group_].members;
  for (uint8_t i = 0; i < c_.count(); ++i) if (members & (1U << i)) {
    const auto reason = c_.availabilityReason(i, now);
    if ((availabilityAction_ == AvailabilityAction::Accept && (reason == AvailabilityReason::NoCan || reason == AvailabilityReason::NoAc)) ||
        (availabilityAction_ == AvailabilityAction::Include && (c_.excluded() & (1U << i)) && reason == AvailabilityReason::Ready) ||
        (availabilityAction_ == AvailabilityAction::Require && (c_.acceptedAbsences() & (1U << i)))) availabilitySelected_ |= 1U << i;
  }
}
void UiModel::reviewAvailability(uint8_t mask, uint32_t now) {
  availabilityPlan_ = c_.previewAvailability(availabilityAction_, mask, false, now);
  availabilitySelection_ = 0; page_ = UiPage::AvailabilityConfirm;
}
void UiModel::confirmAvailability(uint32_t now) {
  const uint32_t previous = c_.availabilityReport().sequence;
  availabilityResult_ = c_.confirmAvailability(availabilityPlan_, memory_, now);
  availabilityRemembered_ = false;
  storageResult_ = c_.availabilityReport().sequence == previous ? StorageResult::Ok : c_.availabilityReport().storage;
  availabilitySequence_ = c_.availabilityReport().sequence;
  availabilityWorking_ = availabilityResult_ == Result::Ok && c_.availabilitySaving();
  if (availabilityResult_ == Result::Ok) availabilityRemembered_ = c_.availabilityReport().remembered;
  notice_ = availabilityWorking_ ? UiNotice::Saving : UiNotice::None;
  availabilitySelection_ = 0; page_ = UiPage::AvailabilityResult;
}
void UiModel::action(UiAction a, uint32_t now) {
  refresh(now);
  if (availabilityWorking_ && a.kind != ActionKind::HardStop) return;
  switch (a.kind) {
    case ActionKind::Startup:
      if (working()) { page_ = UiPage::Result; return; }
      editing_ = false; availabilityAction_ = AvailabilityAction::Accept;
      if (group_ >= 0) for (uint8_t i = 0; i < c_.count(); ++i)
        if ((c_.configuration().groups[group_].members & c_.excluded() & (1U << i)) && c_.availabilityReason(i, now) == AvailabilityReason::Ready)
          availabilityAction_ = AvailabilityAction::Include;
      selectAvailability(now); page_ = UiPage::Availability; notice_ = UiNotice::None; break;
    case ActionKind::AvailabilityPrimary:
      if (startupRetry()) { c_.reconcile(); break; }
      if (startupProbe()) { availabilityAction_ = AvailabilityAction::Probe; reviewAvailability(0, now); break; }
      if (group_ >= 0) reviewAvailability(c_.configuration().groups[group_].members, now);
      break;
    case ActionKind::AvailabilityIndividuals:
      selectAvailability(now); page_ = UiPage::AvailabilityUnits; break;
    case ActionKind::AvailabilityToggle:
      if (a.value >= 0 && a.value < unitCount()) availabilitySelected_ ^= 1U << unitAt(a.value);
      break;
    case ActionKind::AvailabilityMode:
      availabilityAction_ = AvailabilityAction((uint8_t(availabilityAction_) + 1) % 3);
      selectAvailability(now); break;
    case ActionKind::AvailabilityReview: reviewAvailability(availabilitySelected_, now); break;
    case ActionKind::AvailabilityRemember:
      if (availabilityPlan_.action == AvailabilityAction::Accept || availabilityPlan_.action == AvailabilityAction::Require)
        availabilityPlan_.remember = !availabilityPlan_.remember;
      break;
    case ActionKind::AvailabilityConfirm: confirmAvailability(now); break;
    case ActionKind::HardStop:
      availabilityResult_ = c_.hardStop(); availabilityRemembered_ = false;
      storageResult_ = StorageResult::Ok;
      if (!availabilityWorking_) { notice_ = UiNotice::None; availabilitySelection_ = 0; page_ = UiPage::AvailabilityResult; }
      break;
    case ActionKind::OpenGroup:
      if (a.value < 0 || a.value >= groupCount()) return;
      editing_ = false; group_ = groupAt(a.value); unit_ = unitAt(0); page_ = UiPage::Units; break;
    case ActionKind::OpenUnit:
      if (group_ < 0 || a.value < 0 || a.value >= population(c_.configuration().groups[group_].members)) return;
      unit_ = unitAt(a.value); page_ = UiPage::Detail; break;
    case ActionKind::Previous: case ActionKind::Next: {
      const int8_t direction = a.kind == ActionKind::Next ? 1 : -1;
      if (page_ == UiPage::Availability && groupCount()) {
        group_ = groupAt(move(groupRank(), direction, groupCount()));
        action({ActionKind::Startup}, now); break;
      }
      if (page_ == UiPage::AvailabilityUnits && group_ >= 0) {
        const uint8_t pages = (unitCount() + 1) / 2;
        unit_ = unitAt(move(unitRank() / 2, direction, pages) * 2); break;
      }
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
      if (availabilityPage()) { page_ = page_ == UiPage::Availability ? UiPage::Groups : UiPage::Availability; availabilitySelection_ = 0; notice_ = UiNotice::None; break; }
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
