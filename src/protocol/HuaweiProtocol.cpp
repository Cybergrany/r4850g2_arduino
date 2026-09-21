// Protocol behaviour based on Craig Peacock's Huawei_R4850G2_CAN (GPL-3.0-or-later).
// Copyright (C) 2021-2024 Craig Peacock. See LICENSE and docs/PARITY.md.
#include "HuaweiProtocol.h"

namespace psu { namespace protocol {
uint8_t address(uint32_t id) { return uint8_t((id >> 16) & 0x7f); }
uint8_t command(uint32_t id) { return uint8_t(id >> 8); }
bool isReply(const CanFrame& f) {
  // Both continuation (..7F) and final (..7E) packets, software addressing.
  return f.extended && !f.rtr && f.length == 8 &&
      (f.id & 0xff8000feUL) == 0x1080007eUL;
}
bool isCurrentBroadcast(const CanFrame& f) {
  return f.extended && !f.rtr && f.length == 8 &&
      (f.id & 0xff80ffffUL) == 0x1000117eUL && f.data[0] == 0 && f.data[1] == 1;
}
uint32_t readBigEndian(const uint8_t* d) {
  // Avoid unaligned reads and AVR's 16-bit int promotion when shifting.
  return (uint32_t(d[0]) << 24) | (uint32_t(d[1]) << 16) |
         (uint32_t(d[2]) << 8) | uint32_t(d[3]);
}
CanFrame request(uint8_t addr, uint8_t cmd) {
  CanFrame f = {};
  f.id = 0x108000feUL | (uint32_t(addr) << 16) | (uint32_t(cmd) << 8);
  f.length = 8;
  f.extended = true; // Data request is a DATA frame, not an RTR frame.
  return f;
}
CanFrame setting(uint8_t addr, uint8_t reg, uint16_t value) {
  CanFrame f = request(addr, setCommand);
  f.data[0] = 1;
  f.data[1] = reg;
  f.data[6] = uint8_t(value >> 8);
  f.data[7] = uint8_t(value);
  return f;
}
uint16_t encodeVoltage(uint16_t cv) { return uint32_t(cv) * fixedPointScale / 100; }
uint16_t encodeCurrent(uint16_t ca, uint16_t rated) {
  // Reference v1.2: the wire value is a fraction of rated current, not A * 20.
  return rated ? uint32_t(ca) * fixedPointScale / rated : 0;
}
int8_t metric(uint8_t reg) {
  if (reg >= 0x70 && reg <= 0x76) return reg - 0x70;
  if (reg == 0x78) return InputVoltage;
  if (reg >= 0x7f && reg <= 0x82) return OutputTemperature + reg - 0x7f;
  return -1;
}
} }
