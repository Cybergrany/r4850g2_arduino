#pragma once
#include <stdint.h>
#include "BuildOptions.h"
#include "BoardConfig.h"
#include "TouchConfig.h"

// Active HIGH common AC permissive. Hardware must hold LOW during reset/power loss.
// The Mega must be powered upstream. -1 preserves installations without a cutoff.
#ifndef PSU_CHG_ALLOW_PIN
#define PSU_CHG_ALLOW_PIN -1
#endif

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
// Optional external fan driver. -1 disables physical output; policy/diagnostics
// remain available. 25 kHz uses a dedicated Timer4 (D6/7/8) or Timer5 (D44/45/46).
#ifndef PSU_FAN_PWM_PIN
#define PSU_FAN_PWM_PIN -1
#endif
#ifndef PSU_FAN_PWM_INVERTED
// The fan's control input is active HIGH. Set 1 only for an inverting buffer.
#define PSU_FAN_PWM_INVERTED 0
#endif
namespace psu { namespace control {
constexpr int8_t chargeAllowPin = PSU_CHG_ALLOW_PIN;
constexpr uint32_t inhibitMs = 2000, availabilityGraceMs = 15000;
constexpr uint32_t startupReadyMs = 15000, availabilitySettleMs = 3000;
// Qualify actual output-ready broadcasts after the nonzero-current ACK. A
// candidate must span this duration; rereading one frame never advances it.
constexpr uint16_t outputReadyStableMs = 2000, outputReadyMaxGapMs = 1000;
constexpr uint8_t outputReadyFrames = 3;
static_assert(startupReadyMs <= UINT16_MAX && outputReadyStableMs < startupReadyMs &&
    outputReadyFrames >= 2 && outputReadyMaxGapMs > 0, "Invalid output readiness window");
constexpr uint32_t unstableMs = 30000, volatileRefreshMs = 10000;
constexpr int8_t fanPwmPin = PSU_FAN_PWM_PIN;
constexpr bool fanPwmInverted = PSU_FAN_PWM_INVERTED;
constexpr uint16_t fanPwmHz = 25000;
constexpr uint8_t enablePin = PSU_ENABLE_IN_PIN;
constexpr bool enableActiveLow = PSU_ENABLE_IN_ACTIVE_LOW, enablePullup = PSU_ENABLE_IN_PULLUP;
constexpr uint16_t enableDebounceMs = 50;
// Per PSU, using its allocated current, never the group total. Currents in cA.
constexpr float fanOutputTempThreshold = 40.0f, fanOutputTempRelease = 35.0f;
constexpr uint16_t fanOutputCurrentThreshold = 3000, fanOutputCurrentRelease = 2500;
constexpr uint32_t fanReleaseMs = 30000;
constexpr float externalFanOffTemp = 20.0f;
constexpr uint16_t externalFanUpdateMs = 100;
static_assert(externalFanOffTemp < fanOutputTempThreshold, "External fan curve requires a positive temperature span");
constexpr float minimumTemperature = -40.0f, maximumTemperature = 150.0f;
// Require a new output-current sample after a zero/wake/fault-standby ACK. This allows
// sensor noise; it does not establish electrical isolation of a parallel bus.
constexpr float stoppedCurrentAmps = 0.5f;
constexpr uint32_t observationMs = 5000;
constexpr uint8_t commandAttempts = 2; // Rejections latch immediately; no write loop.
#if PSU_CHG_ALLOW_PIN >= 0
static_assert(chargeAllowPin >= 2 && chargeAllowPin < 70 &&
    (chargeAllowPin < 50 || chargeAllowPin > 53) && chargeAllowPin != board::canChipSelect &&
    chargeAllowPin != board::canInterrupt, "CHG_ALLOW conflicts with serial/CAN/SPI");
static_assert(!PSU_ENABLE_ENCODER || (chargeAllowPin != board::encoderClock &&
    chargeAllowPin != board::encoderData && chargeAllowPin != board::encoderButton), "CHG_ALLOW conflicts with wheel");
static_assert(!PSU_ENABLE_DISPLAY || (chargeAllowPin != board::displayChipSelect &&
    chargeAllowPin != board::displayDataCommand && chargeAllowPin != board::displayReset &&
    chargeAllowPin != board::displayBacklight), "CHG_ALLOW conflicts with display");
static_assert(!PSU_ENABLE_TOUCH || (chargeAllowPin != touch::chipSelect && chargeAllowPin != touch::interruptPin),
    "CHG_ALLOW conflicts with touch");
static_assert(!PSU_ENABLE_ENABLE_IN || chargeAllowPin != enablePin, "CHG_ALLOW conflicts with ENABLE_IN");
static_assert(chargeAllowPin != fanPwmPin, "CHG_ALLOW conflicts with fan PWM");
#endif
static_assert(PSU_CHG_ALLOW_PIN >= -1, "Invalid CHG_ALLOW pin");
static_assert(PSU_FAN_PWM_PIN == -1 || (PSU_FAN_PWM_PIN >= 6 && PSU_FAN_PWM_PIN <= 8) ||
    (PSU_FAN_PWM_PIN >= 44 && PSU_FAN_PWM_PIN <= 46), "25 kHz fan requires Mega D6/7/8 or D44/45/46 (or -1)");
#if PSU_FAN_PWM_PIN >= 0
static_assert(fanPwmPin != board::canChipSelect && fanPwmPin != board::canInterrupt,
    "External fan conflicts with CAN");
static_assert(!PSU_ENABLE_ENCODER || (fanPwmPin != board::encoderClock && fanPwmPin != board::encoderData &&
    fanPwmPin != board::encoderButton && fanPwmPin != 11 && fanPwmPin != 12),
    "External fan conflicts with wheel pins or Timer1");
static_assert(!PSU_ENABLE_DISPLAY || (fanPwmPin != board::displayChipSelect && fanPwmPin != board::displayDataCommand &&
    fanPwmPin != board::displayReset && fanPwmPin != board::displayBacklight), "External fan conflicts with display");
static_assert(!PSU_ENABLE_DISPLAY || (fanPwmPin <= 8 ?
    (board::displayBacklight < 6 || board::displayBacklight > 8) :
    (board::displayBacklight < 44 || board::displayBacklight > 46)), "Fan timer conflicts with display backlight PWM");
static_assert(!PSU_ENABLE_TOUCH || (fanPwmPin != touch::chipSelect && fanPwmPin != touch::interruptPin),
    "External fan conflicts with touch");
static_assert(!PSU_ENABLE_ENABLE_IN || fanPwmPin != enablePin, "External fan conflicts with ENABLE_IN");
#endif
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
