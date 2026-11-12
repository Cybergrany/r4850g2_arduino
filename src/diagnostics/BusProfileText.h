#pragma once
#include "BusProfile.h"
#if PSU_BUS_PROFILE
#include <Arduino.h>
namespace psu {
void printBusEvent(Print& out, const BusEventRecord& event);
void printBusSummary(Print& out, const BusProfile& profile, const BusReceiveStats& rx, uint32_t now);
}
#endif
