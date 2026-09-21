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

// Protocol and PSU objects do not depend on Arduino, SPI, or a particular CAN IC.
class CanTransport {
 public:
  virtual bool begin() = 0;
  virtual bool send(const CanFrame& frame) = 0;
  virtual bool receive(CanFrame& frame) = 0;
  virtual uint8_t droppedFrames() const { return 0; }
  virtual ~CanTransport() = default;
};
}
