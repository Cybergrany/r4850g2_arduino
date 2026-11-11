#include "../util/FlashString.h"
#include "UiLayout.h"
#include "../debug/Simulator.h"
#include <stdio.h>
#include <string.h>
namespace psu {
uint8_t UiLayout::buttons(const UiModel& m, UiButton* b) {
  if (m.settingsPage()) return settingsButtons(m, b);
  uint8_t n = 0;
  auto add = [&](uint8_t r,uint8_t c,uint8_t w,uint8_t h,ActionKind a,FlashText label,int16_t value=0) {
    if (n < capacity) b[n++] = {r,c,w,h,{a,value},label};
  };
  switch (m.page()) {
    case UiPage::Alerts:
      for (uint8_t i=0;i<3;++i) {
        const uint8_t rank = m.alertSelection()/3*3+i;
        if (m.alertSourceAt(rank) < AlertManager::Count) add(2+i*3,0,26,3,ActionKind::OpenAlert,UI_TEXT(""),rank);
      }
      add(12,0,8,3,ActionKind::Previous,UI_TEXT("Prev"));
      add(12,9,8,3,ActionKind::Back,UI_TEXT("Back"));
      add(12,18,8,3,ActionKind::Next,UI_TEXT("Next")); break;
    case UiPage::AlertDetail: {
      const auto link = m.recoveryLink();
      add(12,0,link.kind == RecoveryKind::None ? 26 : 12,3,ActionKind::Acknowledge,UI_TEXT("Acknowledge"));
      if (link.kind != RecoveryKind::None) add(12,14,12,3,ActionKind::Recover,recoveryLabel(link.kind));
      if (m.displayedAlert().code == AlertCode::ThermalStop)
        add(10,0,26,2,ActionKind::Resume,UI_TEXT("Resume after cooling"));
      else if (m.displayedAlert().code == AlertCode::TemperatureSensor)
        add(10,0,26,2,ActionKind::RetrySensors,UI_TEXT("Retry sensors"));
      break;
    }
    case UiPage::RecoveryConfirm:
      add(12,0,12,3,ActionKind::Back,UI_TEXT("Cancel"));
      add(12,14,12,3,ActionKind::RecoveryConfirm,UI_TEXT("Confirm")); break;
    case UiPage::Recovery:
      add(12,0,8,3,ActionKind::Back,UI_TEXT("Back"));
      if (m.availableRecoveryCommand() != RecoveryCommand::None)
        add(12,9,17,3,ActionKind::RecoveryReview,m.availableRecoveryCommand() == RecoveryCommand::Retry ? UI_TEXT("Retry ALL...") :
            m.availableRecoveryCommand() == RecoveryCommand::Save ? UI_TEXT("Save settings...") :
            m.availableRecoveryCommand() == RecoveryCommand::Sensors ? UI_TEXT("Retry sensors...") :
            m.recoveryKind() == RecoveryKind::Temperatures ? UI_TEXT("Resume...") : UI_TEXT("Enable..."));
      else if (m.recoveryKind() == RecoveryKind::Bus || m.recoveryKind() == RecoveryKind::Temperatures) {
        add(12,9,8,3,ActionKind::Previous,UI_TEXT("Prev")); add(12,18,8,3,ActionKind::Next,UI_TEXT("Next"));
      }
      if (m.recoveryKind() == RecoveryKind::Storage || (m.recoveryKind() == RecoveryKind::Temperatures &&
          m.availableRecoveryCommand() != RecoveryCommand::None)) {
        add(10,0,12,2,ActionKind::Previous,UI_TEXT("Prev PSU")); add(10,14,12,2,ActionKind::Next,UI_TEXT("Next PSU"));
      }
      break;
    case UiPage::Detail:
      if (m.recovering()) {
        add(12,0,12,3,ActionKind::Back,UI_TEXT("Back"));
        add(12,14,12,3,ActionKind::RecoveryReview,UI_TEXT("Retry ALL..."));
      } else add(12,0,26,3,ActionKind::Back,UI_TEXT("Back"));
      break;
    case UiPage::Result: {
      const auto link = m.recoveryLink();
      add(12,0,link.kind == RecoveryKind::None ? 26 : 12,3,ActionKind::Back,UI_TEXT("Back"));
      if (link.kind != RecoveryKind::None) add(12,14,12,3,ActionKind::Recover,recoveryLabel(link.kind));
      break;
    }
    case UiPage::Groups:
      add(1,0,26,1,ActionKind::Startup,UI_TEXT(""));
      // Group tiles are buttons too; their content is drawn by UiView.
      for (uint8_t i=0;i<3;++i) {
        const uint8_t rank=m.groupRank()/3*3+i;
        if (rank<m.groupCount()) add(2,i*9,8,10,ActionKind::OpenGroup,UI_TEXT(""),rank);
      }
      add(12,0,8,3,ActionKind::Previous,UI_TEXT("Prev"));
      add(12,9,8,3,ActionKind::Tools,UI_TEXT("Tools"));
      add(12,18,8,3,ActionKind::Next,UI_TEXT("Next")); break;
    case UiPage::Units:
      for (uint8_t i=0;i<2;++i) if (m.unitRank()/2*2+i<m.unitCount()) {
        add(2+i*5,0,26,5,ActionKind::OpenUnit,UI_TEXT(""),m.unitRank()/2*2+i);
      }
      add(12,0,6,3,ActionKind::Back,UI_TEXT("Back"));
      add(12,6,8,3,ActionKind::Configure,UI_TEXT("Config"));
      add(12,14,6,3,ActionKind::Previous,UI_TEXT("Prev"));
      add(12,20,6,3,ActionKind::Next,UI_TEXT("Next")); break;
    case UiPage::Config:
      add(3,0,26,3,ActionKind::EditVoltage,UI_TEXT(""));
      add(7,0,26,3,ActionKind::EditCurrent,UI_TEXT(""));
      add(12,0,12,3,ActionKind::Back,UI_TEXT("Back"));
      if (!m.recovering()) add(12,14,12,3,ActionKind::Tools,UI_TEXT("Tools"));
      break;
    case UiPage::Editor:
      add(5,0,5,4,ActionKind::Adjust,UI_TEXT("-"),-1);
      add(5,21,5,4,ActionKind::Adjust,UI_TEXT("+"),1);
      add(12,0,11,3,ActionKind::Cancel,UI_TEXT("Cancel"));
      add(12,12,14,3,ActionKind::Apply,UI_TEXT("Apply & save")); break;
    case UiPage::Confirm:
      add(12,0,12,3,ActionKind::ConfirmNo,UI_TEXT("No / Cancel"));
      add(12,14,12,3,ActionKind::ConfirmYes,UI_TEXT("Yes, partial")); break;
    case UiPage::Tools:
      add(2,0,13,2,ActionKind::Startup,UI_TEXT("Startup"));
      add(2,14,12,2,ActionKind::Alerts,UI_TEXT("Alerts"));
#if PSU_ENABLE_TOUCH
      add(4,0,26,2,ActionKind::Calibrate,UI_TEXT("Calibrate touch"));
      add(6,0,26,2,ActionKind::TouchDebug,UI_TEXT("Touch diagnostics"));
#endif
#if PSU_UI_DEBUG
      add(8,0,26,2,ActionKind::Scenarios,UI_TEXT("SIM scenarios"));
#endif
      if (m.cutoffFitted()) add(10,0,26,2,ActionKind::HardStop,UI_TEXT("Hard stop: AC cutoff"));
      add(12,0,26,3,ActionKind::Back,UI_TEXT("Back")); break;
    case UiPage::AvailabilityOverview:
      for (uint8_t i=0;i<3;++i) {
        const uint8_t rank=m.groupRank()/3*3+i;
        if (rank<m.groupCount()) add(2,i*9,8,8,ActionKind::AvailabilityGroup,UI_TEXT(""),rank);
      }
      add(10,0,26,2,ActionKind::Controller,UI_TEXT("Controller"));
      add(12,0,8,3,ActionKind::Previous,UI_TEXT("Prev"));
      add(12,9,8,3,ActionKind::Back,UI_TEXT("Back"));
      add(12,18,8,3,ActionKind::Next,UI_TEXT("Next")); break;
    case UiPage::Controller:
      add(2,0,26,2,ActionKind::Stop,UI_TEXT("Stop / zero-current hold"));
      add(4,0,12,2,ActionKind::Enable,UI_TEXT("Enable..."));
      add(4,14,12,2,ActionKind::Diagnostics,UI_TEXT("Diagnostics"));
      if (m.cutoffFitted()) add(6,0,26,2,ActionKind::Probe,UI_TEXT("Commissioning probe..."));
      add(8,0,26,2,ActionKind::Retry,UI_TEXT("Retry ALL PSUs..."));
      if (m.cutoffFitted()) add(10,0,26,2,ActionKind::HardStop,UI_TEXT("Hard stop: AC cutoff"));
      add(12,0,26,3,ActionKind::Back,UI_TEXT("Back")); break;
    case UiPage::Availability:
      add(6,0,26,2,ActionKind::AvailabilityInclude,UI_TEXT("Include"));
      add(8,0,26,2,ActionKind::AvailabilityExclude,UI_TEXT("Exclude"));
      add(10,0,26,2,ActionKind::AvailabilityIndividuals,UI_TEXT("Choose individuals"));
      add(12,0,26,3,ActionKind::Back,UI_TEXT("Back")); break;
    case UiPage::AvailabilityUnits:
      for (uint8_t i=0; i<4 && m.availabilityTop()+i<m.unitCount(); ++i)
        add(2+i*2,0,26,2,ActionKind::AvailabilityToggle,UI_TEXT(""),m.availabilityTop()+i);
      add(10,0,8,2,ActionKind::AvailabilityScroll,UI_TEXT("Up"),-1);
      add(10,18,8,2,ActionKind::AvailabilityScroll,UI_TEXT("Down"),1);
      add(12,0,12,3,ActionKind::Back,UI_TEXT("Cancel"));
      add(12,14,12,3,ActionKind::AvailabilityReview,UI_TEXT("CONFIRM")); break;
    case UiPage::AvailabilityConfirm:
      if (m.availabilityPlan().action != AvailabilityAction::Probe)
        add(9,0,26,2,ActionKind::AvailabilityRemember,m.availabilityPlan().remember ? UI_TEXT("[x] Remember + save config") : UI_TEXT("[ ] Remember + save config"));
      add(12,0,12,3,ActionKind::Back,UI_TEXT("Cancel"));
      if (m.availabilityPlan().blocker == Issue::None) add(12,14,12,3,ActionKind::AvailabilityConfirm,UI_TEXT("Confirm"));
      break;
    case UiPage::AvailabilityResult:
      add(12,0,26,3,ActionKind::Back,UI_TEXT("Back"));
      if (m.cutoffFitted()) add(10,0,26,2,ActionKind::HardStop,UI_TEXT("Hard stop: AC cutoff"));
      break;
    case UiPage::TouchDebug:
      add(8,0,10,3,ActionKind::Adjust,UI_TEXT("Test -"),-1);
      add(8,16,10,3,ActionKind::Adjust,UI_TEXT("Test +"),1);
      add(12,0,26,3,ActionKind::Back,UI_TEXT("Back")); break;
    case UiPage::Scenarios:
      add(8,0,8,3,ActionKind::ScenarioPrevious,UI_TEXT("Prev"));
      add(8,9,8,3,ActionKind::ScenarioStart,UI_TEXT("Run"));
      add(8,18,8,3,ActionKind::ScenarioNext,UI_TEXT("Next"));
      add(12,0,26,3,ActionKind::Back,UI_TEXT("Back")); break;
    default: add(12,0,26,3,ActionKind::Back,UI_TEXT("Back")); break;
  }
  return n;
}
uint8_t UiLayout::hit(const UiModel& m,int16_t x,int16_t y,UiAction& action) {
  UiButton b[capacity]; const auto n=buttons(m,b);
  for (uint8_t i=0;i<n;++i) if (x>=4+b[i].col*12 && x<4+(b[i].col+b[i].width)*12 &&
      y>=b[i].row*16 && y<(b[i].row+b[i].height)*16) { action=b[i].action; return i+1; }
  action={}; return 0;
}
void UiLayout::controls(UiFrame& f,const UiModel& m) {
#if PSU_ENABLE_TOUCH
  for (uint8_t row=12;row<15;++row) f.text(row,0,26,UI_TEXT(""));
#endif
  UiButton b[capacity]; const auto n=buttons(m,b);
  for (uint8_t i=0;i<n;++i) {
#ifdef __AVR__
    if (!pgm_read_byte(b[i].label.data)) continue;
#else
    if (!*b[i].label.data) continue;
#endif
    const bool selected=(m.settingsPage() && m.page()!=UiPage::ProfileName && i==m.settingsSelection()) ||
        (m.page()==UiPage::ProfileName && ((m.nameCursor()==profiles::nameLength && b[i].action.kind==ActionKind::NameDone) || (m.nameCursor()>profiles::nameLength && b[i].action.kind==ActionKind::Back))) ||
        (m.page()==UiPage::Tools && i==m.toolSelection()) ||
        (m.availabilityPage() && i==m.availabilitySelection()) ||
        (m.page()==UiPage::AlertDetail && i==m.alertSelection()) ||
        ((m.page()==UiPage::Recovery || m.page()==UiPage::RecoveryConfirm || m.page()==UiPage::Result ||
          (m.recovering() && m.page()==UiPage::Detail)) && i==m.recoverySelection()) ||
        (m.page()==UiPage::Config && m.field()==UiField::Tools && b[i].action.kind==ActionKind::Tools) ||
        (m.page()==UiPage::Confirm && (b[i].action.kind==ActionKind::ConfirmYes)==m.confirmingYes());
    const uint8_t style=Text;
    f.outline(b[i].row,b[i].col,b[i].width,b[i].height,selected);
    for (uint8_t r=b[i].row;r<b[i].row+b[i].height;++r) f.text(r,b[i].col,b[i].width,UI_TEXT(""),style);
    f.text(b[i].row+b[i].height/2,b[i].col,b[i].width,b[i].label,style);
  }
}
void UiLayout::extras(UiFrame& f,const UiModel& m,const PsuController& c,uint32_t now) {
  if (m.settingsPage()) { settingsExtras(f,m,c,now); return; }
  if (m.page()==UiPage::Editor) {
    const bool volts=m.field()==UiField::Voltage;
    f.text(2,0,26,volts ? UI_TEXT("SELECTED PSU VOLTAGE") : m.scope().kind==ScopeKind::Unit ? UI_TEXT("PSU CURRENT") : UI_TEXT("TOTAL CURRENT PER GROUP"),Accent);
    if (m.scope().kind==ScopeKind::Group) f.text(3,0,26,c.configuration().groups[m.group()].name,Accent);
    char value[12]; formatValue(value,sizeof(value),m.editValue()/100.0f,"",1);
    f.largeNumber(6,value); f.text(8,12,2,volts ? UI_TEXT("V") : UI_TEXT("A"),Muted);
    const auto live=summarize(c,m.settingsMembers(),
        volts ? protocol::OutputVoltage : protocol::OutputCurrent,volts ? Aggregate::Mean : Aggregate::Sum,now);
    f.text(9,0,5,UI_TEXT("Live:"),Muted); formatValue(value,sizeof(value)-live.partial(),live.value,volts ? "V" : "A");
    if (live.partial()) strcat(value,"*");
    if (volts && population(m.settingsMembers()) > 1) f.text(9,6,18,UI_TEXT("See each PSU"),Muted);
    else f.text(9,6,12,live.valid() ? value : "--",live.partial() ? Warning : Text);
    char lo[9],hi[9],range[27]; formatValue(lo,sizeof(lo),m.minimum()/100.0f,""); formatValue(hi,sizeof(hi),m.maximum()/100.0f,"");
    PSU_SNPRINTF(range,sizeof(range),"Range %.7s - %.7s %c",lo,hi,volts ? 'V' : 'A'); f.text(10,0,26,range,Muted);
  } else if (m.page()==UiPage::Tools) {
    f.text(1,0,26,UI_TEXT("TOOLS"),Accent);
    if (!m.cutoffFitted()) f.text(10,0,26,UI_TEXT("AC cutoff not fitted"),Muted);
  }
#if PSU_UI_DEBUG
  else if (m.page()==UiPage::Scenarios) {
    f.text(2,0,26,UI_TEXT("SIMULATION SCENARIO"),Accent);
    const auto i=m.scenarioSelection();
    f.text(5,0,26,i==uint8_t(DebugScenario::Count) ? UI_TEXT("restart: reload RAM save") :
        i>uint8_t(DebugScenario::Count) ? UI_TEXT("reset: erase RAM saves") : scenarioName(DebugScenario(i)),Accent);
    f.outline(5,0,26,1,true);
    f.text(6,0,26,PSU_ENABLE_ENCODER ? UI_TEXT("Wheel:choose Click:run") : UI_TEXT("Choose with Prev / Next"),Muted);
  }
#endif
}
}
