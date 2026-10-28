#pragma once
#include "../config/BuildOptions.h"
#include "../config/BoardConfig.h"
#include "../ui/UiAction.h"
#if PSU_UI_DEBUG
#include "../debug/Simulator.h"
#else
#include "../can/Mcp2515Transport.h"
#include "../storage/ArduinoEeprom.h"
#endif
#include "../psu/PsuController.h"
#include "../io/FanOutput.h"
#include "../ui/LocalUi.h"
#include "../ui/Ili9341Display.h"
#include "../ui/SerialConsole.h"

namespace psu {
class Application {
 public:
  Application();
  void begin();
  void tick();
 private:
#if PSU_UI_DEBUG
  SimulatedCan can_;
  RamStorage eeprom_;
  DebugScenario desiredScenario_ = DebugScenario::Healthy;
  Result scenarioResult_ = Result::Ok;
  bool priming_ = true, changed_ = false;
  bool debugInputPresent_ = false, debugInput_ = false;
  UiAction debugRequest_;
  void resetDebug(bool restart, bool erase, DebugScenario scenario);
  void debugTick(uint32_t now);
#else
  Mcp2515Transport can_;
  ArduinoEeprom eeprom_;
#endif
  PsuController controller_;
#if PSU_FAN_PWM_PIN >= 0 && !PSU_UI_DEBUG
  FanOutput fanOutput_;
#endif
  MemoryManager memory_;
#if PSU_ENABLE_SERIAL
  SerialConsole console_;
  static bool command(void* context, uint8_t count, char** args, Print& output, uint32_t now);
#endif
#if PSU_ENABLE_DISPLAY
  Ili9341Display display_;
  LocalUi localUi_;
#endif
};
}
