#pragma once
#include <stdint.h>
namespace psu {
enum class ActionKind : uint8_t {
  None, OpenGroup, OpenUnit, Previous, Next, Back, Configure, EditVoltage, EditCurrent,
  Adjust, Apply, Cancel, ConfirmNo, ConfirmYes, Tools, Calibrate, TouchDebug,
  Scenarios, ScenarioPrevious, ScenarioNext, ScenarioStart, DebugRestart, DebugReset,
  SettingsChoice, SettingsItem, ProfileInclusion, ProfileName, ProfileReview, ProfileConfirm, NameCursor, NameDone,
  Recover, RecoveryReview, RecoveryConfirm, Startup, AvailabilityInclude, AvailabilityIndividuals, AvailabilityToggle, AvailabilityExclude, AvailabilityGroup, AvailabilityScroll, Controller, Stop, Enable, Diagnostics, Retry, Probe,
  AvailabilityReview, AvailabilityRemember, AvailabilityConfirm, HardStop, Alerts, OpenAlert, Acknowledge, Resume, RetrySensors
};
struct UiAction { ActionKind kind = ActionKind::None; int16_t value = 0;
  UiAction() = default;
  UiAction(ActionKind k, int16_t v = 0) : kind(k), value(v) {}
};
}
