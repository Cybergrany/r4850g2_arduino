#pragma once
#include "Psu.h"
#include "EnableControl.h"
namespace psu {
enum class ControlPhase : uint8_t { AwaitIdentity, Zero, Stopping, Fan, Voltage, Prepared, Wake, Starting, Held, Online, EmergencyStandby, VerifyStandby, Fault };
enum class FanMode : uint8_t { Unknown, Auto, Max };
enum class FanOverride : uint8_t { Policy, Auto, Max };
enum class FanReason : uint8_t { Startup, Temperature, RequestedCurrent, MeasuredCurrent, InvalidTelemetry, Cooling, Cool, ManualAuto, ManualMax };
enum class OutputDemand : uint8_t { Unknown, Zero, Starting, Active, Stopping };
enum class Activity : uint8_t { Reconciling, Stopping, Holding, Starting, Running, StartBlocked, StopUnconfirmed };
struct ExternalFanStatus {
  float maximumTemperature = 0;
  uint8_t duty = 255, slot = 0;
  FanReason reason = FanReason::Startup;
  bool telemetryComplete = false;
};
// Separate command acceptance, sustained readiness and measured output. Pending
// means current may already be flowing, not that the output is inhibited.
struct ReadinessStatus {
  uint16_t stableMs = 0, remainingMs = 0;
  uint8_t frames = 0;
  bool pending = false, acknowledged = false;
};
struct UnitControl {
  uint32_t since = 0, coolSince = 0;
  uint32_t outputSince = 0;
  uint32_t readySince = 0, readyLast = 0;
  uint16_t outputWindowMs = 0;
  uint8_t readyFrames = 0;
  OutputDemand outputDemand = OutputDemand::Unknown;
  ControlPhase phase = ControlPhase::AwaitIdentity;
  FanMode fanAcknowledged = FanMode::Unknown, fanRequested = FanMode::Max;
  FanOverride fanOverride = FanOverride::Policy;
  FanReason fanReason = FanReason::Startup;
  CommandState last = CommandState::Idle, fault = CommandState::Idle;
  uint8_t lastRegister = 0, faultRegister = 0, attempts = 0;
  bool zeroConfirmed = false, awakeConfirmed = false, standbyConfirmed = false;
  bool cooling = false, fanFailed = false;
  bool postReady = false; // Second zero/V/fan pass after power-stage readiness.
  bool reconciled = false;
};
}
