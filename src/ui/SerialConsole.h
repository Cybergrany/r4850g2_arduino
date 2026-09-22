#pragma once
#include "../config/BuildOptions.h"
#if PSU_ENABLE_SERIAL
#include <Arduino.h>
#include "BufferedOutput.h"
#include "../psu/PsuController.h"
#include "../storage/MemoryManager.h"
namespace psu {
class SerialConsole {
 public:
  // Stream must report writable capacity (HardwareSerial does); writes never
  // exceed it. A Stream with the default zero capacity produces no UART output.
  SerialConsole(Stream& stream, PsuController& controller, MemoryManager& memory)
      : stream_(stream), controller_(controller), memory_(memory) {}
  void begin(StorageResult loaded);
  void finishStartup();
  void displayStatus(bool available);
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
  void printUnit(uint8_t slot, uint8_t field, uint32_t now);
  void printFrame(int8_t index, const CanFrame& frame);
  static void onFrame(void* context, int8_t index, const CanFrame& frame);
  Stream& stream_;
  BufferedOutput io_;
  PsuController& controller_;
  MemoryManager& memory_;
  char line_[80] = {};
  uint8_t length_ = 0;
  bool discard_ = false, echo_ = true, afterCr_ = false;
  bool submitted_ = false, redraw_ = false, trailingDiscard_ = false;
  bool backlogNotice_ = false, resetNotice_ = false, expiredNotice_ = false;
  bool promptVisible_ = false, descriptionOpen_ = false;
  uint32_t lastInput_ = 0, now_ = 0;
  View view_ = View::None;
  uint8_t viewMask_ = 0xff, row_ = 0, field_ = 0;
  bool watch_ = false, raw_ = false, confirmation_ = false, wasBusy_ = false;
  uint8_t descriptionAddress_ = 0;
  uint32_t lastWatch_ = 0;
  struct Trace { CanFrame frame; int8_t slot; } trace_[console::traceSlots];
  uint8_t traceHead_ = 0, traceTail_ = 0, traceCount_ = 0;
  uint16_t traceDrops_ = 0, reportedTraceDrops_ = 0, reportedOverruns_ = 0, reportedReadFailures_ = 0;
  uint32_t lastTraceNotice_ = 0, saveRevision_ = 0;
  uint16_t saveToken_ = 0;
  bool waitingSave_ = false, traceTurn_ = false;
  ApplyPlan plan_ = {};
};
}
#endif
