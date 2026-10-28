#pragma once
#include "Psu.h"
#include "EnableControl.h"
namespace psu {
enum class ControlPhase : uint8_t { AwaitIdentity, Standby, Stopping, Fan, Voltage, Zero, Prepared, Wake, Starting, Online, Fault };
enum class FanMode : uint8_t { Unknown, Auto, Max };
enum class FanReason : uint8_t { Startup, Temperature, RequestedCurrent, MeasuredCurrent, InvalidTelemetry, Cooling, Cool };
enum class Activity : uint8_t { Reconciling, Stopping, Standby, Starting, Running, StartBlocked, StopUnconfirmed };
struct UnitControl {
  uint32_t since = 0, coolSince = 0;
  ControlPhase phase = ControlPhase::AwaitIdentity;
  FanMode fanAcknowledged = FanMode::Unknown, fanRequested = FanMode::Max;
  FanReason fanReason = FanReason::Startup;
  CommandState last = CommandState::Idle, fault = CommandState::Idle;
  uint8_t lastRegister = 0, faultRegister = 0, attempts = 0;
  bool standbyConfirmed = false, cooling = false, fanFailed = false;
};
}
