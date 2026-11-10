#pragma once
#include "UiFrame.h"
namespace psu {
enum class RecoveryKind : uint8_t { None, Availability, Unit, Bus, Temperatures, Storage, Calibration, Settings, Enable, Help };
enum class RecoveryCommand : uint8_t { None, Retry, Save, Enable, Sensors };
struct RecoveryLink { RecoveryKind kind; uint8_t slot; };
FlashText recoveryLabel(RecoveryKind kind);
}
