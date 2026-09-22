#include "LocalUi.h"
#if PSU_ENABLE_DISPLAY
namespace psu {
void LocalUi::begin() {
  available_ = display_.begin(); frame_.clear();
#if PSU_ENABLE_ENCODER
  wheel_.begin();
#endif
}
void LocalUi::tick(uint32_t now) {
  // Missing hardware leaves CAN/serial usable and disables blind edits.
  if (!available_) return;
  const auto oldPage = model_.page(); const auto oldNotice = model_.notice();
  model_.tick(now);
#if PSU_ENABLE_ENCODER
  const auto event = wheel_.read();
  if (event.delta || event.clicked || event.held) {
    lastInput_ = now; dirty_ = true;
    if (dimmed_) { dimmed_ = false; display_.dim(false); }
    else model_.input(event, now);
  }
  if (!dimmed_ && !model_.working() && display_.canDim() && uint32_t(now - lastInput_) >= ui::backlightIdleMs) {
    dimmed_ = true; display_.dim(true);
  }
#else
  if (uint32_t(now - lastPage_) >= ui::monitorPageMs) { model_.nextGroupPage(); lastPage_ = now; dirty_ = true; }
#endif
  if (oldPage != model_.page() || oldNotice != model_.notice()) dirty_ = true;
  if (dirty_ || uint32_t(now - lastDraw_) >= ui::refreshMs) {
    UiView::compose(frame_, model_, controller_, now); lastDraw_ = now; dirty_ = false;
  }
  display_.service(frame_);
}
}
#endif
