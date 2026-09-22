#include "LocalUi.h"
#if PSU_ENABLE_DISPLAY || PSU_ENABLE_ENCODER
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
  if (event.delta || event.clicked) {
    const int16_t count = controller_.count();
    const int16_t delta = event.delta ? event.delta : 1;
    index_ = (int32_t(index_) + delta % count + count) % count;
    dirty_ = true;
  }
#endif
#if PSU_ENABLE_DISPLAY
  if (dirty_ || uint32_t(now - lastDraw_) >= 1000) {
    display_.status(controller_, index_, now);
    dirty_ = false; lastDraw_ = now;
  }
#else
  (void)now;
#endif
}
}
#endif
