#pragma once
#include "Psu.h"
#include "EnableControl.h"
namespace psu {
enum class ControlPhase : uint8_t { AwaitIdentity, Zero, Stopping, Fan, Voltage, Prepared, Wake, Starting, Held, Online, EmergencyStandby, VerifyStandby, Fault };
enum class FanMode : uint8_t { Unknown, Auto, Max };
enum class FanReason : uint8_t { Startup, Temperature, RequestedCurrent, MeasuredCurrent, InvalidTelemetry, Cooling, Cool };
enum class Activity : uint8_t { Reconciling, Stopping, Holding, Starting, Running, StartBlocked, StopUnconfirmed };
struct ExternalFanStatus {
  float maximumTemperature = 0;
  uint8_t duty = 255, slot = 0;
  FanReason reason = FanReason::Startup;
  bool telemetryComplete = false;
};
struct UnitControl {
  uint32_t since = 0, coolSince = 0;
  ControlPhase phase = ControlPhase::AwaitIdentity;
  FanMode fanAcknowledged = FanMode::Unknown, fanRequested = FanMode::Max;
  FanReason fanReason = FanReason::Startup;
  CommandState last = CommandState::Idle, fault = CommandState::Idle;
  uint8_t lastRegister = 0, faultRegister = 0, attempts = 0;
  bool zeroConfirmed = false, awakeConfirmed = false, standbyConfirmed = false;
  bool cooling = false, fanFailed = false;
};
}
