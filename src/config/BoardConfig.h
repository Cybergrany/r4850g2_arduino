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
// ILI9341 320x240 landscape, SPI shared with CAN; CS must be separate.
constexpr uint8_t displayChipSelect = 22, displayDataCommand = 23;
constexpr int8_t displayReset = 24;
constexpr int8_t displayBacklight = -1; // Optional PWM logic input, e.g. pin 6; not a bare LED load.
constexpr uint32_t displaySpiHz = 8000000UL;
constexpr uint8_t displayRotation = 1; // 1 or 3: landscape.
constexpr bool displayVerifyId = true; // Connect TFT SDO/MISO; opt out only for a verified write-only module.
constexpr uint32_t serialBaud = 115200UL;
constexpr uint16_t eepromBudget = 512;
} }
