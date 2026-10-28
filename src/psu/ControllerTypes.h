#pragma once
#include <stdint.h>
namespace psu {
enum class Result : uint8_t { Ok, Invalid, Busy, TransportError, Blocked, ConfirmRequired, Changed };
enum class Issue : uint8_t {
  None, Missing, Unbound, Unassigned, IdentityPending, Conflict, NoTelemetry,
  NotReady, VoltageUnsynced, DeploymentMismatch, Busy, Invalid, Capacity,
  Disabled, Reconciling, ControlFault, NoProfile, FanTestRequiresHold,
  NoAc, ApprovalRequired, Settling, Cutoff, NoParticipants
};
}
