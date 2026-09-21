#include "LocalUi.h"
#if PSU_ENABLE_DISPLAY || PSU_ENABLE_ENCODER
#include "../config/BoardConfig.h"

namespace psu {
void LocalUi::begin() {
#if PSU_ENABLE_ENCODER
  wheel_.begin();
#endif
#if PSU_ENABLE_DISPLAY
  display_.begin();
#endif
}
void LocalUi::tick(uint32_t now) {
  if (index_ >= controller_.count()) index_ = 0;
#if PSU_ENABLE_ENCODER
  const WheelEvent event = wheel_.read();
  if (event.delta || event.clicked) { lastInput_ = now; dirty_ = true; error_ = false; }
  if (event.clicked) {
    if (!menu_) { menu_ = true; editing_ = false; item_ = 0; }
    else if (item_ <= 4) editing_ = !editing_;
    else if (item_ == 5 || item_ == 6) {
      error_ = controller_.applySingle(index_, item_ == 5 ? ApplyMode::Online : ApplyMode::OnlineAndOffline) != Result::Ok;
      if (!error_) menu_ = false;
    } else if (item_ == 7) {
      error_ = controller_.busy() || memory_.save(controller_.configuration()) != StorageResult::Ok;
    } else menu_ = false;
  }
  if (event.delta) {
    if (!menu_ || (editing_ && item_ == 0)) {
      const int16_t count = controller_.count();
      index_ = (int32_t(index_) + event.delta % count + count) % count;
    } else if (!editing_) item_ = (int32_t(item_) + event.delta % 9 + 9) % 9;
    else {
      const auto& c = controller_.unit(index_).config();
      const Parameter p = item_ == 1 ? Parameter::Voltage : item_ == 2 ? Parameter::Current :
                          item_ == 3 ? Parameter::OfflineVoltage : Parameter::OfflineCurrent;
      const uint16_t old = item_ == 1 ? c.voltage : item_ == 2 ? c.current :
                           item_ == 3 ? c.offlineVoltage : c.offlineCurrent;
      error_ = controller_.modifySingle(index_, p, old / 100.0f + event.delta * 0.1f) != Result::Ok;
    }
  }
  if (menu_ && uint32_t(now - lastInput_) >= board::menuTimeoutMs) {
    menu_ = false; editing_ = false; dirty_ = true;
  }
#endif
#if PSU_ENABLE_DISPLAY
  if (dirty_ || uint32_t(now - lastDraw_) >= 1000) {
    if (menu_) display_.menu(controller_, index_, item_, editing_, error_);
    else display_.status(controller_, index_, now);
    dirty_ = false; lastDraw_ = now;
  }
#endif
}
}
#endif
