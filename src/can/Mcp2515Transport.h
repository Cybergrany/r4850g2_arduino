#pragma once
#include "CanTransport.h"
#include "Mcp2515Transmit.h"
#include "../config/BoardConfig.h"

namespace psu {
// Adapts arduino-CAN's singleton MCP2515. All business logic stays outside this driver.
class Mcp2515Transport : public CanTransport {
 public:
  bool begin() override;
  bool send(const CanFrame& frame) override;
  void service(uint32_t now) override;
  TransmitState transmitState() const override { return transmitter_.state(); }
  bool receive(CanFrame& frame) override;
  uint8_t droppedFrames() const override { return dropped_; }
  uint8_t receiveHighWater() const override { return highWater_; }
  uint16_t hardwareOverflows() const override { return hardwareOverflows_; }
 private:
  static void onReceive(int length);
  static Mcp2515Transport* instance_;
  CanFrame queue_[board::canReceiveSlots];
  volatile uint8_t head_ = 0;
  volatile uint8_t tail_ = 0;
  volatile uint8_t dropped_ = 0;
  volatile uint8_t highWater_ = 0;
  uint16_t hardwareOverflows_ = 0;
  mcp2515::Transmitter transmitter_;
};
}
