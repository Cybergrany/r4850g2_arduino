#pragma once
#include <stdio.h>
#include <string.h>
// Literal-only arguments: avoid copying command words and format strings into
// scarce AVR SRAM. Host builds retain the normal libc format/type checks.
#ifdef __AVR__
#include <avr/pgmspace.h>
#define PSU_SNPRINTF(out, size, literal, ...) snprintf_P(out, size, PSTR(literal), ##__VA_ARGS__)
#define PSU_STRCMP(value, literal) strcmp_P(value, PSTR(literal))
#define PSU_CONST_STORAGE PROGMEM
#define PSU_COPY_CONSTANT(out, source, size) memcpy_P(out, source, size)
#else
#define PSU_SNPRINTF(out, size, literal, ...) snprintf(out, size, literal, ##__VA_ARGS__)
#define PSU_STRCMP(value, literal) strcmp(value, literal)
#define PSU_CONST_STORAGE
#define PSU_COPY_CONSTANT(out, source, size) memcpy(out, source, size)
#endif
