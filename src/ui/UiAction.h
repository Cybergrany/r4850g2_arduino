#pragma once
#include <stdint.h>
namespace psu {
enum class ActionKind : uint8_t {
  None, OpenGroup, OpenUnit, Previous, Next, Back, Configure, EditVoltage, EditCurrent,
  Adjust, Apply, Cancel, ConfirmNo, ConfirmYes, Tools, Calibrate, TouchDebug,
  Scenarios, ScenarioPrevious, ScenarioNext, ScenarioStart, DebugRestart, DebugReset,
  Startup, AvailabilityPrimary, AvailabilityIndividuals, AvailabilityToggle, AvailabilityMode,
  AvailabilityReview, AvailabilityRemember, AvailabilityConfirm, HardStop
};
struct UiAction { ActionKind kind = ActionKind::None; int16_t value = 0;
  UiAction() = default;
  UiAction(ActionKind k, int16_t v = 0) : kind(k), value(v) {}
};
}
