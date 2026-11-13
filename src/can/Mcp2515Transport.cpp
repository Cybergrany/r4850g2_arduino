#include "../config/BuildOptions.h"
#if !PSU_UI_DEBUG
#include "Mcp2515Transport.h"
#include "Mcp2515Transmit.h"
#include "../diagnostics/RuntimeTiming.h"
#include <CAN.h>
#include <util/atomic.h>

namespace psu {
namespace {
// SPI transactions use the same CS and clock as arduino-CAN. Our registered RX
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
  // The library's handler drains until empty inside a LOW-level ISR. A burst
  // can therefore mask Timer0 for multiple overflows. Own the handler instead:
  // at most the two buffers in one snapshot, each read in one SPI transaction.
  SPI.usingInterrupt(digitalPinToInterrupt(board::canInterrupt));
  pinMode(board::canInterrupt, INPUT);
  attachInterrupt(digitalPinToInterrupt(board::canInterrupt), onInterrupt, LOW);
#if PSU_BUS_PROFILE
  profileReady_ = true;
#endif
  return true;
}
void Mcp2515Transport::onInterrupt() {
  auto& self = *instance_;
  const uint32_t started = micros();
  const uint8_t flags = self.receiveBuffers();
  // Bounding one LOW-level ISR does not bound its immediate retriggering. An
  // asserted INT with no RX flags can otherwise starve Timer0/UART indefinitely.
  // Also yield if both buffers refilled: main will drain a bounded snapshot and
  // re-arm only after INT releases. Finish all SPI transactions before detaching.
  if (digitalRead(board::canInterrupt) == LOW) {
    detachInterrupt(digitalPinToInterrupt(board::canInterrupt));
    self.receivePolling_ = true;
    self.interruptFlags_ = flags;
    if (self.interruptYields_ != UINT16_MAX) ++self.interruptYields_;
  }
  const uint32_t us = timing::elapsed(started, micros());
  if (us != timing::invalid && us > self.interruptMaxUs_)
    self.interruptMaxUs_ = us > UINT16_MAX ? UINT16_MAX : us;
}
uint8_t Mcp2515Transport::receiveBuffers() {
  Registers registers;
  const uint8_t flags = registers.read(0x2c); // CANINTF.RX0IF/RX1IF
  if (flags & 1) receiveBuffer(0);
  if (flags & 2) receiveBuffer(1);
  return flags;
}
void Mcp2515Transport::pollReceiveInterrupt() {
  // The CAN interrupt is detached, so this is the only producer until re-arm.
  // Timer0/UART/wheel interrupts remain enabled throughout foreground polling.
  receiveBuffers();
  if (digitalRead(board::canInterrupt) != LOW) {
    receivePolling_ = false;
    // LOW mode catches arrivals between the pin check and re-arm as well.
    attachInterrupt(digitalPinToInterrupt(board::canInterrupt), onInterrupt, LOW);
  }
}
void Mcp2515Transport::receiveBuffer(uint8_t buffer) {
  // The unused head slot is safe scratch even when the ring is full. Publish
  // only after decoding/validation; main never reads an unpublished slot.
  auto& frame = queue_[head_];
  SPI.beginTransaction(SPISettings(board::canSpiHz, MSBFIRST, SPI_MODE0));
  digitalWrite(board::canChipSelect, LOW);
  SPI.transfer(buffer ? 0x94 : 0x90); // READ RX BUFFER, starting at SIDH
  const uint8_t sidh = SPI.transfer(0), sidl = SPI.transfer(0);
  frame.id = uint32_t(sidh) << 21 | uint32_t(sidl & 0xe0) << 13 | uint32_t(sidl & 3) << 16;
  frame.id |= uint32_t(SPI.transfer(0)) << 8;
  frame.id |= SPI.transfer(0);
  const uint8_t dlc = SPI.transfer(0);
  frame.length = dlc & 0x0f; frame.extended = (sidl & 8) != 0; frame.rtr = (dlc & 0x40) != 0;
  for (uint8_t i = 0; i < 8; ++i) frame.data[i] = SPI.transfer(0);
  // MCP2515 DS20001801K section 12.4: CS rising clears only this RXnIF.
  // A later frame cannot be erased by a separate, delayed flag-clear command.
  digitalWrite(board::canChipSelect, HIGH);
  SPI.endTransaction();
#if PSU_BUS_PROFILE
  ++profileReceived_;
#endif
  if (frame.length != 8 || !frame.extended || frame.rtr) {
#if PSU_BUS_PROFILE
    ++profileInvalid_;
#endif
    return;
  }
  const uint8_t next = (head_ + 1) % board::canReceiveSlots;
#if PSU_BUS_PROFILE
  // Count negotiation even if the application queue drops the frame. This
  // distinguishes unseen negotiation from lost telemetry without doing policy in ISR.
  if ((frame.id & 0xff80ffffUL) == 0x1000107eUL && frame.data[0] == 0x30 && (frame.data[1] == 1 || frame.data[1] == 2))
    ++profileNegotiation_;
#endif
  if (next == tail_) {
#if PSU_BUS_PROFILE
    ++profileDropped_;
#endif
    receiveLoss_ = true;
    if (dropped_ != UINT8_MAX) ++dropped_;
    return;
  }
#if PSU_BUS_PROFILE
  arrived_[head_] = uint16_t(millis());
#endif
  // Publish only a complete frame. No Serial, display, configuration, or EEPROM in ISR.
  asm volatile("" ::: "memory");
  head_ = next;
  const uint8_t used = (next + board::canReceiveSlots - tail_) % board::canReceiveSlots;
  if (used > highWater_) highWater_ = used;
}
uint16_t Mcp2515Transport::receiveInterruptMaxUs() const {
  uint16_t result;
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) { result = interruptMaxUs_; }
  return result;
}
ReceiveInterruptStatus Mcp2515Transport::receiveInterruptStatus() const {
  ReceiveInterruptStatus result;
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    result.yields = interruptYields_; result.flags = interruptFlags_;
    result.polling = receivePolling_;
  }
  result.pinLow = digitalRead(board::canInterrupt) == LOW;
  return result;
}
bool Mcp2515Transport::receive(CanFrame& frame) {
#if PSU_BUS_PROFILE
  const uint32_t clock = micros();
  const uint32_t gap = receiveCalled_ ? timing::elapsed(lastReceiveCallUs_, clock) : 0;
  if (gap != timing::invalid && gap > maxReceiveGapUs_) maxReceiveGapUs_ = gap;
  // Queue timestamps are modulo milliseconds. If service was absent for over
  // their range, mark the age as saturated rather than reporting a false small age.
  if (gap != timing::invalid && gap >= 65535000UL) maxQueueAgeMs_ = UINT16_MAX;
  receiveCalled_ = true; lastReceiveCallUs_ = clock;
#endif
  // Free two ring slots before reading another two-buffer snapshot. Never drop
  // queued frames merely to make progress after an interrupt has yielded.
  if (receivePolling_ && receivePending() < board::canReceiveSlots - 2) pollReceiveInterrupt();
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
