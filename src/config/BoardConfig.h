#pragma once
#include <stdint.h>

namespace psu { namespace board {
// Existing Nano/Mega wiring and MCP2515 crystal. Change these here, not in logic.
constexpr uint8_t canChipSelect = 10;
constexpr uint8_t canInterrupt = 2;
constexpr uint32_t canCrystalHz = 8000000UL;
constexpr uint32_t canBitrate = 125000UL;
constexpr uint32_t canSpiHz = 10000000UL;
constexpr uint16_t canTransmitTimeoutMs = 20;
constexpr uint8_t canReceiveSlots = 16;
constexpr uint8_t encoderClock = 3;
constexpr uint8_t encoderData = 4;
constexpr uint8_t encoderButton = 5;
constexpr uint8_t encoderStepsPerNotch = 4;
constexpr uint8_t displayAddress = 0x3c;
constexpr uint32_t displayBusHz = 400000UL;
constexpr uint32_t displayWireTimeoutUs = 25000UL;
constexpr uint32_t serialBaud = 115200UL;
constexpr uint16_t menuTimeoutMs = 10000;
constexpr uint16_t eepromBudget = 512;
} }
