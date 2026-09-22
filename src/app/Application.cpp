#include "Application.h"
#include <Arduino.h>

namespace psu {
Application::Application() : controller_(can_), memory_(eeprom_)
#if PSU_ENABLE_SERIAL
  , console_(Serial, controller_, memory_)
#endif
#if PSU_ENABLE_DISPLAY || PSU_ENABLE_ENCODER
  , localUi_(controller_, memory_)
#endif
{}
void Application::begin() {
  static_assert(board::eepromBudget == MemoryManager::budget, "EEPROM budgets must agree");
  Configuration config = defaultConfiguration();
  const StorageResult loaded = memory_.load(config);
  controller_.configure(config);
  controller_.begin();
#if PSU_ENABLE_SERIAL
  Serial.begin(board::serialBaud); // Boot does not wait for an attached serial monitor.
  console_.begin(loaded);
#else
  (void)loaded;
#endif
#if PSU_ENABLE_DISPLAY || PSU_ENABLE_ENCODER
  localUi_.begin();
#endif
  // Restoring EEPROM alone never writes the PSU's nonvolatile defaults.
  if (loaded == StorageResult::Ok && config.applyOnBoot)
    controller_.applyRange(0, controller_.count());
#if PSU_ENABLE_SERIAL
  console_.finishStartup();
#endif
}
void Application::tick() {
#if PSU_ENABLE_SERIAL
  console_.tick(millis());
#endif
  controller_.tick(millis());
#if PSU_ENABLE_DISPLAY || PSU_ENABLE_ENCODER
  localUi_.tick(millis());
#endif
}
}
