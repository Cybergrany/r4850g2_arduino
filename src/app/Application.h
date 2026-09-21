#pragma once
#include "../can/Mcp2515Transport.h"
#include "../psu/PsuController.h"
#include "../storage/ArduinoEeprom.h"
#include "../ui/LocalUi.h"
#include "../ui/SerialConsole.h"

namespace psu {
class Application {
 public:
  Application();
  void begin();
  void tick();
 private:
  Mcp2515Transport can_;
  PsuController controller_;
  ArduinoEeprom eeprom_;
  MemoryManager memory_;
#if PSU_ENABLE_SERIAL
  SerialConsole console_;
#endif
#if PSU_ENABLE_DISPLAY || PSU_ENABLE_ENCODER
  LocalUi localUi_;
#endif
};
}
