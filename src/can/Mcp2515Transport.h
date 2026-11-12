#include "../config/BuildOptions.h"
#if !PSU_UI_DEBUG
#pragma once
#include "CanTransport.h"
#include "Mcp2515Transmit.h"
#include "../config/BoardConfig.h"

namespace psu {
// arduino-CAN initializes the MCP2515; bounded RX/TX belong to this adapter.
class Mcp2515Transport : public CanTransport {
 public:
  bool begin() override;
  bool send(const CanFrame& frame) override;
  void service(uint32_t now) override;
  TransmitState transmitState() const override {
#if PSU_CAN_OBSERVER
    return TransmitState::Idle;
#else
    return transmitter_.state();
#endif
  }
  bool receive(CanFrame& frame) override;
  uint8_t droppedFrames() const override { return dropped_; }
  uint8_t receiveHighWater() const override { return highWater_; }
  uint8_t receivePending() const override { return (head_ + board::canReceiveSlots - tail_) % board::canReceiveSlots; }
  uint16_t hardwareOverflows() const override { return hardwareOverflows_; }
  uint16_t receiveInterruptMaxUs() const override;
  bool takeReceiveLoss() override;
#if PSU_BUS_PROFILE
  void busReceiveStats(BusReceiveStats& stats) const override;
#endif
 private:
  static void onInterrupt();
  void receiveBuffer(uint8_t buffer);
  static Mcp2515Transport* instance_;
  CanFrame queue_[board::canReceiveSlots];
  volatile uint8_t head_ = 0;
  volatile uint8_t tail_ = 0;
  volatile uint8_t dropped_ = 0;
  volatile uint8_t highWater_ = 0;
  volatile bool receiveLoss_ = false;
  volatile uint16_t interruptMaxUs_ = 0;
  uint16_t hardwareOverflows_ = 0;
#if !PSU_CAN_OBSERVER
  mcp2515::Transmitter transmitter_;
#endif
#if PSU_BUS_PROFILE
  uint16_t arrived_[board::canReceiveSlots] = {};
  volatile uint32_t profileReceived_ = 0, profileDropped_ = 0, profileInvalid_ = 0;
  volatile uint16_t profileNegotiation_ = 0;
  uint32_t lastReceiveCallUs_ = 0, maxReceiveGapUs_ = 0;
  uint16_t maxQueueAgeMs_ = 0;
  bool receiveCalled_ = false;
  bool profileReady_ = false;
#endif
};
}

#endif
