#include "UiView.h"
#include "UiText.h"
#include <math.h>
#include "../config/Deployment.h"
#include "../util/FlashString.h"
namespace psu {
namespace {
void line(UiFrame& f, uint8_t row, FlashText t, uint8_t style = Text) { f.text(row, 0, 26, t, style); }
FlashText flash(const __FlashStringHelper* s) { return {reinterpret_cast<const char*>(s)}; }
void temperature(UiFrame& f, const PsuController& c, uint8_t slot, protocol::Metric metric, uint8_t row, uint32_t now) {
  float value; char text[14];
  formatValue(text, sizeof(text), c.freshMetric(slot, metric, value, now) ? value : NAN, "C", 1);
  f.text(row, 0, 12, metric == protocol::InputTemperature ? UI_TEXT("Input temp") : UI_TEXT("Output temp"));
  f.text(row, 12, 14, text, Accent, true);
}
}
void drawRecovery(UiFrame& f, const UiModel& m, const PsuController& c, uint32_t now) {
  char text[27]; const auto& cfg = c.configuration();
  const uint8_t slot = m.recoverySlot() < c.count() ? m.recoverySlot() : 0;
  const auto* device = c.deviceForSlot(slot, now);
  const auto kind = m.recoveryKind();
  if (m.page() == UiPage::RecoveryConfirm) {
    line(f, 1, UI_TEXT("REVIEW RECOVERY"), Accent);
    if (m.recoveryCommand() == RecoveryCommand::Retry) {
      line(f, 3, UI_TEXT("Reconcile ALL PSUs?"), Warning);
      f.wrap(5, 8, UI_TEXT("All managed units restart checks at zero current. Authorized limits may resume if enable is ON."));
    } else if (m.recoveryCommand() == RecoveryCommand::Save) {
      line(f, 3, UI_TEXT("Save reviewed RAM config?"), Warning);
      f.wrap(5, 8, UI_TEXT("Save drafts, authorized profile, absence permissions and enable preference. Does not apply drafts."));
    } else if (m.recoveryCommand() == RecoveryCommand::Sensors) {
      line(f, 3, UI_TEXT("Retry selected PSU sensors?"), Warning);
      f.wrap(5, 8, UI_TEXT("Requires both temperatures fresh and valid. Only this PSU retries; other shares stay unchanged."));
    } else {
      line(f, 3, UI_TEXT("Enable authorized profile?"), Warning);
      f.wrap(5, 8, UI_TEXT("May restore saved operating limits immediately. Staged edits are not applied. Thermal guards still apply."));
    }
    line(f, 9, c.enableControl().enabled() ? UI_TEXT("Current enable: ON") : UI_TEXT("Current enable: OFF"), Warning);
    line(f, 10, UI_TEXT("Confirm within 15 seconds"), Muted);
  } else if (kind == RecoveryKind::Bus) {
    line(f, 1, UI_TEXT("BUS / CONTROL DIAGNOSTICS"), Accent);
    const auto b = c.busStatus(now);
    PSU_SNPRINTF(text, sizeof(text), "CAN %s  PSU %u/%u", c.ready() ? "OK" : "FAIL", unsigned(slot+1), unsigned(c.count())); f.text(2, 0, 26, text);
    PSU_SNPRINTF(text, sizeof(text), "Seen %u ID %u Bcast %u", b.responding, b.verified, b.broadcasting); f.text(3, 0, 26, text);
    PSU_SNPRINTF(text, sizeof(text), "TX %u RX %u HW %u", c.txFailures(), c.droppedFrames(), c.hardwareOverflows()); f.text(4, 0, 26, text);
    const auto& id = cfg.units[slot].identity;
    PSU_SNPRINTF(text, sizeof(text), "ID %02X%02X%02X%02X%02X%02X", id.bytes[0], id.bytes[1], id.bytes[2], id.bytes[3], id.bytes[4], id.bytes[5]); f.text(5, 0, 26, text);
    if (device) {
      PSU_SNPRINTF(text, sizeof(text), "Addr %u age %lums", device->address, (unsigned long)(now-device->lastSeen)); f.text(6, 0, 26, text);
    } else line(f, 6, UI_TEXT("No verified live address"), Warning);
    PSU_SNPRINTF(text, sizeof(text), "Discovery overflows %u", c.discovery().overflow()); f.text(7, 0, 26, text);
    f.wrap(8, 9, flash(issueName(c.issue(slot, now))), Warning);
    line(f, 10, m.startupFault() ? UI_TEXT("AC cutoff latched LOW") : UI_TEXT("Serial: diag bus/control"), Muted);
    line(f, 11, UI_TEXT("Identity fixes: serial bind"), Muted);
  } else if (kind == RecoveryKind::Temperatures) {
    const auto t = c.thermalStatus();
    PSU_SNPRINTF(text, sizeof(text), "TEMPERATURES PSU %u/%u", unsigned(slot+1), unsigned(c.count())); f.text(1, 0, 26, text, Accent);
    temperature(f, c, slot, protocol::InputTemperature, 3, now);
    temperature(f, c, slot, protocol::OutputTemperature, 4, now);
    PSU_SNPRINTF(text, sizeof(text), "Current factor %u%%", t.percent); f.text(5, 0, 26, text, Warning);
    char base[9], effective[9];
    formatValue(base, sizeof(base), cfg.operating.currentMask & (1U << slot) ? cfg.operating.current[slot]/100.0f : NAN, "A", 2);
    formatValue(effective, sizeof(effective), c.effectiveCurrent(slot)/100.0f, "A", 2);
    PSU_SNPRINTF(text, sizeof(text), "Auth %.7s Limit %.7s", base, effective); f.text(6, 0, 26, text);
    if (t.stop != ThermalStop::None) line(f, 7, UI_TEXT("OFF; cooling continues"));
    else {
      PSU_SNPRINTF(text, sizeof(text), "Hot budget left: %lus", (unsigned long)((t.hotMs >= alerts::errorMs ? 0 : alerts::errorMs-t.hotMs)/1000)); f.text(7, 0, 26, text);
    }
    line(f, 8, t.stop != ThermalStop::None ? UI_TEXT("THERMAL STOP LATCHED") : UI_TEXT("All limits share reduction"), Warning);
    line(f, 9, UI_TEXT("Hot PSUs:"), Muted);
    uint8_t n=0;
    for (uint8_t i=0; i<c.count(); ++i) if (t.contributors & (1U << i)) { text[n++]=char('1'+i); text[n++]=' '; }
    if (!n) text[n++]='-';
    text[n]=0; f.text(9, 10, 16, text, Warning);
    if (m.availableRecoveryCommand() == RecoveryCommand::None) line(f, 10, UI_TEXT("Both sensors must be fresh"), Muted);
  } else if (kind == RecoveryKind::Storage) {
    line(f, 1, UI_TEXT("REVIEW RAM BEFORE SAVE"), Accent);
    PSU_SNPRINTF(text, sizeof(text), "Draft V %u.%02u", cfg.voltage/100, cfg.voltage%100); f.text(2, 0, 26, text);
    if (cfg.operating.voltageAuthorized) {
      PSU_SNPRINTF(text, sizeof(text), "Auth V %u.%02u", cfg.operating.voltage/100, cfg.operating.voltage%100); f.text(3, 0, 26, text);
    } else line(f, 3, UI_TEXT("Voltage not authorized"), Warning);
    PSU_SNPRINTF(text, sizeof(text), "Enable %s Excluded 0x%02X", cfg.autoResume ? "ON" : "OFF", cfg.acceptedAbsent); f.text(4, 0, 26, text);
    const int8_t group = groupFor(cfg, slot);
    PSU_SNPRINTF(text, sizeof(text), "PSU %u / %.8s", unsigned(slot+1), group >= 0 ? cfg.groups[group].name : "no group"); f.text(5, 0, 26, text);
    const auto draft = group >= 0 ? allocation(cfg.groups[group], slot) : 0;
    PSU_SNPRINTF(text, sizeof(text), "Draft A %u.%02u", draft/100, draft%100); f.text(6, 0, 26, text);
    if (cfg.operating.currentMask & (1U << slot)) {
      PSU_SNPRINTF(text, sizeof(text), "Auth A %u.%02u", cfg.operating.current[slot]/100, cfg.operating.current[slot]%100); f.text(7, 0, 26, text);
    } else line(f, 7, UI_TEXT("Current not authorized"), Warning);
    line(f, 8, cfg.thermalStop != ThermalStop::None ? UI_TEXT("Thermal stop persists") : UI_TEXT("Drafts stay unapplied"), Muted);
  } else if (kind == RecoveryKind::Enable) {
    line(f, 1, UI_TEXT("ENABLE CONTROL"), Accent);
    line(f, 3, c.enableControl().enabled() ? UI_TEXT("Enable request is ON") : UI_TEXT("Enable request is OFF"), Warning);
    f.wrap(5, 8, UI_TEXT("Enable restores authorized operating settings after normal checks. Review staged and authorized values via serial config."));
    line(f, 9, UI_TEXT("Latest switch/serial wins"), Muted);
  } else {
    line(f, 1, UI_TEXT("COMMISSIONING / HELP"), Accent);
    if (cfg.deploymentId != deployment::id) {
      line(f, 3, UI_TEXT("Wrong deployment config"), Error);
      line(f, 5, UI_TEXT("Verify firmware/template")); line(f, 6, UI_TEXT("Serial: config / defaults"));
      line(f, 8, UI_TEXT("No automatic migration"), Warning);
    } else if (m.recoveryCause() == AlertCode::Display) {
      line(f, 3, UI_TEXT("Display not responding"), Warning);
      f.wrap(5, 8, UI_TEXT("Check TFT power, CS/DC/reset and SDO wiring. Use serial diag ui and diag bus. Reboot after repair."));
    } else if (m.recoveryCause() == AlertCode::Touch) {
      line(f, 3, UI_TEXT("Touch disabled in build"), Warning);
      line(f, 5, UI_TEXT("Build PSU_ENABLE_TOUCH=1"));
      line(f, 6, UI_TEXT("Serial: touch calibrate"));
    } else {
      line(f, 3, UI_TEXT("Serial commissioning:"), Accent);
      line(f, 4, UI_TEXT("discover / diag bus"));
      line(f, 5, UI_TEXT("bind SLOT ID RATED_AMPS"));
      line(f, 6, UI_TEXT("group set NAME SLOTS"));
      line(f, 7, UI_TEXT("set voltage / set current"));
      line(f, 8, UI_TEXT("preview all / apply all"));
      line(f, 9, UI_TEXT("Verify results, then save"));
#if !PSU_ENABLE_TOUCH
      line(f, 10, UI_TEXT("Touch disabled in build"), Muted);
#endif
    }
  }
}
void drawRecoveryFeedback(UiFrame& f, const UiModel& m, uint8_t row) {
  if (m.recoverySaving()) line(f, row, UI_TEXT("Saving; waiting for verify"), Warning);
  else if (m.recoverySaved()) line(f, row, UI_TEXT("Saved and verified"), Good);
  else if (m.recoveryResult() != Result::Ok) line(f, row,
      m.recoveryResult() == Result::Busy ? UI_TEXT("Busy; wait then retry") :
      m.recoveryResult() == Result::Changed ? UI_TEXT("Changed; review again") :
      m.recoveryResult() == Result::Blocked ? UI_TEXT("Blocked; inspect guards") :
      m.recoveryKind() == RecoveryKind::Storage ? UI_TEXT("Save failed; inspect EEPROM") : UI_TEXT("Failed; inspect diagnostics"), Warning);
}
}
