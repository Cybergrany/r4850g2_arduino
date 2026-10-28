#pragma once
// ATmega2560 Timer4/5 register adapter for the physical fan-output harness.
#define F_CPU 16000000UL
#define _BV(bit) (1U << (bit))
constexpr uint8_t WGM41 = 1, WGM43 = 4, CS40 = 0;
constexpr uint8_t COM4A1 = 7, COM4B1 = 5, COM4C1 = 3;
constexpr uint8_t COM5A1 = 7, COM5B1 = 5, COM5C1 = 3;
extern volatile uint8_t TCCR4A, TCCR4B, TIMSK4, TCCR5A, TCCR5B, TIMSK5;
extern volatile uint16_t TCNT4, ICR4, OCR4A, OCR4B, OCR4C, TCNT5, ICR5, OCR5A, OCR5B, OCR5C;
