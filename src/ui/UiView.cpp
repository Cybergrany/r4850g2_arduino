#include "UiView.h"
#include "UiText.h"
#include <stdio.h>
#include <string.h>
namespace psu {
namespace {
void line(UiFrame& f, uint8_t row, FlashText s, uint8_t style = Text) { f.text(row, 0, ui::columns, s, style); }
void number(UiFrame& f, uint8_t row, uint8_t col, uint8_t width, float value, const char* unit, uint8_t style = Text, uint8_t decimals = 1) {
  char b[ui::columns + 1]; formatValue(b, width + 1, value, unit, decimals); f.text(row, col, width, b, style, true);
}
void reading(UiFrame& f, uint8_t row, uint8_t col, uint8_t width, Reading r, const char* unit, uint8_t style = Text, uint8_t decimals = 1) {
  if (!r.valid()) { f.text(row, col, width, UI_TEXT("--"), (style & selectedStyle) | Warning, true); return; }
  char b[ui::columns + 1]; formatValue(b, width + 1 - r.partial(), r.value, unit, decimals);
  if (r.partial()) strcat(b, "*");
  f.text(row, col, width, b, r.partial() ? uint8_t((style & selectedStyle) | Warning) : style, true);
}
Reading measurement(const PsuController& c, uint8_t unit, protocol::Metric metric, uint32_t now) {
  return summarize(c, 1U << unit, metric, Aggregate::Sum, now);
}
Issue displayIssue(const PsuController& c, uint8_t slot, uint32_t now) {
  if (!c.ready()) return Issue::Invalid;
  const Issue issue = c.issue(slot, now);
  if (issue != Issue::None) return issue;
  const protocol::Metric required[] = {protocol::OutputVoltage, protocol::OutputCurrent, protocol::OutputPower};
  for (const auto metric : required) {
    float value; if (!c.freshMetric(slot, metric, value, now)) return Issue::NoTelemetry;
  }
  return Issue::None;
}
FlashText shortIssue(Issue issue) {
  switch (issue) {
    case Issue::None: return UI_TEXT("OK");
    case Issue::Missing: return UI_TEXT("MISSING");
    case Issue::Unbound: return UI_TEXT("UNBOUND");
    case Issue::Unassigned: return UI_TEXT("NO GROUP");
    case Issue::IdentityPending: return UI_TEXT("VERIFY ID");
    case Issue::Conflict: return UI_TEXT("ID CONFLICT");
    case Issue::NoTelemetry: return UI_TEXT("STALE DATA");
    case Issue::NotReady: return UI_TEXT("NOT READY");
    case Issue::VoltageUnsynced: return UI_TEXT("APPLY BUS VOLTAGE FIRST");
    case Issue::DeploymentMismatch: return UI_TEXT("WRONG DEPLOYMENT");
    case Issue::Busy: return UI_TEXT("BUSY");
    case Issue::Capacity: return UI_TEXT("CURRENT LIMIT");
    default: return UI_TEXT("CAN / CONFIG ERROR");
  }
}
FlashText shortCommand(CommandState state) {
  switch (state) {
    case CommandState::Rejected: return UI_TEXT("PSU REJECTED");
    case CommandState::Timeout: return UI_TEXT("ACK TIMEOUT");
    case CommandState::IdentityTimeout: return UI_TEXT("ID TIMEOUT");
    case CommandState::TransportError: return UI_TEXT("CAN TX FAILED");
    case CommandState::Changed: return UI_TEXT("DEVICE CHANGED");
    case CommandState::Incomplete: return UI_TEXT("INCOMPLETE APPLY");
    default: return UI_TEXT("APPLYING");
  }
}
void status(UiFrame& f, uint8_t row, const PsuController& c, uint8_t slot, uint32_t now, uint8_t background) {
  const Issue issue = displayIssue(c, slot, now);
  const auto state = c.report().units[slot].state;
  if (issue != Issue::None) line(f, row, shortIssue(issue), background | Warning);
  else if (commandFailed(state)) line(f, row, shortCommand(state), background | Error);
  else if (commandActive(state)) line(f, row, UI_TEXT("APPLYING"), background | Warning);
  else line(f, row, UI_TEXT("OK"), background | Good);
}
void header(UiFrame& f, const UiModel& m, const PsuController& c, uint32_t now) {
  const uint8_t mask = membersMask(c.count());
  reading(f, 0, 0, 6, summarize(c, mask, protocol::OutputVoltage, Aggregate::Mean, now), "V");
  reading(f, 0, 7, 7, summarize(c, mask, protocol::OutputCurrent, Aggregate::Sum, now), "A");
  reading(f, 0, 15, 7, summarize(c, mask, protocol::OutputPower, Aggregate::Sum, now), "W", Text, 0);
  uint8_t page = m.groupRank() / ui::groupsPerPage + 1;
  uint8_t pages = (m.groupCount() + ui::groupsPerPage - 1) / ui::groupsPerPage;
  if (m.page() == UiPage::Units || m.page() == UiPage::Detail) {
    page = m.unitRank() / ui::unitsPerPage + 1;
    pages = (population(c.configuration().groups[m.group()].members) + ui::unitsPerPage - 1) / ui::unitsPerPage;
  }
  char b[8]; snprintf(b, sizeof(b), "%u/%u", unsigned(pages ? page : 0), unsigned(pages)); f.text(0, 23, 3, b, Muted);
}
void groups(UiFrame& f, const UiModel& m, const PsuController& c, uint32_t now) {
  if (!m.groupCount()) {
    line(f, 3, UI_TEXT("No groups configured"), Warning);
    f.wrap(5, 10, UI_TEXT("Use serial to discover, bind identities and create groups. See the getting-started guide."));
    return;
  }
  const uint8_t first = m.groupRank() / ui::groupsPerPage * ui::groupsPerPage;
  for (uint8_t pos = 0; pos < ui::groupsPerPage; ++pos) {
    const int8_t g = m.groupAt(first + pos); if (g < 0) continue;
    const auto& group = c.configuration().groups[g]; const uint8_t col = pos * 9;
    const uint8_t bg = g == m.group() ? selectedStyle : 0;
    for (uint8_t row = 2; row <= 13; ++row) f.text(row, col, 8, UI_TEXT(""), bg);
    f.text(2, col, 8, group.name, bg | Accent);
    f.text(3, col, 8, UI_TEXT("LIVE/REQ"), bg | Muted);
    reading(f, 4, col, 8, summarize(c, group.members, protocol::OutputCurrent, Aggregate::Sum, now), "A", bg);
    const auto state = c.groupStatus(g, now);
    if ((c.configuration().operating.currentMask & group.members) == group.members) {
      char b[9]; b[0] = '/'; formatValue(b + 1, 8, state.authorizedCurrent / 100.0f, "A"); f.text(5, col, 8, b, bg | Muted, true);
    } else f.text(5, col, 8, UI_TEXT("/--A"), bg | Warning, true);
    f.text(6, col, 8, UI_TEXT("MAX TEMP"), bg | Muted);
    reading(f, 7, col, 8, summarize(c, group.members, protocol::OutputTemperature, Aggregate::Maximum, now), "C", bg, 0);
    reading(f, 8, col, 8, sessionTotal(c, group.members), "Ah", bg);
    f.text(9, col, 8, UI_TEXT("SESSION~"), bg | Muted);
    reading(f, 10, col, 8, summarize(c, group.members, protocol::OutputPower, Aggregate::Sum, now), "W", bg, 0);
    uint8_t errors = 0, active = 0;
    for (uint8_t i = 0; i < c.count(); ++i) if (group.members & (1U << i)) {
      if (displayIssue(c, i, now) != Issue::None || commandFailed(c.report().units[i].state)) ++errors;
      if (commandActive(c.report().units[i].state)) ++active;
    }
    char b[12];
    if (errors) { snprintf(b, sizeof(b), "ERR %u/%u", unsigned(errors), unsigned(population(group.members))); f.text(12, col, 8, b, bg | Error); }
    else f.text(12, col, 8, active ? UI_TEXT("APPLYING") : UI_TEXT("OK"), bg | (active ? Warning : Good));
    if (pendingSettings(c, group.members)) f.text(13, col, 8, UI_TEXT("PENDING"), bg | Warning);
  }
  line(f, 14, UI_TEXT("Click:PSUs  Hold:Config"), Muted);
}
void units(UiFrame& f, const UiModel& m, const PsuController& c, uint32_t now) {
  f.text(1, 0, 26, c.configuration().groups[m.group()].name, Accent);
  const uint8_t count = population(c.configuration().groups[m.group()].members);
  const uint8_t first = m.unitRank() / ui::unitsPerPage * ui::unitsPerPage;
  for (uint8_t pos = 0; pos < ui::unitsPerPage && first + pos < count; ++pos) {
    const uint8_t slot = m.unitAt(first + pos), row = 2 + pos * 6;
    const uint8_t bg = slot == m.unit() ? selectedStyle : 0;
    for (uint8_t r = row; r < row + 5; ++r) line(f, r, UI_TEXT(""), bg);
    char b[27]; snprintf(b, sizeof(b), "PSU%u", unsigned(slot + 1)); f.text(row, 0, 8, b, bg | Accent);
    if (pendingSettings(c, 1U << slot)) f.text(row, 16, 10, UI_TEXT("PENDING"), bg | Warning, true);
    reading(f, row + 1, 0, 6, measurement(c, slot, protocol::OutputVoltage, now), "V", bg);
    reading(f, row + 1, 10, 7, measurement(c, slot, protocol::OutputCurrent, now), "", bg);
    f.text(row + 1, 17, 1, UI_TEXT("/"), bg);
    if (c.configuration().operating.currentMask & (1U << slot)) {
      formatValue(b, 9, c.configuration().operating.current[slot] / 100.0f, "A");
      f.text(row + 1, 18, 8, b, bg);
    } else f.text(row + 1, 18, 8, UI_TEXT("--A"), bg | Warning);
    char a[6], z[6]; const auto in = measurement(c, slot, protocol::InputTemperature, now), out = measurement(c, slot, protocol::OutputTemperature, now);
    if (in.valid()) formatValue(a, sizeof(a), in.value, "C", 0); else strcpy(a, "--C");
    if (out.valid()) formatValue(z, sizeof(z), out.value, "C", 0); else strcpy(z, "--C");
    snprintf(b, sizeof(b), "%s/%s", a, z); f.text(row + 2, 0, 11, b, bg);
    reading(f, row + 2, 11, 8, sessionTotal(c, 1U << slot), "Ah", bg);
    reading(f, row + 2, 19, 7, measurement(c, slot, protocol::OutputPower, now), "W", bg, 0);
    reading(f, row + 3, 0, 6, measurement(c, slot, protocol::InputVoltage, now), "V", bg, 0);
    reading(f, row + 3, 7, 8, measurement(c, slot, protocol::InputFrequency, now), "Hz", bg, 0);
    reading(f, row + 3, 17, 9, measurement(c, slot, protocol::InputCurrent, now), "A", bg);
    status(f, row + 4, c, slot, now, bg);
  }
  line(f, 14, UI_TEXT("Click:Info  Hold:Groups"), Muted);
}
void detail(UiFrame& f, const UiModel& m, const PsuController& c, uint32_t now) {
  const uint8_t slot = m.unit(); const auto* d = c.deviceForSlot(slot, now);
  if (!d) for (uint8_t i = 0; i < Discovery::capacity; ++i) {
    const auto& old = c.discovery().device(i);
    if (old.occupied && sameIdentity(old.identity, c.configuration().units[slot].identity) &&
        (!d || uint32_t(now - old.lastSeen) < uint32_t(now - d->lastSeen))) d = &old;
  }
  char b[27]; snprintf(b, sizeof(b), "PSU%u  Last CAN addr: %u", unsigned(slot + 1), unsigned(d ? d->address : 0));
  line(f, 1, UI_TEXT("PSU DETAILS"), Accent); f.text(2, 0, 26, b);
  if (!d) f.text(2, 21, 5, UI_TEXT("--"));
  status(f, 4, c, slot, now, 0);
  const Issue issue = displayIssue(c, slot, now); const auto command = c.report().units[slot].state;
  if (issue != Issue::None) f.wrap(5, 8, {reinterpret_cast<const char*>(issueName(issue))}, Warning);
  else if (commandFailed(command)) f.wrap(5, 8, {reinterpret_cast<const char*>(stateName(command))}, Error);
  else f.wrap(5, 8, UI_TEXT("Online, ready; no known command fault. Hardware alarms are not fully decoded."));
  if (issue != Issue::None && commandFailed(command)) line(f, 9, shortCommand(command), Error);
  if (d) {
    snprintf(b, sizeof(b), "Last response %lus ago", (unsigned long)(uint32_t(now - d->lastSeen) / 1000)); f.text(10, 0, 26, b, Muted);
    if (d->telemetry.alarmSeen) { snprintf(b, sizeof(b), "Raw alarm 0x%08lX", (unsigned long)d->telemetry.alarmBits); f.text(11, 0, 26, b, Muted); }
  } else line(f, 10, UI_TEXT("No verified live address"), Warning);
  line(f, 14, UI_TEXT("Click:List  Hold:Groups"), Muted);
}
void config(UiFrame& f, const UiModel& m, const PsuController& c, uint32_t now) {
  const auto& cfg = c.configuration(); const uint8_t mask = cfg.groups[m.group()].members;
  f.text(1, 0, 8, cfg.groups[m.group()].name, Accent); f.text(1, 9, 17, UI_TEXT("CONFIG"), Accent);
  line(f, 3, UI_TEXT("BUS VOLTAGE - ALL GROUPS"), m.field() == UiField::Voltage ? selectedStyle | Accent : Accent);
  line(f, 7, UI_TEXT("GROUP TOTAL CURRENT"), m.field() == UiField::Current ? selectedStyle | Accent : Accent);
  for (uint8_t i = 0; i < 2; ++i) {
    const bool voltage = i == 0, chosen = m.field() == (voltage ? UiField::Voltage : UiField::Current);
    const uint8_t row = voltage ? 4 : 8; const uint8_t style = chosen ? selectedStyle : 0;
    f.text(row, 0, 4, UI_TEXT("Set"), style);
    const uint16_t value = chosen && m.editing() ? m.editValue() : voltage ? cfg.voltage : cfg.groups[m.group()].current;
    if (!chosen || !m.editing() || (now / ui::blinkMs) % 2 == 0) number(f, row, 4, 8, value / 100.0f, voltage ? "V" : "A", style, 2);
    else f.text(row, 4, 8, UI_TEXT(""), style);
    f.text(row, 13, 4, UI_TEXT("Live"), Muted);
    reading(f, row, 17, 9, summarize(c, voltage ? membersMask(c.count()) : mask,
        voltage ? protocol::OutputVoltage : protocol::OutputCurrent, voltage ? Aggregate::Mean : Aggregate::Sum, now), voltage ? "V" : "A");
    f.text(row + 1, 0, 6, UI_TEXT("Range"), Muted);
    number(f, row + 1, 6, 8, (voltage ? limits::minVoltage : 0) / 100.0f, voltage ? "V" : "A", Muted, 2);
    f.text(row + 1, 14, 1, UI_TEXT("-"), Muted);
    number(f, row + 1, 15, 11, (voltage ? limits::maxVoltage : c.groupCurrentMaximum(m.group())) / 100.0f, voltage ? "V" : "A", Muted, 2);
  }
  if (pendingSettings(c, mask)) line(f, 11, UI_TEXT("Pending changes / ACKs"), Warning);
  line(f, 12, UI_TEXT("EEPROM saved after ACKs"), Muted);
  line(f, 14, m.editing() ? UI_TEXT("Click:Apply  Hold:Cancel") : UI_TEXT("Click:Edit  Hold:Groups"), Muted);
}
void confirmation(UiFrame& f, const UiModel& m) {
  line(f, 2, UI_TEXT("MISSING MEMBER - BLOCKED"), Error);
  f.wrap(4, 8, UI_TEXT("Missing outputs may still be ON. Apply original shares to responding units only? No redistribution."), Warning);
  char b[27]; uint8_t n = snprintf(b, sizeof(b), "Missing PSU:");
  for (uint8_t i = 0; i < PSU_MAX_UNITS; ++i) if (m.plan().missing & (1U << i))
    n += snprintf(b + n, sizeof(b) - n, " %u", unsigned(i + 1));
  f.text(10, 0, 26, b);
  line(f, 12, m.confirmingYes() ? UI_TEXT("No   > YES, APPLY PARTIAL") : UI_TEXT("> NO   Yes, apply partial"), selectedStyle | Warning);
  line(f, 14, UI_TEXT("Click:Choose Hold:Cancel"), Muted);
}
void result(UiFrame& f, const UiModel& m) {
  FlashText title = UI_TEXT("READY"), body = UI_TEXT(""); uint8_t color = Warning;
  switch (m.notice()) {
    case UiNotice::Changed: title = UI_TEXT("CONFIG / DEVICES CHANGED"); body = UI_TEXT("Edit or confirmation expired. Review current settings before retrying."); break;
    case UiNotice::Busy: title = UI_TEXT("OPERATION BUSY"); body = UI_TEXT("Wait for the current apply or EEPROM save to finish."); break;
    case UiNotice::Blocked: title = UI_TEXT("APPLY BLOCKED"); body = {reinterpret_cast<const char*>(issueName(m.blocker()))}; break;
    case UiNotice::Applying: title = UI_TEXT("APPLYING - WAIT FOR ACKS"); body = UI_TEXT("You may return to groups. The operation continues in the background."); break;
    case UiNotice::Saving: title = UI_TEXT("APPLIED - SAVING EEPROM"); body = UI_TEXT("CAN and serial remain active. Wait for save confirmation."); break;
    case UiNotice::Saved: title = UI_TEXT("APPLIED AND SAVED"); body = UI_TEXT("PSUs acknowledged settings. Output measurements show actual delivery."); color = Good; break;
    case UiNotice::PartialSaved: title = UI_TEXT("PARTIAL APPLY SAVED"); body = UI_TEXT("Only responding units accepted their original shares. Missing outputs remain unknown."); break;
    case UiNotice::ApplyFailed: title = UI_TEXT("APPLY FAILED - NOT SAVED"); body = UI_TEXT("Some units may have accepted settings. Inspect PSU details; there is no automatic rollback."); color = Error; break;
    case UiNotice::SaveFailed: title = UI_TEXT("APPLIED / EEPROM FAILED"); body = UI_TEXT("Live settings were accepted, but not saved. Retry serial save after checking EEPROM."); color = Error; break;
    case UiNotice::SavedOlder: title = UI_TEXT("EARLIER SNAPSHOT SAVED"); body = UI_TEXT("Configuration changed while saving. Current RAM edits remain unsaved; review serial config."); break;
    case UiNotice::Cancelled: title = UI_TEXT("CANCELLED - DRAFT RETAINED"); body = UI_TEXT("No partial apply sent. Review the staged request before applying again."); break;
    default: break;
  }
  line(f, 2, title, color); f.wrap(4, 8, body);
  if (m.notice() == UiNotice::Applying || m.notice() == UiNotice::ApplyFailed || m.notice() == UiNotice::Saving) {
    char b[27];
    snprintf(b, sizeof(b), "ACK %u/%u  Failed %u", unsigned(population(m.succeeded())), unsigned(population(m.plan().recipients)), unsigned(population(m.failed()))); f.text(10, 0, 26, b, color);
  }
  line(f, 14, m.working() ? UI_TEXT("Hold:Groups (continues)") : UI_TEXT("Click:Config Hold:Groups"), Muted);
}
}
void UiView::compose(UiFrame& f, const UiModel& m, const PsuController& c, uint32_t now) {
  f.clear(); header(f, m, c, now);
  switch (m.page()) {
    case UiPage::Groups: groups(f, m, c, now); break;
    case UiPage::Units: units(f, m, c, now); break;
    case UiPage::Detail: detail(f, m, c, now); break;
    case UiPage::Config: config(f, m, c, now); break;
    case UiPage::Confirm: confirmation(f, m); break;
    case UiPage::Result: result(f, m); break;
  }
  if (m.page() != UiPage::Result && m.working()) line(f, 14,
      m.notice() == UiNotice::Saving ? UI_TEXT("Saving EEPROM...") : UI_TEXT("Applying settings..."), Warning);
}
}
