#include "FanOutput.h"
#if PSU_FAN_PWM_PIN >= 0 && !PSU_UI_DEBUG
#include <Arduino.h>
#if !defined(__AVR_ATmega2560__) && !defined(PSU_TEST_FAN_GPIO)
#error "25 kHz fan output requires the Mega 2560 adapter"
#endif
#if PSU_FAN_PWM_PIN <= 8
#define FAN_TIMER_A TCCR4A
#define FAN_TIMER_B TCCR4B
#define FAN_TIMER_MASK TIMSK4
#define FAN_TIMER_COUNT TCNT4
#define FAN_TIMER_TOP ICR4
#if PSU_FAN_PWM_PIN == 6
#define FAN_COMPARE OCR4A
#define FAN_COMPARE_BIT COM4A1
#elif PSU_FAN_PWM_PIN == 7
#define FAN_COMPARE OCR4B
#define FAN_COMPARE_BIT COM4B1
#else
#define FAN_COMPARE OCR4C
#define FAN_COMPARE_BIT COM4C1
#endif
#else
#define FAN_TIMER_A TCCR5A
#define FAN_TIMER_B TCCR5B
#define FAN_TIMER_MASK TIMSK5
#define FAN_TIMER_COUNT TCNT5
#define FAN_TIMER_TOP ICR5
#if PSU_FAN_PWM_PIN == 46
#define FAN_COMPARE OCR5A
#define FAN_COMPARE_BIT COM5A1
#elif PSU_FAN_PWM_PIN == 45
#define FAN_COMPARE OCR5B
#define FAN_COMPARE_BIT COM5B1
#else
#define FAN_COMPARE OCR5C
#define FAN_COMPARE_BIT COM5C1
#endif
#endif
#endif

namespace psu {
#if PSU_FAN_PWM_PIN >= 0 && !PSU_UI_DEBUG
namespace {
constexpr uint16_t timerTop = F_CPU / (2UL * control::fanPwmHz);
static_assert(F_CPU % (2UL * control::fanPwmHz) == 0 && timerTop >= 255, "Fan PWM clock/resolution mismatch");
uint16_t compare(uint8_t demand) {
  const uint8_t electrical = control::fanPwmInverted ? 255 - demand : demand;
  return (uint32_t(electrical) * timerTop + 127) / 255;
}
}
#endif
void FanOutput::begin() {
  updated_ = 0; demand_ = 255;
#if PSU_FAN_PWM_PIN >= 0 && !PSU_UI_DEBUG
  // Full cooling before display/CAN setup, then claim only the chosen timer.
  digitalWrite(control::fanPwmPin, control::fanPwmInverted ? LOW : HIGH);
  pinMode(control::fanPwmPin, OUTPUT);
  FAN_TIMER_B = 0; FAN_TIMER_A = 0; FAN_TIMER_MASK = 0;
  FAN_TIMER_COUNT = 0; FAN_TIMER_TOP = timerTop; FAN_COMPARE = compare(255);
  // Mode 10: phase-correct PWM, fixed ICR TOP, prescaler 1. Timer4/5 use
  // identical WGM/CS bit positions. No ISR; Timer0/time and Timer1/wheel untouched.
  FAN_TIMER_A = _BV(WGM41) | _BV(FAN_COMPARE_BIT);
  FAN_TIMER_B = _BV(WGM43) | _BV(CS40);
#endif
}
bool FanOutput::due(uint32_t now) {
  if (uint32_t(now - updated_) < control::externalFanUpdateMs) return false;
  updated_ = now; return true;
}
void FanOutput::write(uint8_t demand) {
  if (demand == demand_) return;
  demand_ = demand;
#if PSU_FAN_PWM_PIN >= 0 && !PSU_UI_DEBUG
  // Phase-correct mode gives genuine DC at OCR=0/TOP, without endpoint spikes.
  // This timer has no ISR or other owner, so its 16-bit TEMP latch is unshared.
  FAN_COMPARE = compare(demand);
#endif
}
}
