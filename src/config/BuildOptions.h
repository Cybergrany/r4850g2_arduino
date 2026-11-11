#pragma once

// These can also be overridden with PlatformIO build_flags. No UI is mandatory.
#ifndef PSU_ENABLE_SERIAL
#define PSU_ENABLE_SERIAL 1
#endif
#ifndef PSU_ENABLE_DISPLAY
#define PSU_ENABLE_DISPLAY 1
#endif
#ifndef PSU_ENABLE_ENCODER
#define PSU_ENABLE_ENCODER 1
#endif
#ifndef PSU_ENABLE_TOUCH
#define PSU_ENABLE_TOUCH 0
#endif
#ifndef PSU_UI_DEBUG
#define PSU_UI_DEBUG 0
#endif
#ifndef PSU_ENABLE_ENABLE_IN
#define PSU_ENABLE_ENABLE_IN 0
#endif
#if PSU_ENABLE_TOUCH && !PSU_ENABLE_DISPLAY
#error "Touch requires PSU_ENABLE_DISPLAY"
#endif
#ifndef PSU_DEBUG_COMPACT_STORAGE
#ifdef __AVR__
#define PSU_DEBUG_COMPACT_STORAGE 1
#else
#define PSU_DEBUG_COMPACT_STORAGE 0
#endif
#endif
#ifndef PSU_MAX_UNITS
#if PSU_UI_DEBUG && defined(__AVR__)
#define PSU_MAX_UNITS 4
#else
#define PSU_MAX_UNITS 9
#endif
#endif

// Static allocation only; Mega EEPROM holds the controller and eight profiles.
static_assert(PSU_MAX_UNITS >= 1 && PSU_MAX_UNITS <= 9, "Support 1..9 PSU slots");
