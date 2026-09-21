#pragma once
#include <EEPROM.h>
#include "MemoryManager.h"

namespace psu {
// AVR's standard Arduino EEPROM library. Other storage backends implement ByteStorage.
class ArduinoEeprom : public ByteStorage {
 public:
  uint16_t length() const override { return EEPROM.length(); }
  uint8_t read(uint16_t address) const override { return EEPROM.read(address); }
  void update(uint16_t address, uint8_t value) override { EEPROM.update(address, value); }
};
}
