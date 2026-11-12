#pragma once
#include "BuildOptions.h"
#include <stdint.h>
namespace psu { namespace busprofile {
// Instrumentation is compiled out unless explicitly requested. No heap/frame archive.
constexpr uint8_t eventSlots = PSU_CAN_OBSERVER ? 24 : 4;
constexpr uint32_t summaryMs = 1000;
constexpr uint16_t outputReserve = 256;
constexpr uint8_t observerAddresses = 12;
constexpr uint32_t confirmationMs = 15000, inputExpiryMs = 30000;
} }
