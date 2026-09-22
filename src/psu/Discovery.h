#pragma once
#include "Psu.h"
namespace psu {
class Discovery {
 public:
  static constexpr uint8_t capacity = PSU_MAX_UNITS + 4;
  void receive(const CanFrame& frame, uint32_t now);
  void tick(uint32_t now, uint32_t staleMs);
  const Psu& device(uint8_t i) const { return devices_[i]; }
  Psu* address(uint8_t address);
  const Psu* identity(const Identity& id, uint32_t now) const;
  uint32_t revision() const { return revision_; }
  uint16_t overflow() const { return overflow_; }
  uint32_t ignored() const { return ignored_; }
 private:
  Psu devices_[capacity] = {};
  uint32_t revision_ = 1, ignored_ = 0;
  uint16_t overflow_ = 0;
};
}
