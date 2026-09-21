#pragma once
#include <Arduino.h>
#include "../psu/PsuController.h"

namespace psu {
inline const __FlashStringHelper* stateName(CommandState s) {
  switch (s) {
    case CommandState::Idle: return F("idle");
    case CommandState::Queued: return F("queued");
    case CommandState::Waiting: return F("waiting");
    case CommandState::Success: return F("accepted");
    case CommandState::Rejected: return F("rejected");
    case CommandState::Timeout: return F("timeout");
    case CommandState::TransportError: return F("CAN error");
  }
  return F("unknown");
}
inline const __FlashStringHelper* resultName(Result r) {
  switch (r) {
    case Result::Ok: return F("OK");
    case Result::Invalid: return F("ERR invalid value, target, or duplicate address");
    case Result::Busy: return F("ERR apply in progress");
    case Result::Disabled: return F("ERR no enabled PSU in selection");
    case Result::TransportError: return F("ERR CAN transport");
  }
  return F("ERR");
}
}
