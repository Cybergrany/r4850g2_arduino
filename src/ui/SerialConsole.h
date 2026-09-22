#pragma once
#include "../config/BuildOptions.h"
#if PSU_ENABLE_SERIAL
#include <Arduino.h>
#include "../psu/PsuController.h"
#include "../storage/MemoryManager.h"

namespace psu {
class SerialConsole {
 public:
  SerialConsole(Stream& stream, PsuController& controller, MemoryManager& memory)
      : io_(stream), controller_(controller), memory_(memory) {}
  void begin(StorageResult loaded);
  void finishStartup();
  void tick(uint32_t now);
 private:
  enum class View : uint8_t { None, Help, Config, Status };
  void execute();
  void resetSession();
  void beginOutput();
  void prompt();
  bool target(const char* text, uint8_t& first, uint8_t& end) const;
  void reply(Result result);
  void storageReply(StorageResult result);
  void outputRow(uint32_t now);
  void startView(View view, uint8_t first, uint8_t end);
  static void onFrame(void* context, int8_t index, const CanFrame& frame);
  Stream& io_;
  PsuController& controller_;
  MemoryManager& memory_;
  char line_[80] = {};
  uint8_t length_ = 0;
  bool discard_ = false;
  bool echo_ = true;
  bool afterCr_ = false;
  bool promptVisible_ = false;
  bool descriptionOpen_ = false;
  uint32_t lastInput_ = 0;
  View view_ = View::None;
  uint8_t viewIndex_ = 0;
  uint8_t viewEnd_ = 0;
  uint8_t row_ = 0;
  bool watch_ = false;
  bool raw_ = false;
  int8_t descriptionIndex_ = -1;
  uint32_t lastWatch_ = 0;
};
}
#endif
