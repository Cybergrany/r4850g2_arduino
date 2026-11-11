#include "UiLayout.h"
#include "UiText.h"
#include "../util/FlashString.h"
#include <stdio.h>
#include <string.h>
namespace psu {
uint8_t UiLayout::settingsButtons(const UiModel& m, UiButton* b) {
  uint8_t n = 0;
  auto add = [&](uint8_t row, uint8_t col, uint8_t width, uint8_t height, ActionKind action, FlashText label, int16_t value = 0) {
    if (n < capacity) b[n++] = {row, col, width, height, {action, value}, label};
  };
  switch (m.page()) {
    case UiPage::ConfigMenu:
      add(2,0,26,2,ActionKind::SettingsChoice,UI_TEXT("Configure global"),0);
      add(4,0,26,2,ActionKind::SettingsChoice,UI_TEXT("Configure group"),1);
      add(6,0,26,2,ActionKind::SettingsChoice,UI_TEXT("Configure individual"),2);
      add(8,0,26,2,ActionKind::SettingsChoice,UI_TEXT("Load Config"),3);
      add(10,0,26,2,ActionKind::SettingsChoice,UI_TEXT("Save Config"),4);
      add(12,0,26,3,ActionKind::Back,UI_TEXT("Back")); break;
    case UiPage::GroupSelect: case UiPage::UnitSelect: case UiPage::ProfileList:
      for (uint8_t i = 0; i < m.listRows(); ++i) add(2+i*2,0,26,2,ActionKind::SettingsItem,UI_TEXT(""),m.listPage()*4+i);
      add(12,0,8,3,ActionKind::Previous,UI_TEXT("Prev")); add(12,9,8,3,ActionKind::Back,UI_TEXT("Back")); add(12,18,8,3,ActionKind::Next,UI_TEXT("Next")); break;
    case UiPage::ProfileOptions:
      add(12,0,11,3,ActionKind::Back,UI_TEXT("Cancel"));
      add(5,0,26,2,ActionKind::ProfileInclusion,UI_TEXT(""));
      if (m.savingProfile()) add(8,0,26,2,ActionKind::ProfileName,UI_TEXT("Edit name"));
      add(12,12,14,3,ActionKind::ProfileReview,UI_TEXT("Review")); break;
    case UiPage::ProfileName:
      add(12,0,11,3,ActionKind::Back,UI_TEXT("Cancel"));
      add(6,0,8,2,ActionKind::NameCursor,UI_TEXT("< Char"),-1);
      add(6,18,8,2,ActionKind::NameCursor,UI_TEXT("Char >"),1);
      add(9,0,8,2,ActionKind::Adjust,UI_TEXT("-"),-1); add(9,18,8,2,ActionKind::Adjust,UI_TEXT("+"),1);
      add(12,12,14,3,ActionKind::NameDone,UI_TEXT("Done")); break;
    case UiPage::ProfileReview: case UiPage::ScopeConfirm:
      add(12,0,11,3,ActionKind::Back,UI_TEXT("Cancel"));
      add(12,12,14,3,ActionKind::ProfileConfirm,m.page()==UiPage::ScopeConfirm ? UI_TEXT("Apply & save") : m.savingProfile() ? UI_TEXT("Save slot") : UI_TEXT("Load")); break;
    default: break;
  }
  return n;
}
void UiLayout::settingsExtras(UiFrame& f, const UiModel& m, const PsuController& c, uint32_t now) {
  char text[27];
  f.text(1,0,26,m.page()==UiPage::ConfigMenu ? UI_TEXT("CONFIGURATION") : m.page()==UiPage::GroupSelect ? UI_TEXT("CHOOSE GROUP") :
    m.page()==UiPage::UnitSelect ? UI_TEXT("CHOOSE PSU") : m.page()==UiPage::ScopeConfirm ? UI_TEXT("CONFIRM GLOBAL CHANGE") :
    m.page()==UiPage::ProfileName ? UI_TEXT("PROFILE NAME") : m.savingProfile() ? UI_TEXT("SAVE CONFIG PROFILE") : UI_TEXT("LOAD CONFIG PROFILE"),Accent);
  if (m.page()==UiPage::GroupSelect || m.page()==UiPage::UnitSelect || m.page()==UiPage::ProfileList) {
    for (uint8_t i=0;i<m.listRows();++i) {
      const uint8_t rank=m.listPage()*4+i,row=2+i*2;
      f.outline(row,0,26,2,m.settingsSelection()==i);
      if (m.page()==UiPage::GroupSelect) {
        const auto& g=c.configuration().groups[m.groupAt(rank)];
        f.text(row,1,24,g.name,Accent);
        PSU_SNPRINTF(text,sizeof(text),"%u PSUs",unsigned(population(g.members))); f.text(row+1,1,24,text,Muted);
      } else if (m.page()==UiPage::UnitSelect) {
        const auto& id=c.configuration().units[rank].identity; const auto* d=c.deviceForSlot(rank,now);
        if (d) PSU_SNPRINTF(text,sizeof(text),"PSU %u A:%u ...%02X%02X",unsigned(rank+1),unsigned(d->address),id.bytes[4],id.bytes[5]);
        else PSU_SNPRINTF(text,sizeof(text),"PSU %u OFF ...%02X%02X",unsigned(rank+1),id.bytes[4],id.bytes[5]);
        f.text(row,1,24,text,Accent); const auto g=groupFor(c.configuration(),rank);
        f.text(row+1,1,24,g>=0 ? c.configuration().groups[g].name : "No group",Muted);
      } else {
        ProfileInfo info; const auto result=m.profileInfo(rank,info);
        PSU_SNPRINTF(text,sizeof(text),"%u: %.12s",unsigned(rank+1),result==StorageResult::Ok ? info.name : result==StorageResult::NoValidRecord ? "EMPTY" : "INVALID");
        f.text(row,1,24,text,Accent);
        f.text(row+1,1,24,result==StorageResult::Ok && info.hasInclusion ? UI_TEXT("Participation stored") : UI_TEXT("Voltage/current targets"),Muted);
      }
    }
    if (!m.listCount()) f.text(4,0,26,UI_TEXT("No groups; use serial"),Warning);
    PSU_SNPRINTF(text,sizeof(text),"Page %u/%u",unsigned(m.listPage()+1),unsigned(m.listCount() ? (m.listCount()+3)/4 : 1)); f.text(10,0,26,text,Muted);
  } else if (m.page()==UiPage::ProfileName) {
    f.text(3,1,24,m.profileName(),Text);
    if (m.nameCursor()<profiles::nameLength) f.outline(3,1+m.nameCursor(),1,1,true);
    f.text(4,0,26,m.nameEditing() ? UI_TEXT("Turn: character Click:done") : UI_TEXT("Turn: cursor Click:edit"),Muted);
  } else if (m.page()==UiPage::ProfileOptions || m.page()==UiPage::ProfileReview) {
    PSU_SNPRINTF(text,sizeof(text),"%u: %.12s",unsigned(m.profileSlot()+1),m.profileName()); f.text(2,0,26,text,Accent);
    if (m.page()==UiPage::ProfileOptions) {
      f.text(4,0,26,m.savingProfile() ? UI_TEXT("Store PSU participation?") : UI_TEXT("Restore participation?"),Text);
      f.outline(5,0,26,2,m.settingsSelection()==1);
      f.text(6,1,24,!m.savingProfile() && !m.storedInclusion() ? UI_TEXT("Not stored in this slot") : m.profileInclusion() ? UI_TEXT("[X] Yes") : UI_TEXT("[ ] No (targets only)"),Accent);
      if (!m.savingProfile()) f.text(8,0,26,UI_TEXT("Changes require OFF/held"),Muted);
      f.text(10,0,26,m.savingProfile() ? UI_TEXT("Includes unapplied targets") : UI_TEXT("Enable/fault guards remain"),Muted);
    } else {
      const auto& r=m.profileReview();
      if (r.storage!=StorageResult::Ok || r.blocker!=Issue::None) {
        if (r.storage!=StorageResult::Ok) {
          f.wrap(4,8,r.storage==StorageResult::IdentityMismatch ? UI_TEXT("Saved and configured PSU IDs differ. Use profile show and config on serial to compare bindings.") :
              r.storage==StorageResult::WrongDeployment ? UI_TEXT("Profile belongs to another deployment.") :
              r.storage==StorageResult::InvalidConfig ? UI_TEXT("Saved targets exceed current ratings, or participation was not stored.") :
              r.storage==StorageResult::Busy ? UI_TEXT("Storage busy. Wait for the current save, then review again.") : UI_TEXT("Profile unavailable or changed. Select the slot again."),Warning);
        } else f.wrap(4,9,r.blocker==Issue::Disabled ? UI_TEXT("Turn OFF and confirm zero current hold before changing participation.") :
            FlashText{reinterpret_cast<const char*>(issueName(r.blocker))},Warning);
        f.text(10,0,26,UI_TEXT("Resolve issue; review again"),Muted); return;
      }
      f.text(4,0,26,m.savingProfile() ? UI_TEXT("Save targets; no apply") :
          c.enableControl().enabled() ? UI_TEXT("Apply targets, then save") : UI_TEXT("Prepare for next ON"),Text);
      uint16_t low,high,total;
      if (m.profileSummary(low,high,total)) {
        char a[10],b[10]; formatValue(a,sizeof(a),low/100.0f,"",2); formatValue(b,sizeof(b),high/100.0f,"",2);
        PSU_SNPRINTF(text,sizeof(text),"V %.8s - %.8s",a,b); f.text(6,0,26,text);
        formatValue(a,sizeof(a),total/100.0f,"A",2);
        PSU_SNPRINTF(text,sizeof(text),"All %u PSUs: %.9s",unsigned(c.count()),a); f.text(7,0,26,text);
      }
      f.text(8,0,26,m.profileInclusion() ? (m.savingProfile() ? UI_TEXT("Store participation: YES") : UI_TEXT("Restore participation: YES")) : (m.savingProfile() ? UI_TEXT("Targets only; no selection") : UI_TEXT("Participation unchanged")),Accent);
      uint8_t n=0; memcpy(text,"In: ",4); n=4;
      for (uint8_t i=0;i<c.count();++i) if (r.included & (1U<<i)) { text[n++]=char('1'+i); text[n++]=' '; }
      text[n]=0; f.text(9,0,26,text,Muted);
      f.text(10,0,26,UI_TEXT("Confirm within 15 seconds"),Muted);
    }
  } else if (m.page()==UiPage::ScopeConfirm) {
    f.wrap(3,5,UI_TEXT("This will override ALL configured PSUs"),Warning);
    f.text(6,0,26,m.field()==UiField::Voltage ? UI_TEXT("VOLTAGE ONLY") : UI_TEXT("CURRENT TOTAL PER GROUP"),Accent);
    char value[12]; formatValue(value,sizeof(value),m.editValue()/100.0f,m.field()==UiField::Voltage ? "V" : "A",2); f.text(8,0,26,value);
    f.text(10,0,26,c.enableControl().enabled() ? UI_TEXT("Apply now, then save") : UI_TEXT("Prepare for next ON"),Muted);
  }
}
}
