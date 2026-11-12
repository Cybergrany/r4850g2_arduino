#include "../util/FlashString.h"
#include "UiView.h"
#include "UiText.h"
#include "UiLayout.h"
#include <stdio.h>
#include <string.h>
namespace psu {
void drawAlerts(UiFrame&, const UiModel&, const PsuController&, uint32_t);
void drawAvailability(UiFrame&, const UiModel&, const PsuController&, uint32_t);
void drawRecoveryFeedback(UiFrame&, const UiModel&, uint8_t);
void drawRecovery(UiFrame&, const UiModel&, const PsuController&, uint32_t);
namespace {
void line(UiFrame& f, uint8_t row, FlashText s, uint8_t style = Text) { f.text(row, 0, ui::columns, s, style); }
void number(UiFrame& f, uint8_t row, uint8_t col, uint8_t width, float value, const char* unit, uint8_t style = Text, uint8_t decimals = 1) {
  char b[ui::columns + 1]; formatValue(b, width + 1, value, unit, decimals); f.text(row, col, width, b, style, true);
}
void reading(UiFrame& f, uint8_t row, uint8_t col, uint8_t width, Reading r, const char* unit, uint8_t style = Text, uint8_t decimals = 1) {
  if (!r.valid()) { f.text(row, col, width, UI_TEXT("--"), Warning, true); return; }
  char b[ui::columns + 1]; formatValue(b, width + 1 - r.partial(), r.value, unit, decimals);
  if (r.partial()) strcat(b, "*");
  f.text(row, col, width, b, r.partial() ? uint8_t(Warning) : style, true);
}
Reading measurement(const PsuController& c, uint8_t unit, protocol::Metric metric, uint32_t now) {
  return summarize(c, 1U << unit, metric, Aggregate::Sum, now);
}
Issue displayIssue(const PsuController& c, uint8_t slot, uint32_t now) {
  if (!c.ready()) return Issue::Invalid;
  const Issue issue = c.issue(slot, now);
  if (issue != Issue::None) return issue;
  if (c.unitControl(slot).fault != CommandState::Idle) return Issue::ControlFault;
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
    case Issue::ControlFault: return UI_TEXT("CONTROL FAULT");
    case Issue::Disabled: return UI_TEXT("ENABLE IS OFF");
    case Issue::Reconciling: return UI_TEXT("RECONCILING");
    case Issue::NoProfile: return UI_TEXT("NEEDS PROFILE");
    case Issue::FanTestRequiresHold: return UI_TEXT("FAN TEST: ZERO HOLD");
    case Issue::NoAc: return UI_TEXT("WAIT AC");
    case Issue::ApprovalRequired: return UI_TEXT("APPROVAL NEEDED");
    case Issue::Settling: return UI_TEXT("SETTLING");
    case Issue::Cutoff: return UI_TEXT("AC INHIBITED / FAULT");
    case Issue::NoParticipants: return UI_TEXT("NO ACTIVE UNITS");
    case Issue::ReadinessPending: return UI_TEXT("VERIFYING OUTPUT READY");
    case Issue::VoltageUnsynced: return UI_TEXT("APPLY PSU VOLTAGE FIRST");
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
    case CommandState::ReadinessTimeout: return UI_TEXT("OUTPUT NOT READY");
    case CommandState::ReadinessRecoveryLimit: return UI_TEXT("READY RESTART LIMIT");
    default: return UI_TEXT("APPLYING");
  }
}
void status(UiFrame& f, uint8_t row, const PsuController& c, uint8_t slot, uint32_t now, uint8_t background) {
  const Issue issue = displayIssue(c, slot, now);
  const auto state = c.report().units[slot].state;
  if ((c.excluded() & (1U << slot)) && c.availabilityReason(slot, now) != AvailabilityReason::Fault)
    line(f, row, c.participationStatus(slot).legacy ? UI_TEXT("EXCLUDED: REVIEW INTENT") :
        c.participationStatus(slot).remembered ? UI_TEXT("EXCLUDED / REMEMBERED") : UI_TEXT("EXCLUDED / SESSION ONLY"), background | Warning);
  else if (issue != Issue::None) line(f, row, shortIssue(issue), background | Warning);
  else if (commandFailed(state)) line(f, row, shortCommand(state), background | Error);
  else if (commandActive(state)) line(f, row, UI_TEXT("APPLYING"), background | Warning);
  else if (c.readinessStatus(slot, now).pending) line(f, row, UI_TEXT("VERIFYING OUTPUT READY"), background | Warning);
  else line(f, row, UI_TEXT("OK"), background | Good);
}
void header(UiFrame& f, const UiModel& m, const PsuController& c, uint32_t now) {
  const UnitMask mask = membersMask(c.count());
  float lo=0,hi=0,value=0; bool any=false;
  for (uint8_t i=0;i<c.count();++i) if ((mask & (1U<<i)) && c.freshMetric(i,protocol::OutputVoltage,value,now)) {
    if (!any || value<lo) lo=value;
    if (!any || value>hi) hi=value;
    any=true;
  }
  if (any && hi-lo>0.1f) f.text(0,0,6,UI_TEXT("V:MIX"),Warning);
  else reading(f,0,0,6,summarize(c,mask,protocol::OutputVoltage,Aggregate::Mean,now),"V");
  reading(f, 0, 7, 7, summarize(c, mask, protocol::OutputCurrent, Aggregate::Sum, now), "A");
  reading(f, 0, 15, 7, summarize(c, mask, protocol::OutputPower, Aggregate::Sum, now), "W", Text, 0);
  uint8_t page = m.groupRank() / ui::groupsPerPage + 1;
  uint8_t pages = (m.groupCount() + ui::groupsPerPage - 1) / ui::groupsPerPage;
  if (m.page() == UiPage::Units || m.page() == UiPage::Detail) {
    page = m.unitRank() / ui::unitsPerPage + 1;
    pages = (population(c.configuration().groups[m.group()].members) + ui::unitsPerPage - 1) / ui::unitsPerPage;
  }
  char b[8]; PSU_SNPRINTF(b, sizeof(b), "%u/%u", unsigned(pages ? page : 0), unsigned(pages)); f.text(0, 23, 3, b, Muted);
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
    const uint8_t bg = Text;
    f.outline(2, col, 8, PSU_ENABLE_TOUCH ? 10 : 12, g == m.group());
    f.text(2, col, 8, group.name, bg | Accent);
    f.text(3, col, 8, UI_TEXT("LIVE/REQ"), bg | Muted);
    reading(f, 4, col, 8, summarize(c, group.members, protocol::OutputCurrent, Aggregate::Sum, now), "A", bg);
    const auto state = c.groupStatus(g, now);
    if ((c.configuration().operating.currentMask & group.members) == group.members) {
      char b[9]; b[0] = '/'; formatValue(b + 1, 8, state.authorizedCurrent / 100.0f, "A"); f.text(5, col, 8, b, bg | Muted, true);
    } else f.text(5, col, 8, UI_TEXT("/--A"), bg | Warning, true);
    if (!PSU_ENABLE_TOUCH) f.text(6, col, 8, UI_TEXT("MAX TEMP"), bg | Muted);
    reading(f, PSU_ENABLE_TOUCH ? 6 : 7, col, 8, summarize(c, group.members, protocol::OutputTemperature, Aggregate::Maximum, now), "C", bg, 0);
    f.text(PSU_ENABLE_TOUCH ? 7 : 8, col, 8, UI_TEXT("Output:"), bg | Muted);
    reading(f, PSU_ENABLE_TOUCH ? 8 : 9, col, 8, summarize(c, group.members, protocol::OutputPower, Aggregate::Sum, now), "W", bg, 0);
    reading(f, PSU_ENABLE_TOUCH ? 9 : 10, col, 8, sessionTotal(c, group.members), "Ah", bg);
    uint8_t errors = 0, active = 0, verifying = 0;
    for (uint8_t i = 0; i < c.count(); ++i) if (group.members & (1U << i)) {
      const bool waived = (c.excluded() & c.acceptedAbsences() & (1U << i)) &&
          c.availabilityReason(i, now) != AvailabilityReason::Fault;
      if (!waived && (displayIssue(c, i, now) != Issue::None || commandFailed(c.report().units[i].state))) ++errors;
      if (commandActive(c.report().units[i].state)) ++active;
      if (c.unitControl(i).outputDemand == OutputDemand::Starting) ++verifying;
    }
    char b[12];
    if (errors) { PSU_SNPRINTF(b, sizeof(b), "ERR %u/%u", unsigned(errors), unsigned(population(group.members))); f.text(PSU_ENABLE_TOUCH ? 10 : 12, col, 8, b, bg | Error); }
    else if (group.members & c.excluded()) { PSU_SNPRINTF(b, sizeof(b), "ACT %u/%u", unsigned(population(group.members & c.participants())), unsigned(population(group.members))); f.text(PSU_ENABLE_TOUCH ? 10 : 12, col, 8, b, bg | Warning); }
    else f.text(PSU_ENABLE_TOUCH ? 10 : 12, col, 8, active ? UI_TEXT("APPLYING") : verifying ? UI_TEXT("VERIFY") : UI_TEXT("OK"), bg | (active || verifying ? Warning : Good));
    if (pendingSettings(c, group.members)) f.text(PSU_ENABLE_TOUCH ? 11 : 13, col, 8, UI_TEXT("PENDING"), bg | Warning);
  }
  line(f, 14, UI_TEXT("Click:PSUs  Hold:Config"), Muted);
}
void units(UiFrame& f, const UiModel& m, const PsuController& c, uint32_t now) {
  f.text(1, 0, 26, c.configuration().groups[m.group()].name, Accent);
  const uint8_t count = population(c.configuration().groups[m.group()].members);
  const uint8_t first = m.unitRank() / ui::unitsPerPage * ui::unitsPerPage;
  for (uint8_t pos = 0; pos < ui::unitsPerPage && first + pos < count; ++pos) {
    const uint8_t slot = m.unitAt(first + pos), row = 2 + pos * (PSU_ENABLE_TOUCH ? 5 : 6);
    const uint8_t bg = Text;
    f.outline(row, 0, 26, 5, slot == m.unit());
    char b[27]; PSU_SNPRINTF(b, sizeof(b), "PSU%u", unsigned(slot + 1)); f.text(row, 0, 8, b, bg | Accent);
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
    PSU_SNPRINTF(b, sizeof(b), "%s/%s", a, z); f.text(row + 2, 0, 11, b, bg);
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
  char b[27]; PSU_SNPRINTF(b, sizeof(b), "PSU%u  Last CAN addr: %u", unsigned(slot + 1), unsigned(d ? d->address : 0));
  line(f, 1, UI_TEXT("PSU DETAILS"), Accent); f.text(2, 0, 26, b);
  if (!d) f.text(2, 21, 5, UI_TEXT("--"));
  status(f, 4, c, slot, now, 0);
  const Issue issue = displayIssue(c, slot, now); const auto command = c.report().units[slot].state;
  const auto& control = c.unitControl(slot);
  if (control.fault != CommandState::Idle) {
    f.wrap(5, 7, {reinterpret_cast<const char*>(stateName(control.fault))}, Error);
    if (control.fault == CommandState::ReadinessRecoveryLimit)
      PSU_SNPRINTF(b, sizeof(b), "Losses %u; budget %u", unsigned(c.readinessLosses(slot)), unsigned(control::outputReadyRecoveries));
    else PSU_SNPRINTF(b, sizeof(b), "Control reg 0x01%02X", unsigned(control.faultRegister));
    f.text(8, 0, 26, b, Error);
  } else if (issue != Issue::None) f.wrap(5, 8, {reinterpret_cast<const char*>(issueName(issue))}, Warning);
  else if (commandFailed(command)) f.wrap(5, 8, {reinterpret_cast<const char*>(stateName(command))}, Error);
  else if (c.readinessStatus(slot, now).pending) {
    const auto ready = c.readinessStatus(slot, now);
    line(f, 5, ready.acknowledged ? UI_TEXT("Current ACK received") : UI_TEXT("Waiting for current ACK"), Warning);
    PSU_SNPRINTF(b, sizeof(b), "Ready %u/%ums (%u/%u)", unsigned(ready.stableMs), unsigned(control::outputReadyStableMs),
        unsigned(ready.frames), unsigned(control::outputReadyFrames)); f.text(6, 0, 26, b, Warning);
    PSU_SNPRINTF(b, sizeof(b), "Deadline in %ums", unsigned(ready.remainingMs)); f.text(7, 0, 26, b, Warning);
    line(f, 8, UI_TEXT("Output may be live"), Warning);
  }
  else if (control.zeroConfirmed) f.wrap(5, 8, control.awakeConfirmed ?
      UI_TEXT("Awake at zero current for cooling. Zero ACK and fresh low current confirmed. Profile retained.") :
      UI_TEXT("Zero current confirmed. Preparing fan/voltage and verifying awake state. Profile retained."));
  else f.wrap(5, 8, control.phase == ControlPhase::Online ?
      UI_TEXT("Online, ready; no known command fault. Hardware alarms are not fully decoded.") :
      UI_TEXT("Reconciliation in progress. Zero current and awake state are not yet confirmed."));
  f.text(3, 0, 5, UI_TEXT("Fan:"), Muted);
  f.text(3, 5, 8, {reinterpret_cast<const char*>(fanName(control.fanRequested))}, Warning);
  f.text(3, 13, 4, UI_TEXT("ACK:"), Muted);
  f.text(3, 17, 9, {reinterpret_cast<const char*>(fanName(control.fanAcknowledged))}, Muted);
  line(f,9,UI_TEXT("A: authorized / limit / ACK"),Muted);
  char base[7], effective[7], accepted[7]; uint16_t raw;
  formatValue(base,sizeof(base),c.configuration().operating.current[slot]/100.0f,"",1);
  formatValue(effective,sizeof(effective),c.effectiveCurrent(slot)/100.0f,"",1);
  if (c.acknowledgedCurrent(slot,raw)) formatValue(accepted,sizeof(accepted),raw*(c.configuration().units[slot].ratedCurrent/100.0f)/1024.0f,"",1);
  else strcpy(accepted,"?");
  PSU_SNPRINTF(b,sizeof(b),"%.6s / %.6s / %.6s",base,effective,accepted); f.text(10,0,26,b);

  if (d) {
    if (d->telemetry.alarmSeen) { PSU_SNPRINTF(b, sizeof(b), "Raw alarm 0x%08lX", (unsigned long)d->telemetry.alarmBits); f.text(11, 0, 26, b, Muted); }
  } else line(f, 11, UI_TEXT("No verified live address"), Warning);
  if (!m.recovering()) line(f, 14, UI_TEXT("Click:List  Hold:Groups"), Muted);
}
void config(UiFrame& f, const UiModel& m, const PsuController& c, uint32_t now) {
  const auto scope = m.scope(); const UnitMask mask = m.settingsMembers();
  if (scope.kind == ScopeKind::Group) f.text(1,0,8,c.configuration().groups[scope.index].name,Accent);
  else if (scope.kind == ScopeKind::Unit) { char b[12]; PSU_SNPRINTF(b,sizeof(b),"PSU %u",unsigned(scope.index+1)); f.text(1,0,8,b,Accent); }
  else f.text(1,0,8,UI_TEXT("GLOBAL"),Accent);
  f.text(1,9,17,UI_TEXT("CONFIG"),Accent);
  line(f,2,scope.kind==ScopeKind::Global ? UI_TEXT("Overrides ALL PSUs") : UI_TEXT("Selected members only"),Warning);
  line(f,3,scope.kind==ScopeKind::Group ? UI_TEXT("GROUP VOLTAGE") : scope.kind==ScopeKind::Unit ? UI_TEXT("PSU VOLTAGE") : UI_TEXT("GLOBAL VOLTAGE"),Accent);
  line(f,7,scope.kind==ScopeKind::Unit ? UI_TEXT("PSU CURRENT") : scope.kind==ScopeKind::Group ? UI_TEXT("GROUP TOTAL CURRENT") : UI_TEXT("CURRENT TOTAL PER GROUP"),Accent);
  for (uint8_t i=0;i<2;++i) {
    const bool voltage=i==0,chosen=m.field()==(voltage ? UiField::Voltage : UiField::Current);
    const uint8_t row=voltage ? 4 : 8; const auto summary=m.configured(voltage ? SettingFields::Voltage : SettingFields::Current);
    f.outline(row-1,0,26,3,chosen); f.text(row,0,4,UI_TEXT("Set"));
    if (summary.minimum!=summary.maximum && !(chosen && m.editing())) f.text(row,4,8,UI_TEXT("MIXED"),Warning);
    else if (!chosen || !m.editing() || (now/ui::blinkMs)%2==0) number(f,row,4,8,(chosen && m.editing() ? m.editValue() : summary.minimum)/100.0f,voltage ? "V" : "A",Text,2);
    if (!voltage && summary.custom) f.text(row+1,0,26,UI_TEXT("CUSTOM SHARES; edit splits"),Warning);
    else {
      f.text(row+1,0,6,UI_TEXT("Range"),Muted);
      number(f,row+1,6,8,(summary.minimum!=summary.maximum ? summary.minimum : voltage ? limits::minVoltage : 0)/100.0f,"",Muted,2);
      f.text(row+1,14,1,UI_TEXT("-"),Muted);
      number(f,row+1,15,11,(summary.minimum!=summary.maximum ? summary.maximum : c.settingMaximum(scope,voltage ? SettingFields::Voltage : SettingFields::Current))/100.0f,voltage ? "V" : "A",Muted,2);
    }
    f.text(row,13,4,UI_TEXT("Live"),Muted);
    if (voltage && population(mask)>1) f.text(row,17,9,UI_TEXT("Per PSU"),Muted);
    else reading(f,row,17,9,summarize(c,mask,voltage ? protocol::OutputVoltage : protocol::OutputCurrent,Aggregate::Sum,now),voltage ? "V" : "A");
  }
  if (!PSU_ENABLE_TOUCH) { line(f,11,UI_TEXT("TOOLS"),Muted); f.outline(11,0,26,1,m.field()==UiField::Tools); }
  line(f,12,c.enableControl().enabled() ? UI_TEXT("EEPROM saved after ACKs") : UI_TEXT("Apply prepares next ON"),Muted);
  line(f,14,m.editing() ? UI_TEXT("Click:Apply Hold:Cancel") : UI_TEXT("Click:Edit Hold:Back"),Muted);
}
void confirmation(UiFrame& f, const UiModel& m) {
  line(f, 2, UI_TEXT("MISSING MEMBER - BLOCKED"), Error);
  f.wrap(4, 8, UI_TEXT("Missing outputs may still be ON. Apply original shares to responding units only? No redistribution."), Warning);
  char b[27]; uint8_t n = PSU_SNPRINTF(b, sizeof(b), "Missing PSU:");
  for (uint8_t i = 0; i < PSU_MAX_UNITS; ++i) if (m.plan().missing & (1U << i))
    n += PSU_SNPRINTF(b + n, sizeof(b) - n, " %u", unsigned(i + 1));
  f.text(10, 0, 26, b);
  if (!PSU_ENABLE_TOUCH) {
    f.text(12, 0, 12, UI_TEXT("No / Cancel")); f.text(12, 14, 12, UI_TEXT("Yes, partial"));
    f.outline(12, 0, 12, 1, !m.confirmingYes()); f.outline(12, 14, 12, 1, m.confirmingYes());
  }
  line(f, 14, UI_TEXT("Click:Choose Hold:Cancel"), Muted);
}
void result(UiFrame& f, const UiModel& m) {
  FlashText title = UI_TEXT("READY"), body = UI_TEXT(""); uint8_t color = Warning;
  switch (m.notice()) {
    case UiNotice::Changed: title = UI_TEXT("CONFIG / DEVICES CHANGED"); body = UI_TEXT("Edit or confirmation expired. Review current settings before retrying."); break;
    case UiNotice::Busy: title = UI_TEXT("OPERATION BUSY"); body = UI_TEXT("Wait for the current apply or EEPROM save to finish."); break;
    case UiNotice::Blocked: title = UI_TEXT("APPLY BLOCKED"); body = {reinterpret_cast<const char*>(issueName(m.blocker()))}; break;
    case UiNotice::Applying: title = UI_TEXT("APPLYING - WAIT FOR ACKS"); body = UI_TEXT("You may return to groups. The operation continues in the background."); break;
    case UiNotice::Saving: title = m.profileOperation() && m.savingProfile() ? UI_TEXT("SAVING NAMED PROFILE") : UI_TEXT("SAVING EEPROM"); body = UI_TEXT("CAN and serial remain active. Wait for verified completion."); break;
    case UiNotice::Saved: title = UI_TEXT("APPLIED AND SAVED"); body = UI_TEXT("PSUs acknowledged settings. Output measurements show actual delivery."); color = Good; break;
    case UiNotice::PartialSaved: title = UI_TEXT("PARTIAL APPLY SAVED"); body = UI_TEXT("Only responding units accepted their original shares. Missing outputs remain unknown."); break;
    case UiNotice::ApplyFailed: title = UI_TEXT("APPLY FAILED - NOT SAVED"); body = UI_TEXT("Some units may have accepted settings. Inspect PSU details; there is no automatic rollback."); color = Error; break;
    case UiNotice::SaveFailed: title = m.profileOperation() && m.savingProfile() ? UI_TEXT("PROFILE SAVE FAILED") : UI_TEXT("APPLIED / EEPROM FAILED"); body = m.profileOperation() && m.savingProfile() ? UI_TEXT("No settings applied. Inspect the profile slot before retrying its save.") : UI_TEXT("Live settings were accepted, but not saved. Review EEPROM before retrying save."); color = Error; break;
    case UiNotice::SavedOlder: title = UI_TEXT("EARLIER SNAPSHOT SAVED"); body = UI_TEXT("Configuration changed while saving. Current RAM edits remain unsaved; review serial config."); break;
    case UiNotice::ProfileSaved: title = UI_TEXT("PROFILE SAVED"); body = UI_TEXT("Named snapshot verified. Saving a profile does not apply its targets."); color = Good; break;
    case UiNotice::Prepared: title = UI_TEXT("PREPARED FOR NEXT ON"); body = UI_TEXT("Targets authorized and saved. Output remains at zero until an explicit ON request."); color = Good; break;
    case UiNotice::ConfigSaved: title = UI_TEXT("CONFIG SAVED"); body = UI_TEXT("Reviewed RAM snapshot verified. Saving did not apply staged changes."); color = Good; break;
    case UiNotice::Cancelled: title = UI_TEXT("CANCELLED - DRAFT RETAINED"); body = UI_TEXT("No partial apply sent. Review the staged request before applying again."); break;
    default: break;
  }
  if (m.profileOperation() && !m.savingProfile() && m.profileInclusion()) {
    if (m.notice() == UiNotice::SavedOlder) body = UI_TEXT("Snapshot saved; participation NOT ACTIVATED because conditions changed. Review current settings and Config.");
    if (m.notice() == UiNotice::SaveFailed) body = UI_TEXT("Targets prepared/applied. Participation NOT ACTIVATED: save failed. Review config before retrying.");
  }
  line(f, 2, title, color); f.wrap(4, 8, body);
  if (m.notice() == UiNotice::Applying || m.notice() == UiNotice::ApplyFailed || m.notice() == UiNotice::Saving) {
    char b[27];
    PSU_SNPRINTF(b, sizeof(b), "ACK %u/%u  Failed %u", unsigned(population(m.succeeded())), unsigned(population(m.plan().recipients)), unsigned(population(m.failed()))); f.text(10, 0, 26, b, color);
  }
  line(f, 14, m.working() ? UI_TEXT("Hold:Groups (continues)") : UI_TEXT("Click:Config Hold:Groups"), Muted);
}
void controlBanner(UiFrame& f, const UiModel& m, const PsuController& c, uint32_t now) {
  // Recovery pages carry their own control/protection details. Keep their
  // target and review heading intact, including during a thermal latch.
  if (m.page() == UiPage::Recovery || m.page() == UiPage::RecoveryConfirm || m.availabilityPage()) return;
  const auto s = c.controlStatus(now); const auto& e = c.enableControl();
  if (s.activity == Activity::Running && !c.excluded()) return;
  const bool fault = s.activity == Activity::StopUnconfirmed || (s.activity == Activity::StartBlocked && s.blocker != Issue::NoProfile);
  const uint8_t color = fault ? Error : Warning;
  const bool full = m.page() == UiPage::Groups || m.page() == UiPage::Editor || m.page() == UiPage::Confirm || m.page() == UiPage::Result;
  if (c.configuration().thermalStop != ThermalStop::None) {
    f.text(1,full ? 0 : 9,full ? 22 : 14,full ? UI_TEXT("THERMAL STOP: COOL + ON") : UI_TEXT("THERMAL STOP"),Error); return;
  }
  FlashText text = UI_TEXT("RECONCILING...");
  if (s.activity == Activity::Holding) text = e.source() == EnableSource::Switch ? UI_TEXT("ZERO A: ENABLE_IN OFF") :
      e.source() == EnableSource::Serial ? UI_TEXT("ZERO A: SERIAL OFF") : UI_TEXT("ZERO A: BOOT OFF");
  else if (s.activity == Activity::Stopping) text = UI_TEXT("STOPPING...");
  else if (s.activity == Activity::StopUnconfirmed) text = UI_TEXT("STOP UNCONFIRMED");
  else if (s.activity == Activity::Starting) text = UI_TEXT("STARTING / VERIFYING PSU");
  else if (s.activity == Activity::StartBlocked) text = s.blocker == Issue::NoProfile ? UI_TEXT("START: NEEDS PROFILE") : UI_TEXT("START BLOCKED");
  if (s.blocker == Issue::NoParticipants) text = UI_TEXT("NO ACTIVE PSUS");
  if (s.blocker == Issue::Cutoff) text = c.startupStatus().permit == PermitState::Fault ? UI_TEXT("AC CUTOFF: RETRY NEEDED") : UI_TEXT("AC INHIBITED: STARTUP");
  if (full) {
    f.text(1, 0, PSU_UI_DEBUG ? 22 : 26, text, color);
    if (s.activity == Activity::Running) {
      char b[32]; PSU_SNPRINTF(b, sizeof(b), "RUN %u/%u: EXCLUSIONS", unsigned(population(c.participants())), unsigned(c.count()));
      if (c.availabilityAttention(now)) f.text(1, 0, PSU_UI_DEBUG ? 22 : 26, UI_TEXT("PSUS RETURNED: REVIEW"), Warning);
      else f.text(1, 0, PSU_UI_DEBUG ? 22 : 26, b, Warning);
    } else if (fault && s.blocker != Issue::Cutoff) {
      char b[9]; PSU_SNPRINTF(b, sizeof(b), "PSU %u", unsigned(s.slot + 1)); f.text(1, 17, 5, b, color);
    }
  } else if (m.page() == UiPage::Units || m.page() == UiPage::Config || m.page() == UiPage::Detail) {
    if (m.page() == UiPage::Detail) f.text(1, 0, 26, UI_TEXT("PSU INFO"), Accent);
    const FlashText tag = fault ? UI_TEXT("CONTROL FAULT") : !e.enabled() ?
        (e.source() == EnableSource::Switch ? UI_TEXT("OFF: SWITCH") : e.source() == EnableSource::Serial ? UI_TEXT("OFF: SERIAL") : UI_TEXT("OFF: BOOT")) :
        s.blocker == Issue::NoProfile ? UI_TEXT("NEEDS PROFILE") : s.activity == Activity::Starting ? UI_TEXT("STARTING") : UI_TEXT("RECONCILING");
    f.text(1, 9, 14, tag, color);
  }
}
}
void UiView::compose(UiFrame& f, const UiModel& m, const PsuController& c, uint32_t now) {
  f.clear(); header(f, m, c, now);
  if (m.alertPage()) {
    drawAlerts(f,m,c,now); UiLayout::controls(f,m);
#if PSU_UI_DEBUG
    f.text(0,23,3,UI_TEXT("SIM"),Warning);
#endif
    return;
  }
  switch (m.page()) {
    case UiPage::Groups: groups(f, m, c, now); break;
    case UiPage::Units: units(f, m, c, now); break;
    case UiPage::Detail: detail(f, m, c, now); break;
    case UiPage::Config: config(f, m, c, now); break;
    case UiPage::Confirm: confirmation(f, m); break;
    case UiPage::Result: result(f, m); break;
    case UiPage::Recovery: case UiPage::RecoveryConfirm: drawRecovery(f, m, c, now); break;
    case UiPage::AvailabilityOverview: case UiPage::Controller: case UiPage::Availability: case UiPage::AvailabilityUnits: case UiPage::AvailabilityConfirm: case UiPage::AvailabilityResult:
      drawAvailability(f, m, c, now); break;
    default: UiLayout::extras(f,m,c,now); break;
  }
  if (m.recovering() && (m.page()==UiPage::Recovery || m.page()==UiPage::Detail || m.page()==UiPage::Availability))
    drawRecoveryFeedback(f,m,m.page()==UiPage::Detail ? 11 : m.page()==UiPage::Availability ? 5 : 9);
#if PSU_ENABLE_TOUCH
  UiLayout::controls(f,m);
#else
  if (m.settingsPage() || m.page()==UiPage::Tools || m.page()==UiPage::Scenarios || m.availabilityPage() || m.recovering() ||
      (m.page()==UiPage::Result && !m.working() && m.recoveryLink().kind != RecoveryKind::None)) UiLayout::controls(f,m);
#endif
  controlBanner(f, m, c, now);
  // Keep status and V/A/W intact. Alerts replace the Tools label with a badge.
  if (c.alerts().count() && m.page()==UiPage::Groups) {
    char badge[10]; PSU_SNPRINTF(badge,sizeof(badge),"Tools!%u",unsigned(c.alerts().count()));
    #if PSU_ENABLE_TOUCH
    f.text(13,9,8,badge,c.alerts().error() ? Error : Warning);
#else
    PSU_SNPRINTF(badge,sizeof(badge),"!%u",unsigned(c.alerts().count()));
    f.text(14,23,3,badge,c.alerts().error() ? Error : Warning);
#endif
  }

#if PSU_UI_DEBUG
  f.text(0,23,3,UI_TEXT("SIM"),Warning);
  // Preserve pagination alongside the permanent SIM mark.
  if (m.page()==UiPage::Groups || m.page()==UiPage::Units) {
    char page[14]; const uint8_t rank=m.page()==UiPage::Groups ? m.groupRank()/3+1 : m.unitRank()/2+1;
    const uint8_t pages=m.page()==UiPage::Groups ? (m.groupCount()+2)/3 : (m.unitCount()+1)/2;
    PSU_SNPRINTF(page,sizeof(page),"%u/%u",unsigned(rank),unsigned(pages)); f.text(1,23,3,page,Muted);
  }
#endif
  if (m.page() != UiPage::Result && m.working()) line(f, 14,
      m.notice() == UiNotice::Saving ? UI_TEXT("Saving EEPROM...") : UI_TEXT("Applying settings..."), Warning);
}
}
