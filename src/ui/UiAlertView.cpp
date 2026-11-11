#include "UiView.h"
#include "AlertText.h"
#include "../util/FlashString.h"
#include <stdio.h>
namespace psu {
namespace { FlashText flash(const __FlashStringHelper* text) { return {reinterpret_cast<const char*>(text)}; } }
void drawAlerts(UiFrame& f, const UiModel& m, const PsuController& c, uint32_t) {
  char text[27];
  if (m.page() == UiPage::Alerts) {
    f.text(1,0,26,UI_TEXT("ALERTS - select for details"),Accent);
    for (uint8_t i=0; i<3; ++i) {
      const uint8_t rank=m.alertSelection()/3*3+i, source=m.alertSourceAt(rank);
      if (source >= AlertManager::Count) continue;
      const auto a=c.alerts().at(source); const auto color=alertLevel(a.code)==AlertLevel::Error ? Error : Warning;
      PSU_SNPRINTF(text,sizeof(text),"%u %s %s",unsigned(a.id),source<c.count() ? "PSU" : "SYS",a.active ? "ACTIVE" : "CLEARED");
      f.text(2+i*3,0,26,text,color);
      f.outline(2+i*3,0,26,3,rank==m.alertSelection());
      f.wrap(3+i*3,4+i*3,flash(alertCause(a.code)),color);
    }
    if (!c.alerts().count()) f.text(5,0,26,UI_TEXT("No incidents"),Good);
    return;
  }
  const auto a=m.displayedAlert(); const auto current=c.alerts().at(a.source);
  const auto color=alertLevel(a.code)==AlertLevel::Error ? Error : Warning;
  PSU_SNPRINTF(text,sizeof(text),"%s #%u %s",color==Error ? "ERROR" : "WARNING",unsigned(a.id),
      current.id!=a.id ? "CHANGED" : current.active ? "ACTIVE" : "CLEARED");
  f.text(1,0,26,text,color);
  if (a.source<c.count()) {
    const int8_t group=groupFor(c.configuration(),a.source);
    PSU_SNPRINTF(text,sizeof(text),"PSU %u / %.8s",unsigned(a.source+1),group>=0 ? c.configuration().groups[group].name : "unassigned");
    f.text(2,0,26,text,Accent);
  } else f.text(2,0,26,UI_TEXT("SYSTEM / ALL GROUPS"),Accent);
  f.wrap(3,5,flash(alertCause(a.code)),color);
  f.wrap(5,7,flash(alertAction(a.code)));
  f.wrap(7,10,flash(alertRecovery(a.code)),Muted);
  if (a.source==AlertManager::Thermal) {
    const auto t=c.thermalStatus(); char temperature[8];
    formatValue(temperature,sizeof(temperature),t.maximum,"C",0);
    PSU_SNPRINTF(text,sizeof(text),"%.6s %u%% %lus",t.valid || t.maximum>=alerts::warningC ? temperature : "?C",unsigned(t.percent),
        (unsigned long)((alerts::errorMs-t.hotMs+999)/1000));
    f.text(2,0,26,text,Accent);
    uint8_t at=0;
    for (uint8_t i=0;i<c.count();++i) if (t.contributors & (1U << i)) {
      if (at) text[at++]=',';
      text[at++]=char('1'+i);
    }
    if (!at) text[at++]='-';
    text[at]=0;
    f.text(4,0,10,UI_TEXT("Heat PSU: "),Muted); f.text(4,10,16,text,Muted);
    if (a.code==AlertCode::ThermalStop) f.text(9,0,26,t.saveFailed ? UI_TEXT("SAVE FAILED; use serial") :
        t.resuming ? UI_TEXT("Verifying resume save...") : !t.persisted ? UI_TEXT("Saving stop marker...") :
        t.cool ? UI_TEXT("Cool; explicit ON needed") : UI_TEXT("Wait for fresh cool data"),t.saveFailed ? Error : Warning);
  }
  if (m.alertResult()!=Result::Ok) f.text(9,0,26,m.alertResult()==Result::Busy ? UI_TEXT("Busy: wait, then retry") : UI_TEXT("Need fresh cool/zero data"),Warning);
  // The acknowledgement always remains usable, even if retry/resume is blocked.
}
}
