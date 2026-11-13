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
enum class ReceiveWork : uint8_t { Unknown, Control, Storage, Serial, UiInput, UiCompose, UiPaint };
struct ReceiveServiceStatus {
  uint32_t maxGapUs = 0; // Between receive attempts, not per-frame queue age.
  ReceiveWork gapEndedIn = ReceiveWork::Unknown, lastDropIn = ReceiveWork::Unknown;
};
struct ReceiveInterruptStatus {
  uint16_t yields = 0;
  uint8_t flags = 0;
  bool polling = false, pinLow = false;
};

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
  virtual uint8_t receivePending() const { return 0; }
  virtual uint16_t hardwareOverflows() const { return 0; }
  virtual uint16_t receiveInterruptMaxUs() const { return 0; }
  virtual ReceiveInterruptStatus receiveInterruptStatus() const { return {}; }
  virtual ReceiveServiceStatus receiveServiceStatus() const { return {}; }
  void receiveWork(ReceiveWork work) { receiveWork_ = work; }
  // Consume recent loss independently of saturating diagnostic counters.
  virtual bool takeReceiveLoss() { return false; }
#if PSU_BUS_PROFILE
  virtual void busReceiveStats(BusReceiveStats& stats) const { stats = {}; }
#endif
  virtual ~CanTransport() = default;
 protected:
  volatile ReceiveWork receiveWork_ = ReceiveWork::Unknown;
};
}
