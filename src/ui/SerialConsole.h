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
  enum class View : uint8_t { None, Help, DiagnosticHelp, Config, Bus, Units, Telemetry, Plan, Legacy };
  void execute();
  void resetSession();
  void beginOutput();
  void prompt();
  void reply(Result result);
  void storageReply(StorageResult result);
  void outputRow(uint32_t now);
  void startView(View view, uint8_t mask = 0xff);
  void printIdentity(const Identity& identity);
  void printMembers(uint8_t mask);
  void printUnit(uint8_t slot, uint32_t now);
  static void onFrame(void* context, int8_t index, const CanFrame& frame);
  Stream& io_;
  PsuController& controller_;
  MemoryManager& memory_;
  char line_[80] = {};
  uint8_t length_ = 0;
  bool discard_ = false, echo_ = true, afterCr_ = false;
  bool promptVisible_ = false, descriptionOpen_ = false;
  uint32_t lastInput_ = 0, now_ = 0;
  View view_ = View::None;
  uint8_t viewMask_ = 0xff, row_ = 0;
  bool watch_ = false, raw_ = false, confirmation_ = false, wasBusy_ = false;
  uint8_t descriptionAddress_ = 0;
  uint32_t lastWatch_ = 0;
  ApplyPlan plan_ = {};
};
}
#endif
