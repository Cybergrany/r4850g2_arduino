#pragma once
#include <Arduino.h>
#include "../psu/PsuController.h"
namespace psu {
inline const __FlashStringHelper* operationName(Operation op) {
  switch (op) {
    case Operation::All: return F("global-voltage-and-current");
    case Operation::Voltage: return F("global-voltage");
    case Operation::GroupCurrent: return F("group-current");
    case Operation::Offline: return F("PSU-offline-defaults");
    case Operation::Restore: return F("restore-authorized-profile");
  }
  return F("unknown");
}
inline const __FlashStringHelper* stateName(CommandState s) {
  switch (s) {
    case CommandState::Idle: return F("idle"); case CommandState::Queued: return F("queued");
    case CommandState::VerifyingIdentity: return F("verifying identity before write");
    case CommandState::Waiting: return F("waiting"); case CommandState::Success: return F("accepted");
    case CommandState::Incomplete: return F("incomplete; current phase not sent");
    case CommandState::Rejected: return F("PSU rejected"); case CommandState::Timeout: return F("ACK timeout");
    case CommandState::IdentityTimeout: return F("identity query timeout; setting not sent");
    case CommandState::TransportError: return F("CAN transport failed"); case CommandState::Changed: return F("identity/address/readiness changed");
    case CommandState::ObservationTimeout: return F("ACK received; fresh stopped current/readiness not confirmed");
  }
  return F("unknown");
}
inline const __FlashStringHelper* issueName(Issue s) {
  switch (s) {
    case Issue::None: return F("ready"); case Issue::Missing: return F("missing; output state unknown");
    case Issue::Unbound: return F("unbound; discover and bind identity");
    case Issue::Unassigned: return F("no source group; use group set");
    case Issue::IdentityPending: return F("identity needs two fresh INFO replies");
    case Issue::Conflict: return F("identity at multiple live addresses; inspect/isolate units");
    case Issue::NoTelemetry: return F("waiting for fresh telemetry AND current broadcast");
    case Issue::NotReady: return F("PSU reports not ready; check AC/protection/alarms");
    case Issue::VoltageUnsynced: return F("common voltage not synchronized; apply voltage/all");
    case Issue::DeploymentMismatch: return F("EEPROM deployment differs; review config, then defaults");
    case Issue::Busy: return F("operation in progress"); case Issue::Invalid: return F("invalid operation or CAN not initialized");
    case Issue::Capacity: return F("per-PSU current share exceeds capability");
    case Issue::Disabled: return F("disabled by effective enable control; diag control; autoresume on or switch edge to enable");
    case Issue::Reconciling: return F("reconciling zero/fan/voltage/wake; wait and inspect diag control");
    case Issue::ControlFault: return F("control fault latched; diag control/psu; correct cause then reconcile");
    case Issue::NoProfile: return F("no complete authorized voltage profile; stage settings then apply all/voltage");
    case Issue::FanTestRequiresHold: return F("manual AUTO requires autoresume off and confirmed awake zero hold; diag control");
    case Issue::NoAc: return F("WAIT AC; fresh PSU input-power-failure flag");
    case Issue::ApprovalRequired: return F("returned/excluded PSU needs inclusion approval; diag startup");
    case Issue::Settling: return F("startup/group availability settling; diag startup");
    case Issue::Cutoff: return F("AC permit inhibited/faulted or addresses negotiating; diag startup");
    case Issue::NoParticipants: return F("no active units; accepted absences retained");
  }
  return F("unknown");
}
inline const __FlashStringHelper* availabilityName(AvailabilityReason r) {
  switch (r) {
    case AvailabilityReason::NoCan: return F("NO CAN; output unknown");
    case AvailabilityReason::NoAc: return F("WAIT AC");
    case AvailabilityReason::Reconciling: return F("BOOTING/RECONCILING");
    case AvailabilityReason::Ready: return F("READY");
    case AvailabilityReason::Fault: return F("FAULT; cannot waive");
  }
  return F("UNKNOWN");
}
inline const __FlashStringHelper* permitName(PermitState s) {
  return s == PermitState::NotFitted ? F("not fitted") : s == PermitState::Allowed ? F("HIGH commanded") :
      s == PermitState::Inhibited ? F("LOW inhibited") : F("LOW fault latched");
}
inline const __FlashStringHelper* enableSourceName(EnableSource s) {
  return s == EnableSource::Serial ? F("autoresume/serial") : s == EnableSource::Switch ? F("ENABLE_IN") : F("boot preference");
}
inline const __FlashStringHelper* activityName(Activity s) {
  switch (s) {
    case Activity::Reconciling: return F("RECONCILING");
    case Activity::Stopping: return F("STOPPING");
    case Activity::Holding: return F("ZERO CURRENT HOLD / AWAKE");
    case Activity::Starting: return F("STARTING AT ZERO CURRENT");
    case Activity::Running: return F("ENABLED / RECONCILED");
    case Activity::StartBlocked: return F("START BLOCKED");
    case Activity::StopUnconfirmed: return F("STOP UNCONFIRMED");
  }
  return F("unknown");
}
inline const __FlashStringHelper* phaseName(ControlPhase s) {
  switch (s) {
    case ControlPhase::AwaitIdentity: return F("await-identity");
    case ControlPhase::EmergencyStandby: return F("fault-request-standby");
    case ControlPhase::VerifyStandby: return F("verify-fault-standby");
    case ControlPhase::Stopping: return F("verify-stopped-current");
    case ControlPhase::Fan: return F("require-fan-policy");
    case ControlPhase::Voltage: return F("prepare-voltage");
    case ControlPhase::Zero: return F("prepare-zero-current");
    case ControlPhase::Prepared: return F("prepared-at-zero");
    case ControlPhase::Held: return F("held-at-zero-awake");
    case ControlPhase::Wake: return F("release-standby");
    case ControlPhase::Starting: return F("verify-wake-at-zero");
    case ControlPhase::Online: return F("reconciled");
    case ControlPhase::Fault: return F("fault-latched");
  }
  return F("unknown");
}
inline const __FlashStringHelper* fanName(FanMode m) {
  return m == FanMode::Max ? F("MAX") : m == FanMode::Auto ? F("AUTO") : F("UNKNOWN");
}
inline const __FlashStringHelper* fanOverrideName(FanOverride m) {
  return m == FanOverride::Max ? F("MAX") : m == FanOverride::Auto ? F("AUTO") : F("POLICY");
}
inline const __FlashStringHelper* fanReasonName(FanReason r) {
  switch (r) {
    case FanReason::Startup: return F("startup/uncertain-state");
    case FanReason::Temperature: return F("output-temperature");
    case FanReason::RequestedCurrent: return F("requested-current");
    case FanReason::MeasuredCurrent: return F("measured-current");
    case FanReason::InvalidTelemetry: return F("invalid/stale-telemetry");
    case FanReason::Cooling: return F("hysteresis/cooldown");
    case FanReason::Cool: return F("cool/low-current");
    case FanReason::ManualAuto: return F("manual-PSU-auto");
    case FanReason::ManualMax: return F("manual-max");
  }
  return F("unknown");
}
inline const __FlashStringHelper* resultName(Result r) {
  switch (r) {
    case Result::Ok: return F("OK"); case Result::Invalid: return F("ERR value/target/capacity/overlapping membership");
    case Result::Busy: return F("ERR operation in progress"); case Result::TransportError: return F("ERR CAN transport");
    case Result::Blocked: return F("BLOCKED; inspect diag group/psu; no new settings sent");
    case Result::ConfirmRequired: return F("WARNING: confirmation required; confirm yes / confirm no");
    case Result::Changed: return F("BLOCKED: preview expired or configuration/topology changed; preview again");
  }
  return F("ERR");
}
}
