#pragma once
#include <stdint.h>
namespace psu { namespace touch {
constexpr uint8_t chipSelect = 25, interruptPin = 18;
constexpr uint32_t spiHz = 2000000UL;
constexpr uint16_t sampleMs = 20, pressureMinimum = 300;
constexpr uint16_t repeatDelayMs = 500, repeatMs = 120, debounceMs = 40;
constexpr uint16_t sampleSpread = 100, verificationPixels = 12;
constexpr uint32_t calibrationTimeoutMs = 60000;
constexpr uint8_t stableSamples = 8;
// Change profile when replacing the panel/wiring. Rotation is checked separately.
constexpr uint16_t profile = 1;
constexpr uint8_t recordBytes = 40;
constexpr uint16_t recordAddress[2] = {216, 472};
} }
