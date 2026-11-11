#include "SerialConsole.h"
#if PSU_ENABLE_SERIAL
#include "UiText.h"
#include "../util/FlashString.h"
#include <string.h>
namespace psu {
namespace {
bool number(const char* text, uint8_t maximum, uint8_t& value) {
  if (!text || !*text) return false;
  uint16_t n = 0;
  for (; *text; ++text) { if (*text < '0' || *text > '9') return false; n = n * 10 + *text - '0'; if (n > maximum) return false; }
  value = n; return n != 0;
}
}
bool SerialConsole::parseScope(const char* text, SettingScope& scope) const {
  if (!PSU_STRCMP(text, "all")) { scope = {}; return true; }
  if (!strncmp(text, "psu:", 4)) {
    uint8_t n; if (!number(text + 4, controller_.count(), n)) return false;
    scope = {ScopeKind::Unit, uint8_t(n - 1)}; return true;
  }
  const auto g = controller_.groupIndex(text);
  if (g < 0) return false;
  scope = {ScopeKind::Group, uint8_t(g)}; return true;
}
bool SerialConsole::profileCommand(char* line) {
  while (*line == ' ' || *line == '\t') ++line;
  if (strncmp(line, "profile", 7) || (line[7] && line[7] != 's' && line[7] != ' ' && line[7] != '\t')) return false;
  char* context = nullptr; char* verb = strtok_r(line, " \t", &context);
  if (!PSU_STRCMP(verb, "profiles")) { profilePending_ = 0; startView(View::Profiles); return true; }
  if (PSU_STRCMP(verb, "profile")) { reply(Result::Invalid); return true; }
  char* action = strtok_r(nullptr, " \t", &context); char* target = strtok_r(nullptr, " \t", &context);
  if (!action || !target) { io_.println(F("profile show/save/load SLOT [name]; profile inclusion yes/no")); return true; }
  if (!PSU_STRCMP(action, "inclusion")) {
    if (!profilePending_ || (PSU_STRCMP(target, "yes") && PSU_STRCMP(target, "no"))) { reply(Result::Invalid); return true; }
    const bool on = !PSU_STRCMP(target, "yes");
    if (profilePending_ == 2) profileReview_ = controller_.previewProfile(memory_, profileReview_.slot, on, now_);
    else profileReview_.restoreInclusion = on;
    startView(View::Profile); return true;
  }
  uint8_t slot;
  if (!number(target, profiles::count, slot)) { reply(Result::Invalid); return true; }
  --slot; confirmation_ = availabilityConfirmation_ = false; profilePending_ = 0;
  profileReview_ = {}; profileReview_.slot = slot; profileReview_.created = now_;
  profileReview_.revision = controller_.configurationRevision(); profileReview_.topology = controller_.discovery().revision();
  profileReview_.availability = controller_.availabilityRevision(); profileReview_.included = controller_.participants();
  ProfileInfo info; const auto status = memory_.profileInfo(slot, info);
  profileCount_ = status == StorageResult::Ok ? info.count : controller_.count();
  if (!PSU_STRCMP(action, "save")) {
    if (controller_.busy() || memory_.saving() || controller_.availabilitySaving()) { reply(Result::Busy); return true; }
    if (status != StorageResult::Ok && status != StorageResult::NoValidRecord) { storageReply(status); return true; }
    profileReview_.generation = status == StorageResult::Ok ? info.generation : 0;
    profileCount_ = controller_.count();
    while (*context == ' ' || *context == '\t') ++context;
    const size_t n = strlen(context);
    if (n > profiles::nameLength) { reply(Result::Invalid); return true; }
    memcpy(profileName_, context, n + 1);
    if (!n && status == StorageResult::Ok) memcpy(profileName_, info.name, sizeof(profileName_));
    for (uint8_t i=0;i<controller_.count();++i) if (!identified(controller_.configuration().units[i].identity)) profileReview_.blocker=Issue::Unbound;
    profilePending_ = 1;
  } else if (!PSU_STRCMP(action, "load")) {
    profileReview_ = controller_.previewProfile(memory_, slot, false, now_); profilePending_ = 2;
  } else if (!PSU_STRCMP(action, "show")) {
    profileReview_.storage = status;
    if (status == StorageResult::Ok) {
      profileReview_.generation = info.generation; profileReview_.included = info.included;
      profileReview_.restoreInclusion = info.hasInclusion; // Show stored metadata; this is not a load request.
    }
  } else { reply(Result::Invalid); return true; }
  startView(View::Profile); return true;
}
void SerialConsole::profileConfirm(bool yes) {
  const uint8_t operation = profilePending_; profilePending_ = 0;
  if (!yes) { io_.println(F("Cancelled; no profile changes")); return; }
  if (profileReview_.revision != controller_.configurationRevision() || profileReview_.topology != controller_.discovery().revision() ||
      profileReview_.availability != controller_.availabilityRevision() || uint32_t(now_ - profileReview_.created) > limits::confirmationMs) {
    reply(Result::Changed); return;
  }
  if (operation == 1) {
    if (profileReview_.blocker != Issue::None) { reply(Result::Blocked); io_.println(issueName(profileReview_.blocker)); return; }
    const auto r = memory_.startProfileSave(profileReview_.slot, profileName_, controller_.configuration(), profileReview_.generation,
        profileReview_.restoreInclusion, controller_.participants());
    if (r != StorageResult::Ok) { storageReply(r); return; }
    waitingSave_ = true; saveToken_ = memory_.saveToken(); saveRevision_ = controller_.configurationRevision();
    io_.println(F("Saving named profile; no settings applied. Wait for verification."));
  } else {
    const auto r = controller_.loadProfile(memory_, profileReview_, now_); reply(r);
    if (r == Result::Ok) io_.println(F("Profile load started; enable/fault/temperature guards remain active."));
  }
}
void SerialConsole::profileRow() {
  if (view_ == View::Profiles) {
    if (row_ >= profiles::count) { view_ = View::None; return; }
    ProfileInfo info; const auto result = memory_.profileInfo(row_, info);
    io_.print(F("PROFILE ")); io_.print(++row_); io_.print(' ');
    if (result == StorageResult::Ok) { io_.print(info.name); io_.print(F(" units=")); io_.print(info.count); io_.print(F(" participation=")); io_.println(info.hasInclusion ? F("stored") : F("not stored")); }
    else io_.println(result == StorageResult::NoValidRecord ? F("EMPTY") : F("INVALID/WRONG DEPLOYMENT"));
    return;
  }
  const auto& p = profileReview_;
  if (row_ == 0) {
    io_.print(profilePending_ == 1 ? F("SAVE PROFILE ") : profilePending_ == 2 ? F("LOAD PROFILE ") : F("PROFILE "));
    io_.print(p.slot + 1); io_.print(F(" generation=")); io_.println(p.generation);
    if (profilePending_ == 1) { io_.print(F("Name: ")); io_.println(*profileName_ ? profileName_ : "(default)"); }
    if (p.storage != StorageResult::Ok) {
      storageReply(p.storage);
      if (p.storage == StorageResult::IdentityMismatch) {
        if (p.mismatch < PSU_MAX_UNITS) {
          ProfileTarget target;
          if (memory_.profileTarget(p.slot, p.mismatch, p.generation, target) == StorageResult::Ok) {
            io_.print(F("Unmatched saved identity=")); printIdentity(target.identity); io_.println();
          }
        } else {
          ProfileInfo info;
          if (memory_.profileInfo(p.slot, info) == StorageResult::Ok) {
            io_.print(F("Saved PSU count=")); io_.print(info.count);
            io_.print(F(" configured=")); io_.println(controller_.count());
          }
        }
        io_.println(F("profile show lists saved IDs; config lists current bindings. No reassignment."));
      } else if (p.storage == StorageResult::InvalidConfig && p.mismatch < controller_.count()) {
        io_.print(F("PSU ")); io_.print(p.mismatch + 1); io_.println(F(" saved current exceeds its configured rating"));
      }
      view_ = View::None; return;
    }
  } else if (row_ <= profileCount_) {
    const uint8_t entry = row_ - 1;
    if (profilePending_ == 1) {
      const auto& c = controller_.configuration(); io_.print(F("PSU ")); io_.print(entry + 1); io_.print(' '); printIdentity(c.units[entry].identity);
      io_.print(F(" V=")); io_.print(c.voltage[entry] / 100.0f); io_.print(F(" A=")); io_.println(c.current[entry] / 100.0f);
    } else {
      ProfileTarget t;
      const auto r = memory_.profileTarget(p.slot, entry, p.generation, t);
      if (r != StorageResult::Ok) { storageReply(r); view_ = View::None; return; }
      io_.print(F("entry ")); io_.print(entry + 1); io_.print(F(" id=")); printIdentity(t.identity);
      io_.print(F(" V=")); io_.print(t.voltage / 100.0f); io_.print(F(" A=")); io_.println(t.current / 100.0f);
    }
  } else if (row_ == profileCount_ + 1) {
    io_.print(profilePending_ ? F("Participation store/restore=") : F("Participation stored=")); io_.println(p.restoreInclusion ? F("YES") : F("NO"));
    if (profilePending_ || p.restoreInclusion) {
      io_.print(profilePending_ ? F("Included slots=") : F("Included saved entries=")); printMembers(p.included); io_.println();
    }
    if (p.blocker != Issue::None) {
      io_.print(F("BLOCKED: ")); io_.println(p.blocker == Issue::Disabled ? F("Turn OFF and confirm zero hold before changing participation") : issueName(p.blocker));
    }
  } else {
    if (profilePending_) io_.println(F("Optional: profile inclusion yes/no (default NO). Review, then confirm yes/no within 15s."));
    view_ = View::None;
  }
  ++row_;
}
}
#endif
