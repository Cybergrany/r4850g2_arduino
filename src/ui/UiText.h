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
