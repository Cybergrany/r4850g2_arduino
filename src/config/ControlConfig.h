#pragma once
#include <stdint.h>
#include "BuildOptions.h"
#include "BoardConfig.h"
#include "TouchConfig.h"

// Optional latching switch. LOW = enabled with the default pull-up wiring.
#ifndef PSU_ENABLE_IN_PIN
#define PSU_ENABLE_IN_PIN 26
#endif
#ifndef PSU_ENABLE_IN_ACTIVE_LOW
#define PSU_ENABLE_IN_ACTIVE_LOW 1
#endif
#ifndef PSU_ENABLE_IN_PULLUP
#define PSU_ENABLE_IN_PULLUP 1
#endif
namespace psu { namespace control {
constexpr uint8_t enablePin = PSU_ENABLE_IN_PIN;
constexpr bool enableActiveLow = PSU_ENABLE_IN_ACTIVE_LOW, enablePullup = PSU_ENABLE_IN_PULLUP;
constexpr uint16_t enableDebounceMs = 50;
// Per PSU, using its allocated current, never the group total. Currents in cA.
constexpr float fanOutputTempThreshold = 40.0f, fanOutputTempRelease = 35.0f;
constexpr uint16_t fanOutputCurrentThreshold = 3000, fanOutputCurrentRelease = 2500;
constexpr uint32_t fanReleaseMs = 30000;
constexpr float minimumTemperature = -40.0f, maximumTemperature = 150.0f;
// Require a new output-current sample after a standby/wake ACK. This allows
// sensor noise; it does not establish electrical isolation of a parallel bus.
constexpr float stoppedCurrentAmps = 0.5f;
constexpr uint32_t observationMs = 5000;
constexpr uint8_t commandAttempts = 2; // Rejections latch immediately; no write loop.
#if PSU_ENABLE_ENABLE_IN
static_assert(PSU_ENABLE_IN_PIN >= 0 && PSU_ENABLE_IN_PIN < 70, "Invalid Mega ENABLE_IN pin");
static_assert(enablePin < 70 && enablePin != 0 && enablePin != 1 &&
    (enablePin < 50 || enablePin > 53) && enablePin != board::canChipSelect &&
    enablePin != board::canInterrupt, "ENABLE_IN conflicts with serial/CAN/SPI");
static_assert(!PSU_ENABLE_ENCODER || (enablePin != board::encoderClock &&
    enablePin != board::encoderData && enablePin != board::encoderButton), "ENABLE_IN conflicts with wheel");
static_assert(!PSU_ENABLE_DISPLAY || (enablePin != board::displayChipSelect &&
    enablePin != board::displayDataCommand && enablePin != board::displayReset &&
    enablePin != board::displayBacklight), "ENABLE_IN conflicts with display");
static_assert(!PSU_ENABLE_TOUCH || (enablePin != touch::chipSelect && enablePin != touch::interruptPin),
    "ENABLE_IN conflicts with touch");
#endif
} }
