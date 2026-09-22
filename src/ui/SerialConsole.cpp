#include "SerialConsole.h"
#if PSU_ENABLE_SERIAL
#include "UiText.h"
#include "../config/ConsoleConfig.h"
#include "../config/BoardConfig.h"
#include "../config/Deployment.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
namespace psu {
namespace {
bool integer(const char* text, uint16_t maximum, uint16_t& value) {
  if (!text || !*text) return false;
  uint32_t n = 0;
  for (const char* c = text; *c; ++c) {
    if (*c < '0' || *c > '9') return false;
    n = n * 10 + (*c - '0'); if (n > maximum) return false;
  }
  value = n; return true;
}
bool decimal(const char* text, uint16_t& value) {
  if (!text || !*text) return false;
  char* end; const float f = strtod(text, &end);
  if (*end || !isfinite(f) || f < 0 || f > 655.35f) return false;
  value = uint16_t(f * 100 + 0.5f); return true;
}
bool identity(const char* text, Identity& id) {
  if (strlen(text) != 12) return false;
  for (uint8_t i = 0; i < 6; ++i) {
    uint8_t v = 0;
    for (uint8_t j = 0; j < 2; ++j) {
      char c = text[i * 2 + j];
      if (c >= 'a' && c <= 'f') c -= 'a' - 'A';
      if (c >= '0' && c <= '9') v = v * 16 + c - '0';
      else if (c >= 'A' && c <= 'F') v = v * 16 + c - 'A' + 10;
      else return false;
    }
    id.bytes[i] = v;
  }
  return identified(id);
}
bool selection(const char* s, uint8_t count, uint8_t& mask) {
  if (!strcmp(s, "all")) { mask = membersMask(count); return true; }
  mask = 0;
  while (*s) {
    uint16_t a = 0, b = 0; bool digit = false;
    while (*s >= '0' && *s <= '9') { digit = true; a = a * 10 + *s++ - '0'; if (a > count) return false; }
    if (!digit || !a) return false;
    b = a;
    if (*s == '-') {
      ++s; b = 0; digit = false;
      while (*s >= '0' && *s <= '9') { digit = true; b = b * 10 + *s++ - '0'; if (b > count) return false; }
      if (!digit || b < a) return false;
    }
    for (uint8_t i = a; i <= b; ++i) mask |= 1U << (i - 1);
    if (!*s) return true;
    if (*s++ != ',' || !*s) return false;
  }
  return false;
}
const __FlashStringHelper* metricName(uint8_t metric) {
  switch (metric) {
    case protocol::InputPower: return F("input-power W");
    case protocol::InputFrequency: return F("input-frequency Hz");
    case protocol::InputCurrent: return F("input-current A");
    case protocol::OutputPower: return F("output-power W");
    case protocol::Efficiency: return F("efficiency %");
    case protocol::OutputVoltage: return F("output-voltage V");
    case protocol::CurrentCapacity: return F("available-current A");
    case protocol::InputVoltage: return F("input-voltage V");
    case protocol::OutputTemperature: return F("output-temperature C");
    case protocol::InputTemperature: return F("input-temperature C");
    case protocol::OutputCurrent: return F("output-current A");
    case protocol::FilteredOutputCurrent: return F("filtered-current A");
    default: return F("unknown");
  }
}
}
void SerialConsole::resetSession() {
  length_ = 0; discard_ = afterCr_ = false; echo_ = true;
  view_ = View::None; watch_ = raw_ = confirmation_ = false; descriptionAddress_ = 0;
}
void SerialConsole::beginOutput() {
  if (promptVisible_ || descriptionOpen_) io_.println();
  promptVisible_ = descriptionOpen_ = false;
}
void SerialConsole::prompt() {
  if (promptVisible_) return;
  beginOutput(); io_.print(F("> "));
  if (echo_) for (uint8_t i = 0; i < length_; ++i) io_.write(uint8_t(line_[i]));
  promptVisible_ = true;
}
void SerialConsole::begin(StorageResult loaded) {
  resetSession(); promptVisible_ = descriptionOpen_ = false;
  controller_.observeFrames(onFrame, this);
  io_.println(F("R4850 parallel controller; help / help diagnostics")); storageReply(loaded);
  io_.println(controller_.ready() ? F("CAN ready; discovering identities (not proof of PSU presence)") : F("CAN failed; console available"));
}
void SerialConsole::finishStartup() { io_.println(F("Startup complete; Enter submits, Ctrl-X resets console")); prompt(); }
void SerialConsole::reply(Result r) { io_.println(resultName(r)); }
void SerialConsole::storageReply(StorageResult r) {
  switch (r) {
    case StorageResult::Ok: io_.println(F("EEPROM OK; saved drafts and authorized profile are separate")); break;
    case StorageResult::NoValidRecord: io_.println(F("EEPROM empty/invalid; no settings applied")); break;
    case StorageResult::TooSmall: io_.println(F("ERR EEPROM needs 512 bytes")); break;
    case StorageResult::InvalidConfig: io_.println(F("ERR invalid configuration")); break;
    case StorageResult::WriteFailed: io_.println(F("ERR EEPROM verification failed")); break;
    case StorageResult::Migrated: io_.println(F("Legacy settings migrated to RAM GROUP1; bind identities, apply, save. Auto-resume OFF.")); break;
    case StorageResult::LegacyNeedsReview: io_.println(F("BLOCKED legacy per-unit settings differ: use legacy to inspect. EEPROM untouched. Commission parallel groups before save.")); break;
    case StorageResult::WrongDeployment: io_.println(F("BLOCKED wrong deployment: inspect config; defaults selects compiled deployment. EEPROM untouched.")); break;
  }
}
void SerialConsole::printIdentity(const Identity& id) {
  for (auto b : id.bytes) { if (b < 16) io_.print('0'); io_.print(b, HEX); }
}
void SerialConsole::printMembers(uint8_t m) {
  if (!m) { io_.print('-'); return; }
  bool first = true;
  for (uint8_t i = 0; i < PSU_MAX_UNITS; ++i) if (m & (1U << i)) {
    if (!first) io_.print(',');
    first = false; io_.print(i + 1);
  }
}
void SerialConsole::startView(View view, uint8_t mask) { view_ = view; row_ = 0; viewMask_ = mask; }
void SerialConsole::execute() {
  char* args[6] = {}; char* context = nullptr; uint8_t argc = 0;
  for (char* word = strtok_r(line_, " \t", &context); word; word = strtok_r(nullptr, " \t", &context)) {
    if (argc == 6) { reply(Result::Invalid); return; } args[argc++] = word;
  }
  if (!argc) return;
  view_ = View::None; uint16_t n = 0, value = 0; uint8_t mask = 0;
  if (!strcmp(args[0], "hello") && argc == 1) {
    resetSession(); io_.println(F("R4850 console ready; help for commands; Ctrl-X resets console")); return;
  }
  if (!strcmp(args[0], "help") && (argc == 1 || (argc == 2 && !strcmp(args[1], "diagnostics")))) {
    startView(argc == 2 ? View::DiagnosticHelp : View::Help); return;
  }
  if ((!strcmp(args[0], "config") || !strcmp(args[0], "groups")) && argc == 1) { startView(View::Config); return; }
  if (!strcmp(args[0], "legacy") && argc == 1) { startView(View::Legacy); return; }
  if (!strcmp(args[0], "discover") && argc == 1) { controller_.scan(); io_.println(F("Read-only address sweep started; diag bus shows discoveries")); return; }
  if (!strcmp(args[0], "diag") && argc >= 2) {
    if (!strcmp(args[1], "bus") && argc == 2) { startView(View::Bus); return; }
    if (!strcmp(args[1], "group") && argc == 3) {
      const int8_t g = controller_.groupIndex(args[2]);
      if (g >= 0) {
        const auto s = controller_.groupStatus(g, now_);
        io_.print(args[2]); io_.print(F(" requested total A=")); io_.print(s.requestedCurrent / 100.0f);
        io_.print(F(" authorized total A=")); io_.println(s.authorizedCurrent / 100.0f);
        io_.print(F("members=")); printMembers(s.members); io_.print(F(" responding=")); printMembers(s.responding);
        io_.print(F(" ready=")); printMembers(s.ready); io_.print(F(" missing=")); printMembers(s.missing); io_.println();
        startView(View::Units, s.members); return;
      }
    }
    if (!strcmp(args[1], "psu") && argc == 3 && integer(args[2], controller_.count(), n) && n) { startView(View::Units, 1U << (n - 1)); return; }
  }
  if ((!strcmp(args[0], "status") || !strcmp(args[0], "telemetry")) && argc <= 2) {
    mask = membersMask(controller_.count());
    if (argc == 2 && strcmp(args[1], "all")) {
      const int8_t g = controller_.groupIndex(args[1]);
      if (g < 0) { reply(Result::Invalid); return; } mask = controller_.configuration().groups[g].members;
    }
    startView(!strcmp(args[0], "telemetry") ? View::Telemetry : View::Units, mask); return;
  }
  if (!strcmp(args[0], "count") && argc == 2 && integer(args[1], PSU_MAX_UNITS, n)) { reply(controller_.setCount(n)); return; }
  if (!strcmp(args[0], "unbind") && argc == 2 && integer(args[1], PSU_MAX_UNITS, n) && n) {
    const auto result = controller_.unbind(n - 1); reply(result);
    if (result == Result::Ok) io_.println(F("Identity released; current authorization removed. This does not switch off the physical output."));
    return;
  }
  if (!strcmp(args[0], "group") && argc >= 3) {
    if (!strcmp(args[1], "set") && argc == 4 && selection(args[3], controller_.count(), mask)) {
      reply(controller_.setGroup(args[2], mask)); io_.println(F("Membership edits do not change live limits; preview/apply new allocation.")); return;
    }
    if (!strcmp(args[1], "delete") && argc == 3) {
      const int8_t g = controller_.groupIndex(args[2]);
      reply(g < 0 ? Result::Invalid : controller_.removeGroup(g));
      io_.println(F("Deleting membership does not switch off physical outputs.")); return;
    }
  }
  if ((!strcmp(args[0], "bind") || !strcmp(args[0], "adopt")) && argc == 4 &&
      integer(args[1], controller_.count(), n) && n && decimal(args[3], value)) {
    Identity id;
    if (!identity(args[2], id)) { reply(Result::Invalid); return; }
    const auto& old = controller_.configuration().units[n - 1].identity;
    if (!strcmp(args[0], "bind") && identified(old) && !sameIdentity(id, old)) {
      io_.println(F("BLOCKED different identity: verify replacement/physical wiring, then adopt <slot> <id> <rated-A>")); return;
    }
    const auto result = controller_.bind(n - 1, id, value, now_); reply(result);
    if (result == Result::Ok) io_.println(F("Binding confirms compatible hardware/rating; apply common voltage before group current. No PSU writes made."));
    else io_.println(F("Binding unchanged; diag bus must show a fresh verified unique identity. Check rating and existing bindings."));
    return;
  }
  if (!strcmp(args[0], "set")) {
    if (argc == 3 && decimal(args[2], value) && (!strcmp(args[1], "voltage") || !strcmp(args[1], "offline-voltage"))) {
      reply(controller_.setVoltage(value, !strcmp(args[1], "offline-voltage"))); return;
    }
    if (argc == 4 && decimal(args[3], value)) {
      if (!strcmp(args[1], "voltage") || !strcmp(args[1], "offline-voltage")) {
        io_.println(F("ERR parallel voltage is global: set voltage <V> (no group)")); return;
      }
      const int8_t g = controller_.groupIndex(args[2]);
      if (g >= 0 && (!strcmp(args[1], "current") || !strcmp(args[1], "offline-current"))) {
        const bool offline = !strcmp(args[1], "offline-current");
        const auto r = controller_.setCurrent(g, value, offline); reply(r);
        if (r == Result::Ok) {
          const auto& group = controller_.configuration().groups[g];
          io_.print(F("STAGED total A=")); io_.print(value / 100.0f); io_.print(F(" across slots ")); printMembers(group.members);
          io_.println(F("; apply sends the previewed per-unit shares"));
        } else {
          const auto check = controller_.checkCurrent(g, value);
          if (check.issue == Issue::Capacity) {
            io_.print(F("PSU ")); io_.print(check.slot + 1); io_.print(F(" share A=")); io_.print(check.share / 100.0f);
            io_.print(F(" exceeds configured maximum A=")); io_.println(check.maximum / 100.0f);
          }
          io_.println(F("No staging change or clamping; inspect config/ratings."));
        }
        return;
      }
    }
  }
  if (!strcmp(args[0], "autoresume") && argc == 2 && (!strcmp(args[1], "on") || !strcmp(args[1], "off"))) {
    reply(controller_.setAutoResume(!strcmp(args[1], "on"))); return;
  }
  if (!strcmp(args[0], "interval") && argc == 2 && integer(args[1], limits::maxPollMs, n)) { reply(controller_.setPollInterval(n)); return; }
  if ((!strcmp(args[0], "apply") || !strcmp(args[0], "preview") || !strcmp(args[0], "offline")) && argc >= 2 && argc <= 3) {
    const bool offline = !strcmp(args[0], "offline");
    const bool partial = argc == 3 && !strcmp(args[2], "partial");
    if ((argc == 3 && !partial) || (offline && strcmp(args[1], "all"))) { reply(Result::Invalid); return; }
    int8_t g = controller_.groupIndex(args[1]);
    Operation op = offline ? Operation::Offline : !strcmp(args[1], "all") ? Operation::All :
        !strcmp(args[1], "voltage") ? Operation::Voltage : Operation::GroupCurrent;
    plan_ = controller_.preview(op, g, now_, partial); confirmation_ = false;
    if (!strcmp(args[0], "preview")) io_.println(F("PREVIEW ONLY; no settings sent"));
    else {
      const auto r = controller_.queue(plan_, false, now_); reply(r);
      if (r == Result::Ok) { io_.println(F("QUEUED; acknowledgements/results determine success")); wasBusy_ = true; }
      if (r == Result::ConfirmRequired) confirmation_ = true;
    }
    startView(View::Plan); return;
  }
  if (!strcmp(args[0], "confirm") && argc == 2) {
    if (!strcmp(args[1], "no")) { confirmation_ = false; io_.println(F("Cancelled; no settings sent")); return; }
    if (!strcmp(args[1], "yes") && confirmation_) {
      confirmation_ = false; const auto r = controller_.queue(plan_, true, now_); reply(r);
      if (r == Result::Ok) wasBusy_ = true;
      return;
    }
    io_.println(F("ERR no valid confirmation pending; apply GROUP partial to preview")); return;
  }
  if (!strcmp(args[0], "poll") && argc == 2) {
    const int8_t g = controller_.groupIndex(args[1]);
    mask = !strcmp(args[1], "all") ? membersMask(controller_.count()) : g >= 0 ? controller_.configuration().groups[g].members : 0;
    reply(controller_.requestPoll(mask, now_)); return;
  }
  if (!strcmp(args[0], "describe") && argc == 2 && integer(args[1], 127, n) && n) {
    const auto r = controller_.requestDescription(n); if (r == Result::Ok) descriptionAddress_ = n; reply(r); return;
  }
  if (!strcmp(args[0], "reset-ah") && argc == 1) { controller_.resetAmpHours(); reply(Result::Ok); return; }
  if ((!strcmp(args[0], "watch") || !strcmp(args[0], "raw") || !strcmp(args[0], "echo")) && argc == 2 &&
      (!strcmp(args[1], "on") || !strcmp(args[1], "off"))) {
    const bool on = !strcmp(args[1], "on");
    if (!strcmp(args[0], "watch")) watch_ = on; else if (!strcmp(args[0], "raw")) raw_ = on; else echo_ = on;
    reply(Result::Ok); return;
  }
  if ((!strcmp(args[0], "save") || !strcmp(args[0], "load") || !strcmp(args[0], "defaults")) && argc == 1) {
    if (controller_.busy()) { reply(Result::Busy); return; }
    if (!strcmp(args[0], "defaults")) { reply(controller_.configure(defaultConfiguration())); return; }
    if (!strcmp(args[0], "save")) storageReply(memory_.save(controller_.configuration()));
    else {
      Configuration c = controller_.configuration(); const auto r = memory_.load(c);
      if (r == StorageResult::Ok || r == StorageResult::Migrated || r == StorageResult::WrongDeployment) reply(controller_.configure(c));
      storageReply(r);
    }
    return;
  }
  io_.println(F("ERR syntax; help. Parallel set syntax: set current GROUP1 55 / set voltage 54"));
}
void SerialConsole::tick(uint32_t now) {
  now_ = now;
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
  if (!wasBusy_ && controller_.busy() && controller_.report().operation == Operation::Restore) {
    beginOutput(); io_.print(F("RESTORE authorized profile; slots=")); printMembers(controller_.report().recipients);
    io_.println(F(" (saved/staged drafts are not applied)"));
    if (view_ == View::None) prompt();
  }
  if (wasBusy_ && !controller_.busy()) {
    beginOutput();
    const auto& r = controller_.report();
    io_.print(F("RESULT accepted slots=")); printMembers(r.succeeded);
    io_.print(F(" failed=")); printMembers(r.failed);
    io_.print(F(" incomplete/skipped=")); printMembers(r.skipped);
    io_.print(F(" operation=")); io_.println(operationName(r.operation));
    startView(View::Units, r.requested);
  }
  wasBusy_ = controller_.busy();
  if (watch_ && !length_ && !discard_ && view_ == View::None && uint32_t(now - lastWatch_) >= 1000) {
    lastWatch_ = now; startView(View::Telemetry, membersMask(controller_.count()));
  }
  if (view_ != View::None && !length_ && !discard_) {
    beginOutput(); outputRow(now);
    if (view_ == View::None) prompt();
  }
}
void SerialConsole::printUnit(uint8_t i, uint32_t now) {
  const auto& c = controller_.configuration();
  const auto* d = controller_.deviceForSlot(i, now);
  io_.print(F("PSU ")); io_.print(i + 1); io_.print(F(" id=")); printIdentity(c.units[i].identity);
  io_.print(F(" addr=")); if (d) io_.print(d->address); else io_.print('-');
  io_.print(' '); io_.println(issueName(controller_.issue(i, now)));
  io_.print(F("  staged A=")); const int8_t g = groupFor(c, i);
  if (g >= 0) io_.print(allocation(c.groups[g], i) / 100.0f); else io_.print('-');
  io_.print(F(" authorized A="));
  if (c.operating.currentMask & (1U << i)) io_.print(c.operating.current[i] / 100.0f); else io_.print('-');
  io_.print(F(" voltage-authorized=")); io_.print(bool(c.operating.voltageMask & (1U << i)));
  const auto& s = controller_.report().units[i];
  io_.print(F(" voltage-acked=")); io_.print(controller_.voltageSynchronized(i));
  io_.print(F(" last=")); io_.print(stateName(s.state)); io_.print(F(" reg=")); io_.println(s.reg);
  if (d) {
    io_.print(F("  response age ms=")); io_.print(uint32_t(now - d->lastSeen));
    io_.print(F(" data=")); if (d->telemetry.dataSeen) io_.print(uint32_t(now - d->telemetry.lastData)); else io_.print('-');
    io_.print(F(" broadcast=")); if (d->telemetry.broadcastSeen) io_.print(uint32_t(now - d->telemetry.lastBroadcast)); else io_.print('-');
    io_.print(F(" alarm-raw="));
    if (d->telemetry.alarmSeen) { io_.print(F("0x")); io_.println(d->telemetry.alarmBits, HEX); }
    else io_.println(F("unknown"));
    io_.print(F("  last measured V="));
    if (d->telemetry.validMask & (1U << protocol::OutputVoltage)) io_.print(d->telemetry.values[protocol::OutputVoltage]); else io_.print('-');
    io_.print(F(" A="));
    if (d->telemetry.validMask & (1U << protocol::OutputCurrent)) io_.print(d->telemetry.values[protocol::OutputCurrent]); else io_.print('-');
    io_.println(F(" (check freshness above)"));
  }
}
void SerialConsole::outputRow(uint32_t now) {
  const auto& c = controller_.configuration();
  if (view_ == View::DiagnosticHelp) {
    switch (row_++) {
      case 0: io_.println(F("BUS / GROUP DIAGNOSTICS (read only; no battery/load needed)")); break;
      case 1: io_.println(F("discover: INFO sweep 1..127; diag bus: responders, identities, current addresses, broadcasts, CAN errors")); break;
      case 2: io_.println(F("diag group NAME / diag psu N: bindings, readiness, ages, authorized shares and last failures")); break;
      case 3: io_.println(F("Ready requires unique verified identity, fresh telemetry AND unsolicited broadcast, PSU ready status.")); break;
      case 4: io_.println(F("poll all/NAME; telemetry all/NAME; watch on/off. CAN ready alone does not prove a PSU is present.")); break;
      case 5: io_.println(F("describe ADDRESS: inspect E-Label/model; raw on/off: brief traffic trace. Verify crystal/wiring/termination for TX errors.")); break;
      case 6: io_.println(F("preview all/NAME: inspect apply blockers; no setting writes. Unknown units require explicit bind/adopt.")); break;
      default: view_ = View::None; break;
    }
  } else if (view_ == View::Help) {
    switch (row_++) {
      case 0: io_.println(F("Parallel: one global voltage; named source groups; current is TOTAL group A.")); break;
      case 1: io_.println(F("count <1..8>; group set <NAME> <1,2 or 1-2>; group delete <NAME>")); break;
      case 2: io_.println(F("discover; diag bus; diag group <NAME>; diag psu <slot>")); break;
      case 3: io_.println(F("bind <slot> <12-hex identity> <rated-A>; adopt replaces; unbind <slot> releases identity")); break;
      case 4: io_.println(F("set voltage <V>; set offline-voltage <V> (global only)")); break;
      case 5: io_.println(F("set current <GROUP> <total-A>; set offline-current <GROUP> <total-A>")); break;
      case 6: io_.println(F("preview <all|voltage|GROUP>; apply <all|voltage|GROUP>")); break;
      case 7: io_.println(F("apply GROUP partial; then confirm yes/no (missing only; no redistribution)")); break;
      case 8: io_.println(F("offline all = explicit PSU nonvolatile defaults; never automatic")); break;
      case 9: io_.println(F("config / groups; status / telemetry [all|GROUP]; watch on/off; raw on/off")); break;
      case 10: io_.println(F("save / load = controller EEPROM; autoresume on/off; defaults = RAM template")); break;
      case 11: io_.println(F("legacy = inspect preserved schema-1 records before migration/save")); break;
      case 12: io_.println(F("poll <all|GROUP>; describe <CAN-address>; interval <1000..10000>; reset-ah")); break;
      case 13: io_.println(F("echo on/off; hello; Ctrl-X/C resets console; Ctrl-U clears line")); break;
      default: view_ = View::None; break;
    }
  } else if (view_ == View::Config) {
    if (row_ == 0) {
      io_.print(F("deployment=")); io_.print(c.deploymentId); io_.print(F(" compiled=")); io_.print(deployment::id);
      io_.print(F(" slots=")); io_.print(c.count); io_.print(F(" autoresume=")); io_.println(c.autoResume);
      io_.print(F("draft global V=")); io_.print(c.voltage / 100.0f); io_.print(F(" offline V=")); io_.print(c.offlineVoltage / 100.0f);
      io_.print(F(" authorized V=")); if (c.operating.voltageAuthorized) io_.println(c.operating.voltage / 100.0f); else io_.println(F("none"));
      io_.print(F("voltage authorized slots=")); printMembers(c.operating.voltageMask); io_.println();
    } else if (row_ <= limits::maxGroups) {
      const auto& g = c.groups[row_ - 1];
      if (g.members) {
        io_.print(g.name); io_.print(F(" slots=")); printMembers(g.members);
        io_.print(F(" total A=")); io_.print(g.current / 100.0f); io_.print(F(" offline total A=")); io_.println(g.offlineCurrent / 100.0f);
      }
    } else if (row_ <= limits::maxGroups + c.count) {
      const uint8_t i = row_ - limits::maxGroups - 1;
      io_.print(F("PSU ")); io_.print(i + 1); io_.print(F(" id=")); printIdentity(c.units[i].identity);
      io_.print(F(" rated A=")); io_.println(c.units[i].ratedCurrent / 100.0f);
    } else view_ = View::None;
    ++row_;
  } else if (view_ == View::Bus) {
    if (row_ == 0) {
      io_.print(F("CAN init=")); io_.print(controller_.ready()); io_.print(F(" bitrate=")); io_.print(board::canBitrate);
      io_.print(F(" crystal=")); io_.print(board::canCrystalHz); io_.print(F(" CS/INT=")); io_.print(board::canChipSelect); io_.print('/'); io_.println(board::canInterrupt);
      io_.print(F("tx-errors=")); io_.print(controller_.txFailures()); io_.print(F(" rx-drops=")); io_.print(controller_.droppedFrames());
      io_.print(F(" discovery-overflow=")); io_.print(controller_.discovery().overflow()); io_.print(F(" ignored=")); io_.print(controller_.discovery().ignored());
      io_.print(F(" scan-next=")); io_.println(controller_.scanning());
      const auto bus = controller_.busStatus(now);
      io_.print(F("configured slots=")); io_.print(bus.configured); io_.print(F(" responding addresses=")); io_.print(bus.responding);
      io_.print(F(" verified identities=")); io_.print(bus.verified); io_.print(F(" broadcasting=")); io_.println(bus.broadcasting);
    } else if (row_ <= Discovery::capacity) {
      const auto& d = controller_.discovery().device(row_ - 1);
      if (d.occupied) {
        io_.print(F("addr=")); io_.print(d.address); io_.print(F(" id=")); printIdentity(d.identity);
        io_.print(F(" verified=")); io_.print(d.verified(now)); io_.print(F(" conflict=")); io_.print(d.conflict);
        io_.print(F(" response-age-ms=")); io_.print(uint32_t(now - d.lastSeen));
        io_.print(F(" broadcast-age-ms=")); if (d.telemetry.broadcastSeen) io_.println(uint32_t(now - d.telemetry.lastBroadcast)); else io_.println(F("none"));
      }
    } else { io_.println(F("Addresses are temporary; count responders, not the highest address. Unbound devices receive no setting writes.")); view_ = View::None; }
    ++row_;
  } else if (view_ == View::Telemetry) {
    constexpr uint8_t rowsPerUnit = protocol::MetricCount + 2;
    while (row_ / rowsPerUnit < c.count && !(viewMask_ & (1U << (row_ / rowsPerUnit)))) row_ += rowsPerUnit;
    const uint8_t slot = row_ / rowsPerUnit, field = row_ % rowsPerUnit;
    if (slot >= c.count) { view_ = View::None; return; }
    if (field == 0) printUnit(slot, now);
    else if (field <= protocol::MetricCount) {
      const auto m = static_cast<protocol::Metric>(field - 1); float value;
      io_.print(F("PSU ")); io_.print(slot + 1); io_.print(' '); io_.print(metricName(m)); io_.print('=');
      if (controller_.metric(slot, m, value, now)) io_.println(m == protocol::Efficiency ? value * 100 : value);
      else io_.println(F("N/A"));
    } else {
      const auto* d = controller_.deviceForSlot(slot, now);
      io_.print(F("PSU ")); io_.print(slot + 1); io_.print(F(" Ah~="));
      if (d) io_.println(d->telemetry.ampHours, 4); else io_.println(F("N/A"));
    }
    ++row_;
  } else if (view_ == View::Units) {
    while (row_ < c.count && !(viewMask_ & (1U << row_))) ++row_;
    if (row_ < c.count) printUnit(row_++, now); else view_ = View::None;
  } else if (view_ == View::Plan) {
    if (row_ == 0) {
      io_.print(F("PLAN ")); io_.print(issueName(plan_.blocker)); io_.print(F(" members=")); printMembers(plan_.members);
      io_.print(F(" recipients=")); printMembers(plan_.recipients); io_.println();
    } else if (row_ <= c.count) {
      const uint8_t i = row_ - 1;
      if (plan_.members & (1U << i)) {
        io_.print(F("PSU ")); io_.print(i + 1); io_.print(F(" V=")); io_.print(plan_.voltage / 100.0f);
        io_.print(F(" share A=")); io_.print(plan_.current[i] / 100.0f); io_.print(' '); io_.println(issueName(plan_.issues[i]));
      }
    } else {
      if (confirmation_) io_.println(F("ARE YOU SURE? Missing outputs unknown; only original shares sent, total request not fully configured. confirm yes/no within 15s."));
      view_ = View::None;
    }
    ++row_;
  } else if (view_ == View::Legacy) {
    LegacyConfiguration old;
    if (!memory_.legacy(old)) { io_.println(F("No valid legacy record")); view_ = View::None; return; }
    if (row_ < old.count) {
      const auto& u = old.units[row_]; io_.print(F("LEGACY PSU ")); io_.print(++row_);
      io_.print(F(" addr=")); io_.print(u.address); io_.print(F(" enabled=")); io_.print(u.enabled);
      io_.print(F(" V/A=")); io_.print(u.voltage / 100.0f); io_.print('/'); io_.print(u.current / 100.0f);
      io_.print(F(" offline V/A=")); io_.print(u.offlineVoltage / 100.0f); io_.print('/'); io_.println(u.offlineCurrent / 100.0f);
    } else { io_.println(F("Export before saving a replacement profile; legacy addresses are not identity bindings.")); view_ = View::None; }
  }
}
void SerialConsole::onFrame(void* context, int8_t index, const CanFrame& f) {
  auto& s = *static_cast<SerialConsole*>(context); auto& out = s.io_;
  if (protocol::isReply(f) && protocol::command(f.id) == protocol::descriptionCommand && protocol::address(f.id) == s.descriptionAddress_) {
    if (!s.descriptionOpen_) s.beginOutput();
    for (uint8_t i = 2; i < 8; ++i) if (f.data[i]) out.write(f.data[i] >= 32 && f.data[i] <= 126 ? f.data[i] : '?');
    s.descriptionOpen_ = true;
    if (!(f.id & 1)) { out.println(); s.descriptionOpen_ = false; s.descriptionAddress_ = 0; if (s.view_ == View::None) s.prompt(); }
  }
  if (index >= 0 && protocol::isReply(f) && protocol::command(f.id) == protocol::setCommand) {
    s.beginOutput(); out.print(F("ACK PSU ")); out.print(index + 1); out.print(F(" reg=")); out.print(f.data[1]);
    const uint32_t raw = protocol::readBigEndian(f.data + 4);
    out.print(f.data[0] == 1 ? F(" accepted raw=") : f.data[0] == 0x21 ? F(" rejected raw=") : F(" unknown-status raw=")); out.print(raw);
    if (f.data[1] <= protocol::Overvoltage) { out.print(F(" V=")); out.print(raw / float(protocol::fixedPointScale)); }
    else if (f.data[1] <= protocol::OfflineCurrent) {
      out.print(F(" A=")); out.print(raw / float(protocol::fixedPointScale) * s.controller_.configuration().units[index].ratedCurrent / 100.0f);
    }
    out.println();
    if (s.view_ == View::None) s.prompt();
  }
  if (s.raw_) {
    s.beginOutput(); out.print(f.id, HEX); out.print(' ');
    for (uint8_t i = 0; i < f.length && i < 8; ++i) { if (f.data[i] < 16) out.print('0'); out.print(f.data[i], HEX); out.print(' '); }
    out.println(); if (s.view_ == View::None) s.prompt();
  }
}
}
#endif
