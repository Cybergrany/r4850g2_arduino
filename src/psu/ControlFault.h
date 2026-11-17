#pragma once
#include "PsuControl.h"
#include "Availability.h"
namespace psu {
enum class FaultCause : uint8_t {
  Command, ZeroHoldCurrent, ZeroObservation, WakeObservation, StandbyObservation,
  OutputReadiness, ReadinessRecovery, ProtectiveApply
};
// One immutable first-fault record per boot, not per PSU. Never read by policy.
// Ages are captured at the fault, not when the operator retrieves this record.
struct ControlFault {
  uint32_t ms = 0, phaseMs = 0, currentAge = 0, voltageAge = 0;
  uint32_t broadcastAge = 0, statusAge = 0, status = 0;
  float amps = 0, volts = 0;
  Identity identity = {};
  uint16_t ackRaw = 0, ratedCurrent = 0, commandRaw = 0;
  uint16_t authorizedCurrent = UINT16_MAX; // No authorized allocation; valid targets are <= 6000.
  uint8_t slot = 0, address = 0, reg = 0, commandReg = 0;
  FaultCause cause = FaultCause::Command;
  ControlPhase phase = ControlPhase::AwaitIdentity;
  CommandState state = CommandState::Idle;
  OutputDemand demand = OutputDemand::Unknown;
  EnableSource source = EnableSource::Boot;
  PermitState permit = PermitState::NotFitted;
  Issue route = Issue::None;
  uint8_t metrics = 0, flags = 0;
  enum Metric : uint8_t { Current = 1, CurrentFresh = 2, Voltage = 4, VoltageFresh = 8,
    Broadcast = 16, Ready = 32, Ack = 64, Status = 128 };
  enum Flag : uint8_t { Zero = 1, Standby = 2, Awake = 4, Enabled = 8, Waiting = 16,
    BroadcastFresh = 32, StatusFresh = 64, Command = 128 };
};
#if defined(__AVR__)
static_assert(sizeof(ControlFault) <= 64, "First-fault record must stay compact");
#endif
}
