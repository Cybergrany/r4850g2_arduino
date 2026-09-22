#pragma once
#include "../config/ChargerConfig.h"
#include "../protocol/HuaweiProtocol.h"
namespace psu {
enum class CommandState : uint8_t { Idle, Queued, VerifyingIdentity, Waiting, Success, Incomplete, Rejected, Timeout, IdentityTimeout, TransportError, Changed };
struct CommandStatus { CommandState state; uint8_t reg; uint32_t rawValue; };
struct Telemetry {
  float values[protocol::MetricCount];
  uint32_t updated[protocol::MetricCount]; // Freshness is per field, not per reply batch.
  uint16_t validMask, freshMask;
  float ampHours;
  uint32_t lastData, lastBroadcast, alarmBits;
  bool dataSeen, broadcastSeen, ready, alarmSeen;
};
// Discovered physical device; address is runtime routing information only.
struct Psu {
  Identity identity;
  uint8_t address, identitySamples;
  uint32_t lastSeen, lastIdentity, lastInfoRequest, lastDataRequest, epoch;
  bool occupied, live, conflict;
  Telemetry telemetry;
  void receive(const CanFrame& frame, uint32_t now);
  bool fresh(uint32_t now, uint32_t timeout) const;
  bool verified(uint32_t now) const;
};
}
