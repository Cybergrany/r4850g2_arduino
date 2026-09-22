#pragma once
#include <stdint.h>

namespace psu {
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
  virtual ~CanTransport() = default;
};
}
