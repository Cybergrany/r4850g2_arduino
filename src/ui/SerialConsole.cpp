#include "SerialConsole.h"
#if PSU_ENABLE_SERIAL
#include "UiText.h"
#include "../config/ConsoleConfig.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

namespace psu {
namespace {
bool number(const char* text, float& value) {
  if (!text || !*text) return false;
  char* end = nullptr;
  value = strtod(text, &end);
  return *end == '\0' && isfinite(value);
}
bool integer(const char* text, uint16_t maximum, uint16_t& value) {
  if (!text || !*text) return false;
  uint32_t n = 0;
  for (const char* c = text; *c; ++c) {
    if (*c < '0' || *c > '9') return false;
    n = n * 10 + (*c - '0');
    if (n > maximum) return false;
  }
  value = n; return true;
}
bool parameter(const char* name, Parameter& p) {
  if (!strcmp(name, "voltage")) p = Parameter::Voltage;
  else if (!strcmp(name, "current")) p = Parameter::Current;
  else if (!strcmp(name, "offline-voltage")) p = Parameter::OfflineVoltage;
  else if (!strcmp(name, "offline-current")) p = Parameter::OfflineCurrent;
  else if (!strcmp(name, "rated-current")) p = Parameter::RatedCurrent;
  else if (!strcmp(name, "address")) p = Parameter::Address;
  else if (!strcmp(name, "enabled")) p = Parameter::Enabled;
  else return false;
  return true;
}
const __FlashStringHelper* metricName(uint8_t m) {
  switch (m) {
    case protocol::InputPower: return F("input-power W=");
    case protocol::InputFrequency: return F("input-frequency Hz=");
    case protocol::InputCurrent: return F("input-current A=");
    case protocol::OutputPower: return F("output-power W=");
    case protocol::Efficiency: return F("efficiency %=");
    case protocol::OutputVoltage: return F("output-voltage V=");
    case protocol::CurrentCapacity: return F("available-current A=");
    case protocol::InputVoltage: return F("input-voltage V=");
    case protocol::OutputTemperature: return F("output-temperature C=");
    case protocol::InputTemperature: return F("input-temperature C=");
    case protocol::OutputCurrent: return F("output-current A=");
    default: return F("filtered-current A=");
  }
}
}
void SerialConsole::begin(StorageResult loaded) {
  resetSession();
  promptVisible_ = descriptionOpen_ = false;
  controller_.observeFrames(onFrame, this);
  io_.println(F("R4850 modular controller; help for commands"));
  storageReply(loaded);
  io_.println(controller_.ready() ? F("CAN ready") : F("CAN failed; config console remains available"));
}
void SerialConsole::finishStartup() {
  io_.println(F("Startup complete; Enter submits, help lists commands"));
  prompt();
}
void SerialConsole::resetSession() {
  length_ = 0; discard_ = afterCr_ = false; echo_ = true;
  view_ = View::None; watch_ = raw_ = false; descriptionIndex_ = -1;
}
void SerialConsole::beginOutput() {
  if (promptVisible_ || descriptionOpen_) io_.println();
  promptVisible_ = descriptionOpen_ = false;
}
void SerialConsole::prompt() {
  if (promptVisible_) return;
  beginOutput();
  io_.print(F("> "));
  if (echo_) for (uint8_t i = 0; i < length_; ++i) io_.write(uint8_t(line_[i]));
  promptVisible_ = true;
}
void SerialConsole::reply(Result r) { io_.println(resultName(r)); }
void SerialConsole::storageReply(StorageResult r) {
  switch (r) {
    case StorageResult::Ok: io_.println(F("EEPROM OK")); break;
    case StorageResult::NoValidRecord: io_.println(F("EEPROM has no valid record; RAM unchanged")); break;
    case StorageResult::TooSmall: io_.println(F("ERR EEPROM needs 512 bytes")); break;
    case StorageResult::InvalidConfig: io_.println(F("ERR invalid configuration")); break;
    case StorageResult::WriteFailed: io_.println(F("ERR EEPROM verification failed")); break;
  }
}
bool SerialConsole::target(const char* text, uint8_t& first, uint8_t& end) const {
  if (!text || !strcmp(text, "all")) { first = 0; end = controller_.count(); return true; }
  // Human-facing slots are 1-based and ranges inclusive, e.g. 2-4.
  const char* dash = strchr(text, '-');
  uint16_t start = 0, last = 0;
  if (!dash) {
    if (!integer(text, controller_.count(), start) || !start) return false;
    last = start;
  } else {
    if (dash == text || dash - text > 2) return false;
    char prefix[3] = {};
    memcpy(prefix, text, dash - text);
    if (!integer(prefix, controller_.count(), start) || !start ||
        !integer(dash + 1, controller_.count(), last) || last < start) return false;
  }
  first = start - 1; end = last; return true;
}
void SerialConsole::startView(View view, uint8_t first, uint8_t end) {
  view_ = view; viewIndex_ = first; viewEnd_ = end; row_ = 0;
}
void SerialConsole::execute() {
  char* args[5] = {};
  char* context = nullptr;
  uint8_t argc = 0;
  for (char* word = strtok_r(line_, " \t", &context); word; word = strtok_r(nullptr, " \t", &context)) {
    if (argc == 5) { reply(Result::Invalid); return; }
    args[argc++] = word;
  }
  if (!argc) return;
  view_ = View::None;
  uint8_t first = 0, end = controller_.count();
  uint16_t n = 0;
  if (!strcmp(args[0], "hello") && argc == 1) {
    resetSession();
    io_.println(F("R4850 console ready; help for commands; Ctrl-X resets console"));
    return;
  }
  if (!strcmp(args[0], "help") && argc == 1) { startView(View::Help, 0, 0); return; }
  if ((!strcmp(args[0], "config") || !strcmp(args[0], "status")) && argc <= 2 && target(args[1], first, end)) {
    startView(!strcmp(args[0], "config") ? View::Config : View::Status, first, end); return;
  }
  if (!strcmp(args[0], "set") && argc == 4 && target(args[1], first, end)) {
    Parameter p; float value;
    reply(parameter(args[2], p) && number(args[3], value)
        ? controller_.modifyRange(first, end, p, value) : Result::Invalid); return;
  }
  if (!strcmp(args[0], "count") && argc == 2 && integer(args[1], PSU_MAX_UNITS, n)) {
    reply(controller_.setCount(n)); return;
  }
  if (!strcmp(args[0], "interval") && argc == 2 && integer(args[1], limits::maxPollMs, n)) {
    reply(controller_.setPollInterval(n)); return;
  }
  if (!strcmp(args[0], "bootapply") && argc == 2 && integer(args[1], 1, n)) {
    reply(controller_.setApplyOnBoot(n)); return;
  }
  if ((!strcmp(args[0], "apply") || !strcmp(args[0], "persist") || !strcmp(args[0], "offline") ||
       !strcmp(args[0], "poll") || !strcmp(args[0], "reset-ah")) && argc == 2 && target(args[1], first, end)) {
    if (!strcmp(args[0], "poll")) reply(controller_.requestData(first, end));
    else if (!strcmp(args[0], "reset-ah")) reply(controller_.resetAmpHours(first, end));
    else {
      const ApplyMode mode = !strcmp(args[0], "persist") ? ApplyMode::OnlineAndOffline :
                             !strcmp(args[0], "offline") ? ApplyMode::Offline : ApplyMode::Online;
      const Result r = controller_.applyRange(first, end, mode);
      if (r == Result::Ok) io_.println(F("QUEUED; status reports PSU acceptance/failure"));
      else reply(r);
    }
    return;
  }
  if (!strcmp(args[0], "describe") && argc == 2 && target(args[1], first, end) && end == first + 1) {
    const Result r = controller_.requestDescription(first);
    if (r == Result::Ok) descriptionIndex_ = first;
    reply(r); return;
  }
  if ((!strcmp(args[0], "watch") || !strcmp(args[0], "raw") || !strcmp(args[0], "echo")) && argc == 2 &&
      (!strcmp(args[1], "on") || !strcmp(args[1], "off"))) {
    const bool on = !strcmp(args[1], "on");
    if (!strcmp(args[0], "watch")) watch_ = on;
    else if (!strcmp(args[0], "raw")) raw_ = on;
    else echo_ = on;
    reply(Result::Ok); return;
  }
  if ((!strcmp(args[0], "save") || !strcmp(args[0], "load") || !strcmp(args[0], "defaults")) && argc == 1) {
    if (controller_.busy()) { reply(Result::Busy); return; }
    if (!strcmp(args[0], "defaults")) { reply(controller_.configure(defaultConfiguration())); return; }
    Configuration c = controller_.configuration();
    if (!strcmp(args[0], "save")) storageReply(memory_.save(c));
    else {
      const StorageResult r = memory_.load(c);
      if (r == StorageResult::Ok) reply(controller_.configure(c));
      storageReply(r);
    }
    return;
  }
  io_.println(F("ERR syntax/value; type help"));
}
void SerialConsole::tick(uint32_t now) {
  // Timeout precedes consumption: bytes arriving after a long gap cannot
  // complete an abandoned command. Discard through EOL rather than executing
  // a suffix as a new command. Ctrl-X/C can explicitly establish a fresh session.
  if (length_ && !discard_ && uint32_t(now - lastInput_) >= console::inputIdleTimeoutMs) {
    beginOutput(); length_ = 0; discard_ = true;
    io_.println(F("Input expired; Enter discards remainder, Ctrl-X resets console"));
    prompt();
  }
  // Never wait for a newline or call readString/readBytes/parseFloat.
  for (uint8_t n = 0; n < console::inputBytesPerTick && io_.available(); ++n) {
    const int c = io_.read();
    if (c < 0) break;
    lastInput_ = now;
    if (c == 3 || c == 24) { // Ctrl-C / Ctrl-X: console only; no PSU cancellation.
      beginOutput(); resetSession();
      io_.println(F("Console reset; PSU jobs unchanged; help for commands"));
      prompt(); continue;
    }
    if (c == 21) { // Ctrl-U: clear the entire line, including invalid/overflow state.
      beginOutput(); length_ = 0; discard_ = afterCr_ = false;
      prompt(); continue;
    }
    // Treat CRLF as one Enter, even when its bytes arrive in separate ticks.
    if (afterCr_ && c == '\n') { afterCr_ = false; continue; }
    afterCr_ = c == '\r';
    if (c == '\n' || c == '\r') {
      if (descriptionOpen_) beginOutput();
      io_.println(); promptVisible_ = false;
      if (discard_) io_.println(F("ERR incomplete/invalid line; discarded"));
      else if (length_) { line_[length_] = 0; execute(); }
      length_ = 0; discard_ = false; afterCr_ = c == '\r';
      if (view_ == View::None) prompt();
    } else if ((c == 8 || c == 127) && !discard_) {
      if (length_) {
        prompt();
        --length_;
        if (echo_) io_.print(F("\b \b"));
      }
    } else if (!discard_) {
      if ((c < 32 && c != '\t') || c > 126 || length_ >= sizeof(line_) - 1) discard_ = true;
      else {
        // Normalize tabs so one stored character corresponds to one displayed cell.
        prompt();
        line_[length_++] = c == '\t' ? ' ' : char(c);
        if (echo_) io_.write(uint8_t(line_[length_ - 1]));
      }
    }
  }
  if (watch_ && !length_ && !discard_ && view_ == View::None && uint32_t(now - lastWatch_) >= 1000) {
    lastWatch_ = now; startView(View::Status, 0, controller_.count());
  }
  if (view_ != View::None && !length_ && !discard_) {
    beginOutput(); outputRow(now);
    if (view_ == View::None) prompt();
  }
}
void SerialConsole::outputRow(uint32_t now) {
  // Emit one short row per loop so CAN processing continues through long reports.
  if (view_ == View::Help) {
    switch (row_++) {
      case 0: io_.println(F("Targets: 1, 2-4, all (slots, not CAN addresses)")); break;
      case 1: io_.println(F("config [target] / status [target]")); break;
      case 2: io_.println(F("set <target> <parameter> <value> (stages RAM only)")); break;
      case 3: io_.println(F("parameters: voltage/current (V/A)")); break;
      case 4: io_.println(F("offline-voltage/offline-current (V/A)")); break;
      case 5: io_.println(F("rated-current (A), address (1..127), enabled (0/1)")); break;
      case 6: io_.print(F("count <1..")); io_.print(PSU_MAX_UNITS); io_.println(F(">")); break;
      case 7: io_.println(F("interval <1000..10000 ms> / bootapply <0|1>")); break;
      case 8: io_.println(F("apply <target> = online voltage/current")); break;
      case 9: io_.println(F("offline <target> = PSU nonvolatile defaults")); break;
      case 10: io_.println(F("persist <target> = online AND offline settings")); break;
      case 11: io_.println(F("save / load = controller EEPROM only")); break;
      case 12: io_.println(F("defaults = reset RAM only; save to persist")); break;
      case 13: io_.println(F("poll <target> / describe <one slot>")); break;
      case 14: io_.println(F("watch <on|off> / raw <on|off> / reset-ah <target>")); break;
      case 15: io_.println(F("echo <on|off> (on by default; disable local echo)")); break;
      case 16: io_.println(F("hello = quiet console; Ctrl-X/C = cancel input + quiet console")); break;
      case 17:
        io_.print(F("Ctrl-U = clear line; input idle timeout ms="));
        io_.println(console::inputIdleTimeoutMs); break;
      case 18: io_.println(F("Console reset leaves PSU jobs/settings unchanged.")); break;
      case 19: io_.println(F("Ranges use per-PSU A, not total bank A.")); break;
      default: view_ = View::None; break;
    }
    return;
  }
  if (viewIndex_ >= controller_.count()) { view_ = View::None; return; }
  const Psu& unit = controller_.unit(viewIndex_);
  const auto& c = unit.config();
  io_.print(F("PSU ")); io_.print(viewIndex_ + 1); io_.print(' ');
  if (view_ == View::Config) {
    switch (row_) {
      case 0:
        io_.print(F("addr=")); io_.print(c.address); io_.print(F(" enabled=")); io_.println(c.enabled); break;
      case 1:
        io_.print(F("online V=")); io_.print(c.voltage / 100.0f); io_.print(F(" A=")); io_.println(c.current / 100.0f); break;
      case 2:
        io_.print(F("offline V=")); io_.print(c.offlineVoltage / 100.0f); io_.print(F(" A=")); io_.println(c.offlineCurrent / 100.0f); break;
      case 3:
        io_.print(F("rated A=")); io_.println(c.ratedCurrent / 100.0f); break;
      default:
        io_.print(F("count=")); io_.print(controller_.count()); io_.print(F(" interval=")); io_.print(controller_.pollInterval());
        io_.print(F(" bootapply=")); io_.println(controller_.applyOnBoot()); break;
    }
    if (++row_ == 5) { row_ = 0; ++viewIndex_; }
  } else {
    if (row_ < protocol::MetricCount) {
      io_.print(metricName(row_));
      if (!unit.hasMetric(protocol::Metric(row_))) io_.println(F("N/A"));
      else io_.println(unit.metric(protocol::Metric(row_)) * (row_ == protocol::Efficiency ? 100 : 1));
    } else if (row_ == protocol::MetricCount) {
      io_.print(F("Ah~=")); io_.print(unit.telemetry().ampHours, 4);
      io_.print(F(" link="));
      io_.println(unit.stale(now, uint32_t(controller_.pollInterval()) * 3) ? F("stale") : F("seen"));
    } else if (row_ == protocol::MetricCount + 1) {
      io_.print(F("command=")); io_.print(stateName(unit.commandStatus().state));
      io_.print(F(" reg=")); io_.print(unit.commandStatus().reg); io_.print(F(" raw=")); io_.println(unit.commandStatus().rawValue);
    } else {
      io_.print(F("rx-drops=")); io_.print(controller_.droppedFrames());
      io_.print(F(" tx-errors=")); io_.print(controller_.txFailures());
      io_.print(F(" unknown=")); io_.println(controller_.unknownFrames());
    }
    if (++row_ == protocol::MetricCount + 3) { row_ = 0; ++viewIndex_; }
  }
  if (viewIndex_ >= viewEnd_) view_ = View::None;
}
void SerialConsole::onFrame(void* context, int8_t index, const CanFrame& frame) {
  auto& self = *static_cast<SerialConsole*>(context);
  auto& out = self.io_;
  if (index >= 0 && protocol::isReply(frame)) {
    const uint8_t cmd = protocol::command(frame.id);
    if (cmd == protocol::descriptionCommand && index == self.descriptionIndex_) {
      // Stream complete description, including its final ..7E fragment, without a large string.
      if (!self.descriptionOpen_) self.beginOutput();
      for (uint8_t i = 2; i < 8; ++i) {
        // Device strings must not inject terminal controls or NUL padding.
        const uint8_t c = frame.data[i];
        if (c) out.write(c >= 32 && c <= 126 ? c : '?');
      }
      self.descriptionOpen_ = true;
      if (!(frame.id & 1)) {
        out.println(); self.descriptionOpen_ = false; self.descriptionIndex_ = -1;
        if (self.view_ == View::None) self.prompt();
      }
    } else if (cmd == protocol::setCommand && (frame.data[0] == 1 || frame.data[0] == 0x21)) {
      self.beginOutput();
      const uint32_t raw = protocol::readBigEndian(frame.data + 4);
      out.print(F("ACK ")); out.print(index + 1); out.print(F(" reg=")); out.print(frame.data[1]);
      out.print(frame.data[0] & 0x20 ? F(" rejected ") : F(" accepted "));
      if (frame.data[1] <= protocol::Overvoltage) { out.print(raw / 1024.0f); out.println('V'); }
      else if (frame.data[1] <= protocol::OfflineCurrent) {
        out.print(raw / 1024.0f * self.controller_.unit(index).config().ratedCurrent / 100.0f); out.println('A');
      } else out.println(raw);
      if (self.view_ == View::None) self.prompt();
    }
  }
  if (self.raw_) {
    self.beginOutput();
    out.print(frame.id, HEX); out.print(' ');
    for (uint8_t i = 0; i < frame.length && i < 8; ++i) {
      if (frame.data[i] < 16) out.print('0');
      out.print(frame.data[i], HEX); out.print(' ');
    }
    out.println();
    if (self.view_ == View::None) self.prompt();
  }
}
}
#endif
