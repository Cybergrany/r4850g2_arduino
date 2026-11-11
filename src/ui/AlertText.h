#pragma once
#include "UiText.h"
namespace psu {
inline const __FlashStringHelper* alertCause(AlertCode c) {
  switch (c) {
    case AlertCode::None: return F("No active condition");
    case AlertCode::Missing: return F("Members unavailable");
    case AlertCode::Excluded: return F("Reduced capacity accepted");
    case AlertCode::Returned: return F("PSU returned; excluded");
    case AlertCode::Unstable: return F("Source availability unstable");
    case AlertCode::Commissioning: return F("Configuration needs review");
    case AlertCode::Display: return F("Display unavailable");
    case AlertCode::Touch: return F("Touch calibration unavailable");
    case AlertCode::OlderSave: return F("Saved an older snapshot");
    case AlertCode::ReceiveLoss: return F("Recent CAN receive loss");
    case AlertCode::DiscoveryFull: return F("Discovery capacity exhausted");
    case AlertCode::ActiveLost: return F("Active PSU lost CAN/AC/readiness");
    case AlertCode::Identity: return F("PSU identity conflict/change");
    case AlertCode::Command: return F("Setting/fan command failed");
    case AlertCode::Control: return F("Reconciliation/output fault");
    case AlertCode::StopUnconfirmed: return F("Stopped current unconfirmed");
    case AlertCode::Cutoff: return F("AC cutoff latched LOW");
    case AlertCode::CanInit: return F("CAN initialization failed");
    case AlertCode::Deployment: return F("Wrong deployment configuration");
    case AlertCode::Storage: return F("Configuration persistence failed");
    case AlertCode::TemperatureWarning: return F("Temperature warning");
    case AlertCode::TemperatureError: return F("Temperature error");
    case AlertCode::ThermalStop: return F("Thermal shutdown latched");
    case AlertCode::TemperatureSensor: return F("Temperature readings lost");
  }
  return F("Unknown condition");
}
inline const __FlashStringHelper* alertAction(AlertCode c) {
  switch (c) {
    case AlertCode::TemperatureWarning: case AlertCode::TemperatureError:
      return F("All active limits reduced; maximum cooling.");
    case AlertCode::ThermalStop: return F("Charger OFF; cooling continues.");
    case AlertCode::TemperatureSensor: return F("Affected PSU stopped. Other shares unchanged.");
    case AlertCode::ActiveLost: case AlertCode::Identity: return F("Output unknown. Identity/startup gates protect writes.");
    case AlertCode::Command: case AlertCode::Control: case AlertCode::StopUnconfirmed:
      return F("Restore blocked. Bounded stop/fault fallback.");
    case AlertCode::Cutoff: return F("Common AC permit LOW; output must be checked.");
    case AlertCode::Missing: case AlertCode::Returned: case AlertCode::Unstable: return F("REVIEW REQUIRED.");
    case AlertCode::Excluded: return F("Excluded shares are not redistributed.");
    case AlertCode::Storage: return F("Saved state unconfirmed. Review before power loss.");
    default: return F("Notification only; existing control gates apply.");
  }
}
inline const __FlashStringHelper* alertRecovery(AlertCode c) {
  switch (c) {
    case AlertCode::TemperatureWarning: case AlertCode::TemperatureError:
      return F("Inspect cooling. All monitored temperatures must cool.");
    case AlertCode::ThermalStop: return F("Cool, verify persistence, then explicit Resume/ON.");
    case AlertCode::TemperatureSensor: return F("Restore both readings, then Retry sensors.");
    case AlertCode::Missing: case AlertCode::Returned: case AlertCode::Unstable:
      return F("Open config before proceeding with power supply.");
    case AlertCode::ActiveLost: return F("Review cause and participation in Config.");
    case AlertCode::Excluded: return F("Excluded until explicitly included in Config.");
    case AlertCode::Identity: return F("Inspect bindings and bus. Isolate conflicting devices.");
    case AlertCode::Command: case AlertCode::Control: case AlertCode::StopUnconfirmed: case AlertCode::Cutoff: case AlertCode::CanInit:
      return F("Inspect diag control/bus, fix cause, then reconcile.");
    case AlertCode::Display: return F("Check display wiring; use serial alerts meanwhile.");
    case AlertCode::Touch: return F("Tools: calibrate touch, or use serial/wheel.");
    case AlertCode::OlderSave: case AlertCode::Storage: return F("Review RAM config and storage. Save again, verify.");
    case AlertCode::ReceiveLoss: case AlertCode::DiscoveryFull: return F("Check diag bus, wiring, load and device count.");
    default: return F("Review config, bindings, groups and authorized profile.");
  }
}
}
