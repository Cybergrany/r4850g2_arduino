#include "../util/FlashString.h"
#include "UiView.h"
#include "UiText.h"
#include <stdio.h>
namespace psu {
namespace {
FlashText reasonText(AvailabilityReason r) {
  switch (r) {
    case AvailabilityReason::NoCan: return UI_TEXT("ABSENT / NO CAN");
    case AvailabilityReason::NoAc: return UI_TEXT("ABSENT / NO AC");
    case AvailabilityReason::Reconciling: return UI_TEXT("RECONCILING");
    case AvailabilityReason::Ready: return UI_TEXT("READY");
    case AvailabilityReason::Fault: return UI_TEXT("FAULT: REVIEW DETAILS");
  }
  return UI_TEXT("UNKNOWN");
}
void line(UiFrame& f, uint8_t row, FlashText t, uint8_t color = Text) { f.text(row, 0, 26, t, color); }
void members(UiFrame& f, uint8_t row, UnitMask mask) {
  char text[20]; uint8_t n=0;
  for (uint8_t i=0; i<PSU_MAX_UNITS; ++i) if (mask & (1U << i)) { text[n++]=char('1'+i); text[n++]=' '; }
  if (!n) text[n++]='-';
  text[n]=0; f.text(row,8,18,text,Accent);
}
void availabilityState(UiFrame& f, uint8_t row, uint8_t col, uint8_t width, AvailabilityState state) {
  if (state == AvailabilityState::Review) {
    f.text(row,col,width,UI_TEXT("Config"),Error); f.text(row+1,col,width,UI_TEXT("Required"),Error);
  } else f.text(row,col,width,state == AvailabilityState::Settled ? UI_TEXT("Settled") : UI_TEXT("Settling"),
      state == AvailabilityState::Settled ? Good : Warning);
}
}
void drawAvailability(UiFrame& f, const UiModel& m, const PsuController& c, uint32_t now) {
  char text[27];
  if (m.page() == UiPage::AvailabilityOverview) {
    line(f,1,UI_TEXT("PSU PARTICIPATION"),Accent);
    if (!m.groupCount()) {
      line(f,3,UI_TEXT("Config Required"),Error);
      f.wrap(5,8,UI_TEXT("Use serial: discover, bind, group set. Controller opens startup controls."));
    }
    for (uint8_t p=0; p<3; ++p) {
      const int8_t g=m.groupAt(m.groupRank()/3*3+p); if (g<0) continue;
      const auto state=c.availabilityStatus(g,now); const uint8_t col=p*9;
      f.outline(2,col,8,8,g==m.group() && m.availabilityFocus()<m.groupCount());
      f.text(2,col,8,c.configuration().groups[g].name,Accent);
      PSU_SNPRINTF(text,sizeof(text),"Ready: %u",state.ready); f.text(4,col,8,text);
      PSU_SNPRINTF(text,sizeof(text),"Absent:%u",state.absent); f.text(5,col,8,text);
      availabilityState(f,7,col,8,state.state);
    }
  } else if (m.page() == UiPage::Controller) {
    line(f,1,c.enableControl().enabled() ? UI_TEXT("CONTROLLER / ENABLE ON") : UI_TEXT("CONTROLLER / ENABLE OFF"),Accent);
    if (!m.cutoffFitted()) line(f,7,UI_TEXT("AC cutoff not fitted"),Muted);
    if (!m.cutoffFitted()) line(f,11,UI_TEXT("Cooling stays active"),Muted);
  } else if (m.page() == UiPage::Availability) {
    if (m.group() >= 0) f.text(1,0,26,c.configuration().groups[m.group()].name,Accent);
    const auto state=c.availabilityStatus(m.group(),now);
    PSU_SNPRINTF(text,sizeof(text),"Ready: %u   Absent: %u",state.ready,state.absent); f.text(2,0,26,text);
    availabilityState(f,3,0,12,state.state);
    line(f,5,UI_TEXT("Changes keep current shares"),Muted);
  } else if (m.page() == UiPage::AvailabilityUnits) {
    line(f,1,UI_TEXT("[x] EXCLUDE  [ ] INCLUDE"),Accent);
    for (uint8_t p=0; p<4 && m.availabilityTop()+p<m.unitCount(); ++p) {
      const uint8_t rank=m.availabilityTop()+p, slot=m.unitAt(rank), row=2+p*2;
      f.outline(row,0,26,2,m.availabilityFocus()==rank);
      PSU_SNPRINTF(text,sizeof(text),"[%c] PSU %u",(m.availabilitySelected() & (1U << slot)) ? 'x' : ' ',unsigned(slot+1));
      f.text(row,0,11,text);
      const auto participation=c.participationStatus(slot);
      const bool edited=((m.availabilitySelected() ^ c.excluded()) & (1U << slot)) != 0;
      f.text(row,11,15,edited ? UI_TEXT("DRAFT") : participation.legacy ? UI_TEXT("LEGACY PERMIT") :
          participation.remembered ? UI_TEXT("REMEMBERED") : UI_TEXT("SESSION ONLY"),Muted,true);
      line(f,row+1,reasonText(c.availabilityReason(slot,now)),c.availabilityReason(slot,now)==AvailabilityReason::Fault ? Error : Muted);
    }
    const uint8_t end=m.availabilityTop()+4 < m.unitCount() ? m.availabilityTop()+4 : m.unitCount();
    PSU_SNPRINTF(text,sizeof(text),"%u-%u/%u",unsigned(m.availabilityTop()+1),end,m.unitCount()); f.text(11,9,8,text,Muted);
  } else if (m.page() == UiPage::AvailabilityConfirm) {
    const auto& p=m.availabilityPlan(); const bool probe=p.action==AvailabilityAction::Probe;
    line(f,1,probe ? UI_TEXT("REVIEW COMMISSIONING") : UI_TEXT("REVIEW PARTICIPATION"),Accent);
    if (probe) {
      f.wrap(2,4,UI_TEXT("Power PSUs for discovery. Charging enable will be OFF."),Warning);
    } else {
      f.text(2,0,8,UI_TEXT("Include:")); members(f,2,p.selected & p.participants);
      f.text(3,0,8,UI_TEXT("Exclude:")); members(f,3,p.selected & UnitMask(~p.participants));
      line(f,4,c.enableControl().enabled() ? UI_TEXT("ON / shares unchanged") : UI_TEXT("Enable OFF / zero hold"),Muted);
    }
    if (p.blocker != Issue::None) {
      line(f,5,UI_TEXT("BLOCKED"),Error);
      if (p.blocker==Issue::ParticipationHold) f.wrap(6,8,UI_TEXT("Stop via Controller first. Wait for confirmed zero current."),Warning);
      else f.wrap(6,8,FlashText{reinterpret_cast<const char*>(issueName(p.blocker))},Warning);
    } else {
      line(f,5,UI_TEXT("ARE YOU SURE?"),Warning);
      if (c.thermalStatus().contributors & p.selected & UnitMask(~p.participants))
        f.wrap(6,8,UI_TEXT("If hot PSU is missing: releases thermal hold. Output unknown."),Warning);
      else if (!probe) f.wrap(6,8,UI_TEXT("Absent included PSUs wait. Excluded until changed. No V/A apply."),Muted);
    }
  } else if (m.page() == UiPage::AvailabilityResult) {
    line(f,1,UI_TEXT("PARTICIPATION RESULT"),Accent);
    if (m.working()) line(f,3,UI_TEXT("Saving; please wait..."));
    else if (m.availabilityResult()==Result::Ok) {
      line(f,3,UI_TEXT("ACTIVE"),Good);
      line(f,4,m.availabilityRemembered() ? UI_TEXT("SAVED AND VERIFIED") : UI_TEXT("SESSION ONLY"),Accent);
    } else {
      line(f,3,UI_TEXT("NOT ACTIVATED"),Error);
      if (m.storageResult()!=StorageResult::Ok) line(f,4,UI_TEXT("SAVE FAILED / BUSY"),Error);
      else if (m.availabilityRemembered()) f.wrap(4,6,UI_TEXT("Saved snapshot not activated: conditions changed."),Warning);
      else f.wrap(4,6,FlashText{reinterpret_cast<const char*>(resultName(m.availabilityResult()))},Warning);
    }
    if (m.startupFault()) line(f,7,UI_TEXT("AC CUTOFF FAULT LATCHED"),Error);
    line(f,8,UI_TEXT("Return to inspect progress"),Muted);
    if (!m.cutoffFitted()) line(f,10,UI_TEXT("AC cutoff not fitted"),Muted);
  }
}
}
