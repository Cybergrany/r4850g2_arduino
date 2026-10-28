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
#ifndef PSU_MAX_UNITS
#define PSU_MAX_UNITS 8
#endif

// Static allocation only. Eight units also fit the 512-byte EEPROM journal.
static_assert(PSU_MAX_UNITS >= 1 && PSU_MAX_UNITS <= 8, "Support 1..8 PSU slots");
