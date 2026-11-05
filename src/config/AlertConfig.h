#pragma once
#include "ControlConfig.h"
#ifndef PSU_WARNING_PIN
#define PSU_WARNING_PIN -1
#endif
#ifndef PSU_ERROR_PIN
#define PSU_ERROR_PIN -1
#endif
#ifndef PSU_TEMP_WARNING_C
#define PSU_TEMP_WARNING_C 65
#endif
#ifndef PSU_TEMP_ERROR_C
#define PSU_TEMP_ERROR_C 70
#endif
#ifndef PSU_TEMP_STOP_C
#define PSU_TEMP_STOP_C 75
#endif
#ifndef PSU_TEMP_CLEAR_C
#define PSU_TEMP_CLEAR_C 60
#endif
#ifndef PSU_TEMP_WARNING_REDUCTION
#define PSU_TEMP_WARNING_REDUCTION 15
#endif
#ifndef PSU_TEMP_ERROR_REDUCTION
#define PSU_TEMP_ERROR_REDUCTION 70
#endif
#ifndef PSU_TEMP_ERROR_MS
#define PSU_TEMP_ERROR_MS 900000UL
#endif
namespace psu { namespace alerts {
constexpr int8_t warningPin = PSU_WARNING_PIN, errorPin = PSU_ERROR_PIN;
constexpr float warningC = PSU_TEMP_WARNING_C, errorC = PSU_TEMP_ERROR_C;
constexpr float stopC = PSU_TEMP_STOP_C, clearC = PSU_TEMP_CLEAR_C;
constexpr uint8_t warningReduction = PSU_TEMP_WARNING_REDUCTION, errorReduction = PSU_TEMP_ERROR_REDUCTION;
constexpr uint32_t errorMs = PSU_TEMP_ERROR_MS;
constexpr uint32_t popupMs = 30000, rearmMs = 60000, recentMs = 30000, monitorPopupMs = 10000;
static_assert(clearC < warningC && warningC < errorC && errorC < stopC &&
    clearC >= control::minimumTemperature && stopC < control::maximumTemperature, "Invalid thermal thresholds");
static_assert(PSU_TEMP_WARNING_REDUCTION > 0 && PSU_TEMP_WARNING_REDUCTION < PSU_TEMP_ERROR_REDUCTION &&
    PSU_TEMP_ERROR_REDUCTION <= 100 && errorMs > 0 && errorMs < 0x80000000UL, "Invalid thermal reductions/timer");
constexpr bool validPin(int pin) {
  return pin == -1 || (pin >= 2 && pin < 70 && (pin < 50 || pin > 53) &&
      pin != board::canChipSelect && pin != board::canInterrupt && pin != control::chargeAllowPin && pin != control::fanPwmPin &&
      (!PSU_ENABLE_ENABLE_IN || pin != control::enablePin) &&
      (!PSU_ENABLE_ENCODER || (pin != board::encoderClock && pin != board::encoderData && pin != board::encoderButton)) &&
      (!PSU_ENABLE_DISPLAY || (pin != board::displayChipSelect && pin != board::displayDataCommand &&
          pin != board::displayReset && pin != board::displayBacklight)) &&
      (!PSU_ENABLE_TOUCH || (pin != touch::chipSelect && pin != touch::interruptPin)));
}
static_assert(validPin(PSU_WARNING_PIN) && validPin(PSU_ERROR_PIN), "Alert output conflicts with configured hardware");
static_assert(PSU_WARNING_PIN == -1 || PSU_WARNING_PIN != PSU_ERROR_PIN, "Alert outputs require separate pins");
} }
