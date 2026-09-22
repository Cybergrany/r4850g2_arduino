#include "Mcp2515Transport.h"
#include "Mcp2515Transmit.h"
#include <CAN.h>

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
  CAN.setPins(board::canChipSelect, board::canInterrupt);
  CAN.setClockFrequency(board::canCrystalHz);
  CAN.setSPIFrequency(board::canSpiHz);
  if (!CAN.begin(board::canBitrate)) return false;
  Registers registers;
  // Let RXB0 overflow into RXB1 while SPI briefly masks the receive interrupt.
  registers.modify(0x60, 0x04, 0x04); // RXB0CTRL.BUKT
  instance_ = this;
  CAN.onReceive(onReceive);
  return true;
}
void Mcp2515Transport::onReceive(int length) {
  auto& self = *instance_;
  if (length != 8 || !CAN.packetExtended() || CAN.packetRtr()) {
    while (CAN.available()) CAN.read();
    return;
  }
  const uint8_t next = (self.head_ + 1) % board::canReceiveSlots;
  if (next == self.tail_) {
    if (self.dropped_ != UINT8_MAX) ++self.dropped_;
    while (CAN.available()) CAN.read();
    return;
  }
  auto& frame = self.queue_[self.head_];
  frame.id = CAN.packetId(); frame.length = 8; frame.extended = true; frame.rtr = false;
  for (uint8_t i = 0; i < 8; ++i) frame.data[i] = CAN.read();
  // Publish only a complete frame. No Serial, display, configuration, or EEPROM in ISR.
  asm volatile("" ::: "memory");
  self.head_ = next;
  const uint8_t used = (next + board::canReceiveSlots - self.tail_) % board::canReceiveSlots;
  if (used > self.highWater_) self.highWater_ = used;
}
bool Mcp2515Transport::receive(CanFrame& frame) {
  if (tail_ == head_) return false;
  frame = queue_[tail_]; // ISR cannot overwrite this occupied slot.
  asm volatile("" ::: "memory");
  tail_ = (tail_ + 1) % board::canReceiveSlots;
  return true;
}
bool Mcp2515Transport::send(const CanFrame& frame) {
  Registers registers;
  return transmitter_.start(registers, frame, millis());
}
void Mcp2515Transport::service(uint32_t now) {
  Registers registers;
  transmitter_.poll(registers, now, board::canTransmitTimeoutMs);
  // EFLG's overflow bits latch: count observed overflow events, not exact lost
  // frames. Preserve all unrelated error flags and never clear receive flags.
  const uint8_t overflow = registers.read(0x2d) & 0xc0;
  if (overflow) {
    if (hardwareOverflows_ != UINT16_MAX) ++hardwareOverflows_;
    registers.modify(0x2d, overflow, 0);
  }
}
}
