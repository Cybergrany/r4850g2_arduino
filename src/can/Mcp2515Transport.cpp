#include "../config/BuildOptions.h"
#if !PSU_UI_DEBUG
#include "Mcp2515Transport.h"
#include "Mcp2515Transmit.h"
#include <CAN.h>
#include <util/atomic.h>

namespace psu {
namespace {
// SPI transactions use the same CS and clock as arduino-CAN. Its registered RX
// interrupt is masked by SPI.beginTransaction while a register access is active.
class Registers {
 public:
  uint8_t read(uint8_t address) {
    start(0x03, address);
    const uint8_t value = SPI.transfer(0);
    finish(); return value;
  }
  void write(uint8_t address, uint8_t value) {
    start(0x02, address); SPI.transfer(value); finish();
  }
  void modify(uint8_t address, uint8_t mask, uint8_t value) {
    start(0x05, address); SPI.transfer(mask); SPI.transfer(value); finish();
  }
 private:
  void start(uint8_t command, uint8_t address) {
    SPI.beginTransaction(SPISettings(board::canSpiHz, MSBFIRST, SPI_MODE0));
    digitalWrite(board::canChipSelect, LOW);
    SPI.transfer(command); SPI.transfer(address);
  }
  void finish() { digitalWrite(board::canChipSelect, HIGH); SPI.endTransaction(); }
};
}
Mcp2515Transport* Mcp2515Transport::instance_ = nullptr;
bool Mcp2515Transport::begin() {
#if PSU_BUS_PROFILE
  profileReady_ = false;
#endif
  CAN.setPins(board::canChipSelect, board::canInterrupt);
  CAN.setClockFrequency(board::canCrystalHz);
  CAN.setSPIFrequency(board::canSpiHz);
  if (!CAN.begin(board::canBitrate)) return false;
  Registers registers;
  // Let RXB0 overflow into RXB1 while SPI briefly masks the receive interrupt.
  registers.modify(0x60, 0x04, 0x04); // RXB0CTRL.BUKT
  instance_ = this;
  CAN.onReceive(onReceive);
#if PSU_BUS_PROFILE
  profileReady_ = true;
#endif
  return true;
}
void Mcp2515Transport::onReceive(int length) {
  auto& self = *instance_;
#if PSU_BUS_PROFILE
  ++self.profileReceived_;
#endif
  if (length != 8 || !CAN.packetExtended() || CAN.packetRtr()) {
#if PSU_BUS_PROFILE
    ++self.profileInvalid_;
#endif
    while (CAN.available()) CAN.read();
    return;
  }
  const uint8_t next = (self.head_ + 1) % board::canReceiveSlots;
#if PSU_BUS_PROFILE
  const uint32_t id = CAN.packetId();
  const uint8_t first = CAN.read(), second = CAN.read();
  // Count negotiation even if the application queue drops the frame. This
  // distinguishes unseen negotiation from lost telemetry without doing policy in ISR.
  if ((id & 0xff80ffffUL) == 0x1000107eUL && first == 0x30 && (second == 1 || second == 2))
    ++self.profileNegotiation_;
#endif
  if (next == self.tail_) {
#if PSU_BUS_PROFILE
    ++self.profileDropped_;
#endif
    self.receiveLoss_ = true;
    if (self.dropped_ != UINT8_MAX) ++self.dropped_;
    while (CAN.available()) CAN.read();
    return;
  }
  auto& frame = self.queue_[self.head_];
#if PSU_BUS_PROFILE
  frame.id = id; frame.length = 8; frame.extended = true; frame.rtr = false;
  frame.data[0] = first; frame.data[1] = second;
  for (uint8_t i = 2; i < 8; ++i) frame.data[i] = CAN.read();
  self.arrived_[self.head_] = uint16_t(millis());
#else
  frame.id = CAN.packetId(); frame.length = 8; frame.extended = true; frame.rtr = false;
  for (uint8_t i = 0; i < 8; ++i) frame.data[i] = CAN.read();
#endif
  // Publish only a complete frame. No Serial, display, configuration, or EEPROM in ISR.
  asm volatile("" ::: "memory");
  self.head_ = next;
  const uint8_t used = (next + board::canReceiveSlots - self.tail_) % board::canReceiveSlots;
  if (used > self.highWater_) self.highWater_ = used;
}
bool Mcp2515Transport::receive(CanFrame& frame) {
#if PSU_BUS_PROFILE
  const uint32_t clock = micros(), gap = clock - lastReceiveCallUs_;
  if (receiveCalled_ && gap > maxReceiveGapUs_) maxReceiveGapUs_ = gap;
  // Queue timestamps are modulo milliseconds. If service was absent for over
  // their range, mark the age as saturated rather than reporting a false small age.
  if (receiveCalled_ && gap >= 65535000UL) maxQueueAgeMs_ = UINT16_MAX;
  receiveCalled_ = true; lastReceiveCallUs_ = clock;
#endif
  if (tail_ == head_) return false;
#if PSU_BUS_PROFILE
  const uint16_t age = uint16_t(uint16_t(millis()) - arrived_[tail_]);
  if (age > maxQueueAgeMs_) maxQueueAgeMs_ = age;
#endif
  frame = queue_[tail_]; // ISR cannot overwrite this occupied slot.
  asm volatile("" ::: "memory");
  tail_ = (tail_ + 1) % board::canReceiveSlots;
  return true;
}
bool Mcp2515Transport::send(const CanFrame& frame) {
#if PSU_CAN_OBSERVER
  (void)frame; return false; // Observer cannot dispatch application CAN frames.
#else
  Registers registers;
  return transmitter_.start(registers, frame, millis());
#endif
}
bool Mcp2515Transport::takeReceiveLoss() {
  bool loss = false;
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) { loss = receiveLoss_; receiveLoss_ = false; }
  return loss;
}
void Mcp2515Transport::service(uint32_t now) {
  Registers registers;
#if PSU_CAN_OBSERVER
  (void)now;
#else
  transmitter_.poll(registers, now, board::canTransmitTimeoutMs);
#endif
  // EFLG's overflow bits latch: count observed overflow events, not exact lost
  // frames. Preserve all unrelated error flags and never clear receive flags.
  const uint8_t overflow = registers.read(0x2d) & 0xc0;
  if (overflow) {
    receiveLoss_ = true;
    if (hardwareOverflows_ != UINT16_MAX) ++hardwareOverflows_;
    registers.modify(0x2d, overflow, 0);
  }
}
#if PSU_BUS_PROFILE
void Mcp2515Transport::busReceiveStats(BusReceiveStats& stats) const {
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    stats.received = profileReceived_; stats.dropped = profileDropped_; stats.invalid = profileInvalid_;
    stats.negotiationFrames = profileNegotiation_; stats.highWater = highWater_;
  }
  stats.maxServiceGapUs = maxReceiveGapUs_; stats.maxQueueAgeMs = maxQueueAgeMs_;
  stats.hardwareOverflows = hardwareOverflows_;
  // Main-loop-only readback, once per summary. Each SPI transaction ends before
  // the next, so an RX interrupt can run between the three register reads.
  if (profileReady_) {
    Registers registers;
    stats.errorFlags = registers.read(0x2d);
    stats.txErrorCount = registers.read(0x1c);
    stats.rxErrorCount = registers.read(0x1d);
  } else stats.errorFlags = stats.txErrorCount = stats.rxErrorCount = 0;
}
#endif
}

#endif
