#include "../util/FlashString.h"
#include "UiView.h"
#include "UiText.h"
#include <stdio.h>
namespace psu {
namespace {
FlashText reasonText(AvailabilityReason r) {
  switch (r) {
    case AvailabilityReason::NoCan: return UI_TEXT("NO CAN / OUTPUT UNKNOWN");
    case AvailabilityReason::NoAc: return UI_TEXT("WAIT AC");
    case AvailabilityReason::Reconciling: return UI_TEXT("BOOTING / RECONCILING");
    case AvailabilityReason::Ready: return UI_TEXT("READY");
    case AvailabilityReason::Fault: return UI_TEXT("FAULT - CANNOT WAIVE");
  }
  return UI_TEXT("UNKNOWN");
}
void line(UiFrame& f, uint8_t row, FlashText t, uint8_t color = Text) { f.text(row, 0, 26, t, color); }
}
void drawAvailability(UiFrame& f, const UiModel& m, const PsuController& c, uint32_t now) {
  char text[27];
  const uint8_t members = m.group() < 0 ? 0 : c.configuration().groups[m.group()].members;
  if (m.page() == UiPage::Availability || m.page() == UiPage::AvailabilityUnits) {
    if (m.group() >= 0) f.text(1, 0, 26, c.configuration().groups[m.group()].name, Accent);
    else line(f, 1, UI_TEXT("STARTUP / AVAILABILITY"), Accent);
  }
  if (m.page() == UiPage::Availability) {
    uint8_t ready = 0, absent = 0;
    uint16_t target = 0, active = 0;
    for (uint8_t i = 0; i < c.count(); ++i) if (members & (1U << i)) {
      const auto r = c.availabilityReason(i, now);
      ready += r == AvailabilityReason::Ready;
      absent += r == AvailabilityReason::NoAc || r == AvailabilityReason::NoCan;
      target += c.configuration().operating.current[i];
      if (c.participants() & (1U << i)) active += c.configuration().operating.current[i];
    }
    PSU_SNPRINTF(text, sizeof(text), "Ready %u/%u  Absent %u", ready, population(members), absent);
    f.text(2, 0, 26, text);
    if (!c.ready()) line(f, 3, UI_TEXT("CAN INIT FAILED: RETRY"), Error);
    else if (m.startupFault()) line(f, 3, UI_TEXT("AC CUTOFF FAULT LATCHED"), Error);
    else if (m.startupProbe()) line(f, 3, UI_TEXT("AC INHIBITED: COMMISSION"), Warning);
    else if (!members) line(f, 3, UI_TEXT("Configure groups via serial"), Warning);
    else line(f, 3, c.availabilitySettled(m.group(), now) ? UI_TEXT("Availability settled") :
        c.availabilityUnstable(m.group(), now) ? UI_TEXT("UNSTABLE - inspect units") : UI_TEXT("SETTLING - checks continue"), Warning);
    PSU_SNPRINTF(text, sizeof(text), "Allocated %u.%02u/%u.%02uA", active/100, active%100, target/100, target%100);
    f.text(4, 0, 26, text, Muted);
    line(f, 5, UI_TEXT("Shares never redistributed"), Muted);
  } else if (m.page() == UiPage::AvailabilityUnits) {
    for (uint8_t p = 0; p < 2 && m.unitRank()/2*2+p < m.unitCount(); ++p) {
      const uint8_t slot = m.unitAt(m.unitRank()/2*2+p), row = 2 + p*3;
      const uint8_t style = m.availabilitySelection() == p ? selectedStyle | Accent : Text;
      PSU_SNPRINTF(text, sizeof(text), "[%c] PSU%u   %u.%02uA", (m.availabilitySelected() & (1U << slot)) ? 'x' : ' ',
          unsigned(slot+1), c.configuration().operating.current[slot]/100, c.configuration().operating.current[slot]%100);
      f.text(row, 0, 26, text, style);
      line(f, row+1, reasonText(c.availabilityReason(slot, now)), style);
      if (c.excluded() & (1U << slot)) line(f, row+2, UI_TEXT("EXCLUDED; needs inclusion"), Warning);
    }
  } else if (m.page() == UiPage::AvailabilityConfirm) {
    const auto& p = m.availabilityPlan();
    line(f, 1, UI_TEXT("REVIEW STARTUP DECISION"), Accent);
    line(f, 2, p.action == AvailabilityAction::Accept ? UI_TEXT("Accept selected absences") : p.action == AvailabilityAction::Include ?
        UI_TEXT("Include returned PSUs") : p.action == AvailabilityAction::Require ? UI_TEXT("Require selected PSUs") : UI_TEXT("Power PSUs; enable OFF"));
    uint8_t at = 0; text[at++] = '#';
    for (uint8_t i = 0; i < c.count(); ++i) if (p.selected & (1U << i)) { text[at++] = char('1'+i); text[at++] = ' '; }
    text[at] = 0; f.text(3, 0, 26, text, Accent);
    uint16_t active = 0;
    for (uint8_t i = 0; i < c.count(); ++i) if (p.participants & (1U << i)) active += c.configuration().operating.current[i];
    PSU_SNPRINTF(text, sizeof(text), "Active total %u.%02uA", active/100, active%100); f.text(4, 0, 26, text);
    if (p.blocker != Issue::None) {
      line(f, 5, UI_TEXT("BLOCKED"), Error);
      f.wrap(6, 9, FlashText{reinterpret_cast<const char*>(issueName(p.blocker))}, Warning);
    } else {
      line(f, 5, UI_TEXT("ARE YOU SURE?"), Warning);
      line(f, 6, UI_TEXT("Missing outputs unknown."), Warning);
      line(f, 7, c.enableControl().enabled() ? UI_TEXT("Enable ON: may resume load") : UI_TEXT("Enable OFF: holds at zero"), Warning);
      line(f, 8, UI_TEXT("Drafts remain unapplied."), Muted);
    }
  } else if (m.page() == UiPage::AvailabilityResult) {
    line(f, 1, UI_TEXT("STARTUP DECISION"), Accent);
    line(f, 3, m.working() ? UI_TEXT("Saving; please wait...") : m.availabilityResult() == Result::Ok ? UI_TEXT("Decision accepted") : UI_TEXT("Decision not activated"),
        m.availabilityResult() == Result::Ok ? Text : Warning);
    if (m.startupFault()) line(f, 3, UI_TEXT("AC CUTOFF FAULT LATCHED"), Error);
    if (!m.working()) {
      if (m.storageResult() != StorageResult::Ok) line(f, 4, UI_TEXT("SAVE FAILED / BUSY"), Error);
      else line(f, 4, m.availabilityRemembered() ? UI_TEXT("Reviewed config saved") : UI_TEXT("Session only"), Muted);
      f.wrap(5, 7, FlashText{reinterpret_cast<const char*>(resultName(m.availabilityResult()))}, Warning);
    }
    line(f, 8, UI_TEXT("Return to inspect progress"), Muted);
    if (!m.cutoffFitted()) line(f, 10, UI_TEXT("AC cutoff not fitted"), Muted);
  }
}
}
