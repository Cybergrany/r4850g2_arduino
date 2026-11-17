#include "SerialConsole.h"
#if PSU_ENABLE_SERIAL
#include "UiText.h"
#include "../util/FlashString.h"
namespace psu {
// Keep status temporaries out of the serial command and UI call chains.
bool SerialConsole::syncNotification(uint32_t now) {
  if (length_ || view_ != View::None) return false;
  const auto s = controller_.syncStatus(now);
  const uint16_t key = uint16_t(s.phase)<<12 | s.qualified | (s.bypass ? 0x8000 : 0) | (s.timedOut ? 0x800 : 0);
  if (key == syncNotice_) return false;
  // Coalesce noisy measurement changes, but announce phases/bypass/timeout immediately.
  if (!((key ^ syncNotice_) & 0xf800U) && uint32_t(now-syncNoticeAt_) < 1000) return false;
  syncNoticeAt_=now; syncNotice_=key; beginOutput(); syncRow(0,now); prompt(); return true;
}
void SerialConsole::syncCommand(uint8_t argc, char** args) {
  if (argc == 1) { startView(View::Sync); return; }
  if (argc >= 3 && !PSU_STRCMP(args[1],"bypass")) {
    if (argc == 3 && !PSU_STRCMP(args[2],"off")) { controller_.requireSync(); syncConfirmation_=false; reply(Result::Ok); return; }
    if (!PSU_STRCMP(args[2],"on")) {
      if (argc == 3) {
        syncReview_ = controller_.reviewSync(now_); syncConfirmation_=true;
        io_.println(F("REVIEW: bypass may release authorized currents now. Session only; protection/enable/participation remain."));
        io_.println(F("Confirm within 15s: sync bypass on confirm")); return;
      }
      if (argc == 4 && !PSU_STRCMP(args[3],"confirm")) {
        if (!syncConfirmation_) { io_.println(F("REVIEW REQUIRED: sync bypass on")); return; }
        const auto result = controller_.bypassSync(syncReview_,now_); syncConfirmation_=false;
        if (result == Result::Changed) io_.println(F("BLOCKED: sync review expired or changed; sync bypass on to review again"));
        else reply(result);
        return;
      }
    }
  }
  reply(Result::Invalid);
}
void SerialConsole::syncRow(uint8_t row, uint32_t now) {
  const auto s = controller_.syncStatus(now);
  if (row == 0) {
    io_.print(F("CURRENT SYNC ")); io_.print(syncPhaseName(s.phase)); io_.print(F(" qualified="));
    io_.print(population(s.qualified)); io_.print('/'); io_.print(population(s.members));
    io_.print(F(" elapsed-ms=")); io_.print(s.elapsedMs); io_.print(F(" bypass=")); io_.println(s.bypass);
  } else if (row == 1) {
    io_.print(F("members=")); printMembers(s.members); io_.print(F(" waiting=")); printMembers(s.members & UnitMask(~s.qualified)); io_.println();
  } else if (row == 2) {
    io_.print(F("prime-max-A=")); io_.print(currentSync::primeCa/100.0f); io_.print(F(" tolerance-percent=")); io_.print(currentSync::tolerance);
    io_.print(F(" stable-ms=")); io_.print(currentSync::stableMs); io_.print(F(" deadline-ms=")); io_.println(currentSync::timeoutMs);
  } else io_.println(F("Timeout holds priming; OFF stops. sync bypass on / on confirm / off. Bypass never saved; reboot OFF."));
}
}
#endif
