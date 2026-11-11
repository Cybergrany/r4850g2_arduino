#pragma once
#include <stdint.h>
#ifndef PSU_STACK_MONITOR
#define PSU_STACK_MONITOR 1
#endif
namespace psu {
struct StackUsage { uint16_t staticFree, freeNow, untouched; bool supported; };
#if defined(__AVR__) && PSU_STACK_MONITOR
StackUsage stackUsage();
#else
inline StackUsage stackUsage() { return {0,0,0,false}; }
#endif
}
