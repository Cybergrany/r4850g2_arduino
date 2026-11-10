#pragma once
#include "Psu.h"
namespace psu {
class Discovery {
 public:
  // Retain the existing 12 observations at full capacity: nine managed PSUs
  // plus three spare routes for discovery/address changes.
  static constexpr uint8_t capacity = PSU_MAX_UNITS < 8 ? PSU_MAX_UNITS + 4 : 12;
  void receive(const CanFrame& frame, uint32_t now);
  void tick(uint32_t now, uint32_t staleMs);
  const Psu& device(uint8_t i) const { return devices_[i]; }
  Psu* address(uint8_t address);
  const Psu* identity(const Identity& id, uint32_t now) const;
  uint32_t revision() const { return revision_; }
  uint16_t overflow() const { return overflow_; }
  bool takeOverflow() { const bool event = overflowEvent_; overflowEvent_ = false; return event; }
  uint32_t ignored() const { return ignored_; }
  bool negotiating() const { return negotiating_; }
  void invalidate();
 private:
  Psu devices_[capacity] = {};
  uint32_t revision_ = 1, ignored_ = 0;
  uint16_t overflow_ = 0;
  uint32_t lastNegotiation_ = 0;
  bool negotiating_ = false;
  bool overflowEvent_ = false;
};
}
