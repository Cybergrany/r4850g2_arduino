#include "StackMonitor.h"
#if defined(__AVR__) && PSU_STACK_MONITOR
#include <avr/io.h>
extern "C" {
extern char __heap_start;
extern void* __brkval;
// .init3 runs after SP initialization and before .data/.bss initialization and
// constructors. Paint only the unallocated region, never the application data.
void psu_stack_paint() __attribute__((naked, used, section(".init3")));
void psu_stack_paint() {
  asm volatile(
    "ldi r30, lo8(__heap_start)\n\t"
    "ldi r31, hi8(__heap_start)\n\t"
    "in r24, __SP_L__\n\t"
    "in r25, __SP_H__\n\t"
    "ldi r18, 0xa5\n"
    "1: cp r30, r24\n\t"
    "cpc r31, r25\n\t"
    "brsh 2f\n\t"
    "st Z+, r18\n\t"
    "rjmp 1b\n"
    "2:\n\t" ::: "r18", "r24", "r25", "r30", "r31");
}
}
namespace psu {
StackUsage stackUsage() {
  const auto start = reinterpret_cast<uint16_t>(&__heap_start);
  const auto heap = __brkval ? reinterpret_cast<uint16_t>(__brkval) : start;
  const uint16_t top = SP;
  uint16_t at = heap;
  while (at < top && *reinterpret_cast<const volatile uint8_t*>(at) == 0xa5) ++at;
  return {uint16_t(RAMEND + 1 - start), uint16_t(top > heap ? top - heap : 0), uint16_t(at - heap), true};
}
}
#endif
