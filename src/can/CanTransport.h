#pragma once
#include <stdint.h>
#include "../config/BuildOptions.h"

namespace psu {
#if PSU_BUS_PROFILE
struct BusReceiveStats {
  uint32_t received = 0, dropped = 0, invalid = 0, maxServiceGapUs = 0;
  uint16_t maxQueueAgeMs = 0, hardwareOverflows = 0, negotiationFrames = 0;
  uint8_t highWater = 0;
  uint8_t errorFlags = 0, txErrorCount = 0, rxErrorCount = 0;
};
#endif
struct CanFrame {
  uint32_t id;
  uint8_t data[8];
  uint8_t length;
  bool extended;
  bool rtr;
};
enum class TransmitState : uint8_t { Idle, Pending, Sent, Failed };

// Protocol and PSU objects do not depend on Arduino, SPI, or a particular CAN IC.
class CanTransport {
 public:
  virtual bool begin() = 0;
  // Accept one frame for transmission; success is not a PSU acknowledgement.
  // Async adapters expose completion via transmitState(), advanced by service().
  virtual bool send(const CanFrame& frame) = 0;
  virtual void service(uint32_t now) { (void)now; }
  virtual TransmitState transmitState() const { return TransmitState::Sent; }
  virtual bool receive(CanFrame& frame) = 0;
  virtual uint8_t droppedFrames() const { return 0; }
  virtual uint8_t receiveHighWater() const { return 0; }
  virtual uint16_t hardwareOverflows() const { return 0; }
  // Consume recent loss independently of saturating diagnostic counters.
  virtual bool takeReceiveLoss() { return false; }
#if PSU_BUS_PROFILE
  virtual void busReceiveStats(BusReceiveStats& stats) const { stats = {}; }
#endif
  virtual ~CanTransport() = default;
};
}
