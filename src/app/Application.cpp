#include "Application.h"
#include <Arduino.h>

namespace psu {
Application::Application() : controller_(can_), memory_(eeprom_)
#if PSU_ENABLE_SERIAL
  , console_(Serial, controller_, memory_)
#endif
#if PSU_ENABLE_DISPLAY
  , localUi_(controller_, memory_, display_)
#endif
{}
void Application::begin() {
  static_assert(board::eepromBudget == MemoryManager::budget, "EEPROM budgets must agree");
  Configuration config = defaultConfiguration();
  const StorageResult loaded = memory_.load(config);
  controller_.configure(config, loaded == StorageResult::Ok);
  // Complete display reset/clear before CAN reception. Runtime drawing is bounded.
#if PSU_ENABLE_DISPLAY
  localUi_.begin();
#endif
  controller_.begin();
#if PSU_ENABLE_SERIAL
  Serial.begin(board::serialBaud); // Boot does not wait for an attached serial monitor.
  console_.begin(loaded);
#else
  (void)loaded;
#endif
#if PSU_ENABLE_SERIAL
#if PSU_ENABLE_DISPLAY
  Serial.println(localUi_.available() ? F("ILI9341 ready") : F("ILI9341 unavailable; local controls disabled"));
#endif
  console_.finishStartup();
#endif
}
void Application::tick() {
#if PSU_ENABLE_SERIAL
  console_.tick(millis());
#endif
  controller_.tick(millis());
#if PSU_ENABLE_DISPLAY
  localUi_.tick(millis());
#endif
}
}
