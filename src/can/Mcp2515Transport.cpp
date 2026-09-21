#include "Mcp2515Transport.h"
#include <CAN.h>

namespace psu {
Mcp2515Transport* Mcp2515Transport::instance_ = nullptr;
bool Mcp2515Transport::begin() {
  CAN.setPins(board::canChipSelect, board::canInterrupt);
  CAN.setClockFrequency(board::canCrystalHz);
  if (!CAN.begin(board::canBitrate)) return false;
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
}
bool Mcp2515Transport::receive(CanFrame& frame) {
  if (tail_ == head_) return false;
  frame = queue_[tail_]; // ISR cannot overwrite this occupied slot.
  asm volatile("" ::: "memory");
  tail_ = (tail_ + 1) % board::canReceiveSlots;
  return true;
}
bool Mcp2515Transport::send(const CanFrame& frame) {
  if (!frame.extended || frame.rtr || frame.length != 8) return false;
  if (!CAN.beginExtendedPacket(frame.id, frame.length, false)) return false;
  if (CAN.write(frame.data, frame.length) != frame.length) return false;
  return CAN.endPacket() == 1;
}
}
