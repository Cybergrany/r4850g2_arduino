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
  console_.displayStatus(localUi_.available());
#endif
  console_.finishStartup();
#endif
}
void Application::tick() {
  const uint32_t started = micros();
  // Receive/process CAN before optional producers. Storage has one owner and
  // advances only when EEPROM is ready, including serial-only builds.
  controller_.tick(millis());
  memory_.stepSave();
#if PSU_ENABLE_SERIAL
  console_.tick(millis());
#endif
#if PSU_ENABLE_DISPLAY
  localUi_.tick(millis());
#endif
  controller_.recordLoopTime(uint32_t(micros() - started));
}
}
