#include "UiModel.h"
#include "UiLayout.h"
#include <string.h>
namespace psu {
namespace {
uint8_t moved(uint8_t n, int16_t delta, uint8_t count) { return count ? (int32_t(n) + delta % count + count) % count : 0; }
char nameCharacter(uint8_t n) { return !n ? ' ' : n <= 26 ? 'A' + n - 1 : n <= 36 ? '0' + n - 27 : n == 37 ? '-' : '_'; }
uint8_t nameIndex(char c) { for (uint8_t i = 0; i < 39; ++i) if (nameCharacter(i) == c) return i; return 0; }
}
bool UiModel::settingsPage() const {
  switch (page()) {
    case UiPage::ConfigMenu: case UiPage::GroupSelect: case UiPage::UnitSelect: case UiPage::ProfileList:
    case UiPage::ProfileOptions: case UiPage::ProfileName: case UiPage::ProfileReview: case UiPage::ScopeConfirm: return true;
    default: return false;
  }
}
uint8_t UiModel::listCount() const { return page_ == UiPage::GroupSelect ? groupCount() : page_ == UiPage::UnitSelect ? c_.count() : profiles::count; }
uint8_t UiModel::listRows() const {
  const uint8_t start = listPage_ * 4, count = listCount(); return start >= count ? 0 : count - start > 4 ? 4 : count - start;
}
bool UiModel::profileSummary(uint16_t& minV, uint16_t& maxV, uint16_t& totalA) const {
  uint16_t volts[PSU_MAX_UNITS], amps[PSU_MAX_UNITS]; uint8_t mismatch;
  const auto& c = c_.configuration();
  if (!saveProfileChoice_ && memory_.profileTargets(profileReview_.slot, profileReview_.generation, c, volts, amps, mismatch) != StorageResult::Ok) return false;
  minV = UINT16_MAX; maxV = totalA = 0;
  for (uint8_t i=0;i<c.count;++i) {
    const uint16_t v=saveProfileChoice_ ? c.voltage[i] : volts[i];
    if (v < minV) minV=v;
    if (v > maxV) maxV=v;
    totalA += saveProfileChoice_ ? c.current[i] : amps[i];
  }
  return true;
}
bool UiModel::storedInclusion() const { ProfileInfo info; return memory_.profileInfo(profileReview_.slot, info) == StorageResult::Ok && info.hasInclusion; }
void UiModel::openSettings(SettingScope scope) {
  scope_ = scope; profileFlow_ = false; page_ = UiPage::Config; field_ = UiField::Current; editing_ = false; notice_ = UiNotice::None;
  if (scope.kind == ScopeKind::Group) group_ = scope.index;
  if (scope.kind == ScopeKind::Unit) { unit_ = scope.index; group_ = groupFor(c_.configuration(), unit_); }
}
void UiModel::chooseProfile(uint8_t slot, uint32_t now) {
  profileReview_ = {}; profileReview_.slot = slot; profileReview_.created = now;
  profileReview_.revision = c_.configurationRevision(); profileReview_.topology = c_.discovery().revision();
  profileReview_.availability = c_.availabilityRevision(); profileReview_.included = c_.participants();
  ProfileInfo info; storageResult_ = memory_.profileInfo(slot, info);
  memset(profileName_, 0, sizeof(profileName_));
  if (storageResult_ == StorageResult::Ok) { memcpy(profileName_, info.name, sizeof(profileName_)); profileReview_.generation = info.generation; }
  else if (storageResult_ == StorageResult::NoValidRecord && saveProfileChoice_) { memcpy(profileName_, "PROFILE ", 8); profileName_[8] = '1' + slot; }
  else { profileFlow_ = true; result(UiNotice::Blocked, Issue::Invalid); return; }
  page_ = UiPage::ProfileOptions; settingsSelection_ = 0; notice_ = UiNotice::None;
}
void UiModel::reviewProfile(uint32_t now) {
  const auto slot = profileReview_.slot; const bool inclusion = profileReview_.restoreInclusion;
  if (saveProfileChoice_) {
    profileReview_.created = now; profileReview_.revision = c_.configurationRevision();
    profileReview_.topology = c_.discovery().revision(); profileReview_.availability = c_.availabilityRevision();
    profileReview_.included = c_.participants(); profileReview_.blocker = Issue::None; profileReview_.storage = StorageResult::Ok;
    for (uint8_t i = 0; i < c_.count(); ++i) if (!identified(c_.configuration().units[i].identity)) profileReview_.blocker = Issue::Unbound;
  } else profileReview_ = c_.previewProfile(memory_, slot, inclusion, now);
  page_ = UiPage::ProfileReview; settingsSelection_ = 0;
}
void UiModel::confirmProfile(uint32_t now) {
  if (working() || c_.busy() || memory_.saving()) { result(UiNotice::Busy); return; }
  const auto& r = profileReview_;
  if (r.revision != c_.configurationRevision() || r.topology != c_.discovery().revision() ||
      r.availability != c_.availabilityRevision() || uint32_t(now - r.created) > limits::confirmationMs) { result(UiNotice::Changed); return; }
  if (r.blocker != Issue::None || r.storage != StorageResult::Ok) { storageResult_ = r.storage; result(UiNotice::Blocked, r.blocker); return; }
  profileFlow_ = true;
  if (saveProfileChoice_) {
    storageResult_ = memory_.startProfileSave(r.slot, profileName_, c_.configuration(), r.generation, r.restoreInclusion, c_.participants());
    if (storageResult_ != StorageResult::Ok) { result(UiNotice::SaveFailed); return; }
    saveToken_ = memory_.saveToken(); operationRevision_ = c_.configurationRevision(); plan_ = {};
    result(UiNotice::Saving);
  } else {
    const auto status = c_.loadProfile(memory_, r, now);
    if (status != Result::Ok) { result(status == Result::Changed ? UiNotice::Changed : UiNotice::Blocked, r.blocker); return; }
    profileLoading_ = true; operationSequence_ = c_.profileLoad().operation; result(UiNotice::Applying);
  }
}
void UiModel::settingsTick(uint32_t now) {
  if (profileLoading_) {
    const auto& p = c_.profileLoad();
    if (p.operation != operationSequence_) { profileLoading_ = false; result(UiNotice::Changed); return; }
    if (p.state == ProfileLoadState::Failed || p.state == ProfileLoadState::Complete) {
      profileLoading_ = false; storageResult_ = p.storage;
      result(p.state == ProfileLoadState::Failed ? (p.storage == StorageResult::Changed ? UiNotice::Changed : p.storage == StorageResult::WriteFailed || p.storage == StorageResult::TooSmall ? UiNotice::SaveFailed : UiNotice::ApplyFailed) : p.older ? UiNotice::SavedOlder : p.prepared ? UiNotice::Prepared : UiNotice::Saved);
    }
    return;
  }
  if (page_ == UiPage::ScopeConfirm && (editRevision_ != c_.configurationRevision() ||
      profileReview_.topology != c_.discovery().revision() || uint32_t(now - profileReview_.created) > limits::confirmationMs)) result(UiNotice::Changed);
}
void UiModel::settingsInput(UiInput event, uint32_t now) {
  if (event.held) { action({ActionKind::Back}, now); return; }
  if (page_ == UiPage::ProfileName) {
    if (event.delta) {
      if (nameEditing_ && nameCursor_ < profiles::nameLength) action({ActionKind::Adjust, event.delta}, now);
      else nameCursor_ = moved(nameCursor_, event.delta, profiles::nameLength + 2);
    }
    if (event.clicked) {
      if (nameCursor_ == profiles::nameLength) action({ActionKind::NameDone}, now);
      else if (nameCursor_ > profiles::nameLength) action({ActionKind::Back}, now);
      else nameEditing_ = !nameEditing_;
    }
    return;
  }
  UiButton buttons[UiLayout::capacity]; const auto count = UiLayout::buttons(*this, buttons);
  if (event.delta) settingsSelection_ = moved(settingsSelection_, event.delta, count);
  if (count && event.clicked) action(buttons[settingsSelection_ % count].action, now);
}
bool UiModel::settingsAction(UiAction a, uint32_t now) {
  if (a.kind == ActionKind::SettingsChoice) {
    if (working()) return true;
    profileFlow_ = false; settingsSelection_ = listPage_ = 0;
    if (a.value == 0) openSettings({});
    else if (a.value == 1) { page_ = UiPage::GroupSelect; listPage_ = groupRank() / 4; settingsSelection_ = groupRank() % 4; }
    else if (a.value == 2) { page_ = UiPage::UnitSelect; listPage_ = unit_ / 4; settingsSelection_ = unit_ % 4; }
    else { saveProfileChoice_ = a.value == 4; page_ = UiPage::ProfileList; }
    return true;
  }
  if (a.kind == ActionKind::SettingsItem) {
    const uint8_t rank = a.value;
    if (rank >= listCount()) return true;
    if (page_ == UiPage::GroupSelect) openSettings({ScopeKind::Group, uint8_t(groupAt(rank))});
    else if (page_ == UiPage::UnitSelect) openSettings({ScopeKind::Unit, rank});
    else if (page_ == UiPage::ProfileList) chooseProfile(rank, now);
    return true;
  }
  if (a.kind == ActionKind::ProfileInclusion && page_ == UiPage::ProfileOptions) {
    if (saveProfileChoice_ || storedInclusion()) profileReview_.restoreInclusion = !profileReview_.restoreInclusion;
    return true;
  }
  if (a.kind == ActionKind::ProfileName && saveProfileChoice_ && page_ == UiPage::ProfileOptions) {
    memcpy(nameBefore_, profileName_, sizeof(nameBefore_));
    const uint8_t n = strlen(profileName_); for (uint8_t i = n; i < profiles::nameLength; ++i) profileName_[i] = ' ';
    profileName_[profiles::nameLength] = 0; nameCursor_ = 0; nameEditing_ = false; page_ = UiPage::ProfileName; return true;
  }
  if (page_ == UiPage::ProfileName) {
    if (a.kind == ActionKind::NameCursor) { nameCursor_ = moved(nameCursor_, a.value, profiles::nameLength); nameEditing_ = false; return true; }
    if (a.kind == ActionKind::Adjust) { if (nameCursor_ < profiles::nameLength) profileName_[nameCursor_] = nameCharacter(moved(nameIndex(profileName_[nameCursor_]), a.value, 39)); return true; }
    if (a.kind == ActionKind::NameDone) {
      uint8_t end = profiles::nameLength;
      while (end && profileName_[end - 1] == ' ') --end;
      profileName_[end] = 0;
      if (!end) { memcpy(profileName_, "PROFILE ", 8); profileName_[8] = '1' + profileReview_.slot; profileName_[9] = 0; }
      nameEditing_ = false; page_ = UiPage::ProfileOptions; settingsSelection_ = 0; return true;
    }
  }
  if (a.kind == ActionKind::ProfileReview) { reviewProfile(now); return true; }
  if (a.kind == ActionKind::ProfileConfirm) {
    if (page_ == UiPage::ScopeConfirm) commit(now);
    else if (page_ == UiPage::ProfileReview) confirmProfile(now);
    return true;
  }
  if (settingsPage() && (a.kind == ActionKind::Previous || a.kind == ActionKind::Next)) {
    listPage_ = moved(listPage_, a.kind == ActionKind::Next ? 1 : -1, (listCount() + 3) / 4); settingsSelection_ = 0; return true;
  }
  if ((a.kind == ActionKind::Back || a.kind == ActionKind::Cancel) && settingsPage()) {
    if (page_ == UiPage::ProfileName) memcpy(profileName_, nameBefore_, sizeof(profileName_));
    page_ = page_ == UiPage::ConfigMenu ? UiPage::Groups : page_ == UiPage::ScopeConfirm ? UiPage::Config :
        page_ == UiPage::ProfileName || page_ == UiPage::ProfileReview ? UiPage::ProfileOptions :
        page_ == UiPage::ProfileOptions ? UiPage::ProfileList : UiPage::ConfigMenu;
    settingsSelection_ = 0; nameEditing_ = false; return true;
  }
  if (a.kind == ActionKind::Back && page_ == UiPage::Config && !recovering()) { page_ = UiPage::ConfigMenu; settingsSelection_ = 0; return true; }
  if (profileFlow_ && page_ == UiPage::Result && !working() && (a.kind == ActionKind::Back || a.kind == ActionKind::Recover)) {
    page_ = UiPage::ProfileList; settingsSelection_ = 0; profileFlow_ = false; return true;
  }
  return false;
}
}
