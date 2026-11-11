#pragma once
#include "config/ChargerConfig.h"
inline void draftVoltage(psu::Configuration& c, uint16_t v) { for (auto& x : c.voltage) x=v; }
inline void authorizedVoltage(psu::Configuration& c, uint16_t v) { for (auto& x : c.operating.voltage) x=v; }
inline void draftGroupCurrent(psu::Configuration& c, uint8_t g, uint16_t total) {
  for (uint8_t i=0;i<c.count;++i) if (c.groups[g].members & (1U<<i)) c.current[i]=psu::allocation(c.groups[g].members,total,i);
}
