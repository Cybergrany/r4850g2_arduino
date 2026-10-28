#include "../util/FlashString.h"
#include "LocalUi.h"
#if PSU_ENABLE_DISPLAY
#include "UiLayout.h"
#include <string.h>
#include <stdio.h>
namespace psu {
void LocalUi::begin() {
  lastInput_ = millis(); // A runtime UI restart begins a new inactivity period.
  available_ = display_.begin(); frame_.clear(); model_.tick(millis());
#if PSU_ENABLE_ENCODER
  wheel_.begin();
#endif
#if PSU_ENABLE_TOUCH
  touchInput_.begin();
  if (!calibrationStore_.load(calibration_) && available_) {
    model_.action({ActionKind::Calibrate},millis()); request(millis());
  }
#endif
}
void LocalUi::request(uint32_t now) {
  const auto r=model_.takeRequest(); (void)now;
#if PSU_ENABLE_TOUCH
  if (r.kind==ActionKind::Calibrate) {
    wizard_.begin(now); calibrationWrite_=retryArmed_=false; gesture_.cancel();
  } else if (r.kind!=ActionKind::TouchDebug)
#endif
  if (r.kind!=ActionKind::None) request_=r;
}
void LocalUi::storageTick() {
#if PSU_ENABLE_TOUCH
  const bool saving=calibrationStore_.saving();
  calibrationStore_.step();
  if (saving && !calibrationStore_.saving()) {
    if (calibrationStore_.result()==StorageResult::Ok) calibration_=wizard_.result();
    dirty_=true; pageReady_=false; gesture_.cancel();
  }
#endif
}
void LocalUi::tick(uint32_t now) {
  if (!available_) return;
  const auto oldPage=model_.page(); const auto oldNotice=model_.notice();
  model_.tick(now);
  bool wheelAction=false;
#if PSU_ENABLE_ENCODER
  const auto event=wheel_.read();
  if (event.delta || event.clicked || event.held) {
    wheelAction=true; lastInput_=now; dirty_=true;
    if (dimmed_) { dimmed_=false; display_.dim(false); }
    else model_.input(event,now);
    pageReady_=false;
#if PSU_ENABLE_TOUCH
    gesture_.cancel();
#endif
  }
#endif
  (void)wheelAction;
#if PSU_ENABLE_TOUCH
  if (oldPage!=model_.page()) { gesture_.cancel(); pageReady_=false; }
  if (!wheelAction) touchTick(now);
#endif
  request(now);
#if PSU_ENABLE_ENCODER || PSU_ENABLE_TOUCH
  const bool calibrationPage=model_.page()==UiPage::Calibration || model_.page()==UiPage::TouchDebug;
  if (!dimmed_ && !calibrationPage && !model_.working() && display_.canDim() && uint32_t(now-lastInput_)>=ui::backlightIdleMs) {
    dimmed_=true; display_.dim(true);
  }
#else
  if (uint32_t(now-lastPage_)>=ui::monitorPageMs) { model_.nextGroupPage(); lastPage_=now; dirty_=true; }
#endif
  if (oldPage!=model_.page()) {
    dirty_=true; pageReady_=false;
#if PSU_ENABLE_TOUCH
    gesture_.cancel();
    if (oldPage==UiPage::Calibration) wizard_.cancel();
#endif
  }
  if (oldNotice!=model_.notice()) dirty_=true;
  // Finish a new page before enabling touch. Periodic telemetry cannot keep
  // restarting its scan and leave the page permanently unarmed.
  if (dirty_ || (pageReady_ && uint32_t(now-lastDraw_)>=ui::refreshMs)) {
    UiView::compose(frame_,model_,controller_,now);
#if PSU_ENABLE_TOUCH
    drawTouch();
#endif
    display_.invalidate(); lastDraw_=now; dirty_=false;
  }
  display_.service(frame_);
  if (!display_.pending()) pageReady_=true;
}
#if PSU_ENABLE_TOUCH
void LocalUi::touchTick(uint32_t now) {
  if (!touchInput_.read(raw_,now)) return;
  if (raw_.down) lastInput_=now;
  if (dimmed_ && raw_.down) { display_.dim(false); dimmed_=false; gesture_.cancel(); return; }
  TouchPoint p; const bool mapped=calibration_.map(raw_.x,raw_.y,p);
  if (raw_.down && mapped) point_=p;
  if (raw_.down && calibration_.valid && !mapped && rejected_!=UINT16_MAX) ++rejected_;
  hit_=raw_.down && mapped ? UiLayout::hit(model_,p.x,p.y,pointedAction_) : 0;
  if (model_.page()==UiPage::Calibration) {
    const auto old=wizard_.state(); const auto target=wizard_.targetIndex();
    if (pageReady_ || !raw_.down) wizard_.sample(raw_,now);
    if (old!=wizard_.state() || target!=wizard_.targetIndex()) { dirty_=true; pageReady_=false; }
    if (wizard_.state()==CalibrationState::Failed || (calibrationWrite_ && !calibrationStore_.saving() && calibrationStore_.result()!=StorageResult::Ok)) {
      if (!raw_.down) retryArmed_=true;
      else if (retryArmed_ && !(mapped && hit_ && pointedAction_.kind==ActionKind::Back)) { wizard_.begin(now); calibrationWrite_=retryArmed_=false; dirty_=true; pageReady_=false; }
    }
    if (wizard_.state()==CalibrationState::Ready) {
      if (!calibrationWrite_) {
        const auto result=calibrationStore_.start(wizard_.result());
        if (result!=StorageResult::Busy) { calibrationWrite_=true; dirty_=true; }
      } else if (!calibrationStore_.saving() && calibrationStore_.result()==StorageResult::Ok && !calibration_.valid) {
        calibration_=wizard_.result(); dirty_=true;
      } else if (!calibrationStore_.saving() && calibrationStore_.result()==StorageResult::Ok) calibration_=wizard_.result();
      if (calibrationWrite_ && !calibrationStore_.saving()) {
        // Completed wizard remains visible until an explicit Back action.
        if (uint32_t(now-lastDraw_)>=ui::refreshMs) dirty_=true;
      }
    }
    if (wizard_.state()==CalibrationState::Collecting || calibrationStore_.saving()) { gesture_.cancel(); return; }
  }
  UiAction action;
  if (raw_.down && mapped) UiLayout::hit(model_,p.x,p.y,action);
  const auto result=gesture_.update(raw_.down,hit_,action.kind==ActionKind::Adjust,pageReady_ && calibration_.valid,now);
  if (result) {
    UiButton b[UiLayout::capacity]; const auto n=UiLayout::buttons(model_,b);
    if (result<=n) {
      if (model_.page()!=UiPage::TouchDebug || b[result-1].action.kind==ActionKind::Back) model_.action(b[result-1].action,now);
      // Paging changes targets even if the page enum stays the same. Keep
      // held +/- repeats live, but gate navigation until its new content lands.
      if (b[result-1].action.kind!=ActionKind::Adjust) { pageReady_=false; gesture_.cancel(); }
      dirty_=true;
    }
  }
}
void LocalUi::drawTouch() {
  if (model_.page()==UiPage::Calibration) {
    // Clear the normal page body; buttons are drawn only after a completed save.
    for (uint8_t r=1;r<15;++r) frame_.text(r,0,26,UI_TEXT(""));
    frame_.text(1,0,26,UI_TEXT("TOUCH CALIBRATION"),Accent);
    if (wizard_.state()==CalibrationState::Collecting) {
      char b[27]; PSU_SNPRINTF(b,sizeof(b),"Hold target %u/5; release",unsigned(wizard_.targetIndex()+1));
      if (wizard_.targetIndex()==5) strcpy(b,"Release to finish");
      frame_.text(12,0,26,b); frame_.text(14,0,26,PSU_ENABLE_ENCODER ? UI_TEXT("Wheel hold: exit") : PSU_ENABLE_SERIAL ? UI_TEXT("Serial: touch debug off") : UI_TEXT("Lift between targets"),Muted);
      const auto t=CalibrationWizard::target(wizard_.targetIndex());
      frame_.text(t.y/16,(t.x-4)/12,1,UI_TEXT("+"),selectedStyle|Accent);
    } else if (wizard_.state()==CalibrationState::Failed) {
      frame_.wrap(4,8,UI_TEXT("Calibration failed. Release then tap to retry."),Warning);
      frame_.text(9,0,26,calibration_.valid ? UI_TEXT("Back: use old calibration") : PSU_ENABLE_ENCODER ? UI_TEXT("Wheel hold: exit") :
          PSU_ENABLE_SERIAL ? UI_TEXT("Serial: touch debug off") : UI_TEXT("Lift between targets"),Muted);
      if (calibration_.valid) UiLayout::controls(frame_,model_);
    } else if (wizard_.state()==CalibrationState::Ready) {
      frame_.text(5,0,26,!calibrationWrite_ || calibrationStore_.saving() ? UI_TEXT("Saving calibration...") :
          calibrationStore_.result()==StorageResult::Ok ? UI_TEXT("Calibration saved") : UI_TEXT("Calibration save failed"),
          calibrationStore_.saving() || !calibrationWrite_ ? Warning : calibrationStore_.result()==StorageResult::Ok ? Good : Error);
      frame_.text(7,0,26,UI_TEXT("Charger config unchanged"),Muted);
      if (calibrationWrite_ && !calibrationStore_.saving()) {
        if (calibrationStore_.result()!=StorageResult::Ok) frame_.text(9,0,26,UI_TEXT("Release, tap to retry"),Warning);
        UiLayout::controls(frame_,model_);
      }
    }
  } else if (model_.page()==UiPage::TouchDebug) {
    frame_.text(1,0,26,UI_TEXT("TOUCH DIAGNOSTICS"),Accent);
    char b[27]; PSU_SNPRINTF(b,sizeof(b),"Raw %d,%d Z=%d",raw_.x,raw_.y,raw_.pressure); frame_.text(3,0,26,b);
    PSU_SNPRINTF(b,sizeof(b),"Pixel %d,%d Down=%u",point_.x,point_.y,unsigned(raw_.down)); frame_.text(5,0,26,b);
    frame_.text(7,0,26,!hit_ ? UI_TEXT("Target: none") : pointedAction_.kind==ActionKind::Back ? UI_TEXT("Target: Back") :
        pointedAction_.value<0 ? UI_TEXT("Target: test minus") : UI_TEXT("Target: test plus"));
    PSU_SNPRINTF(b,sizeof(b),"Read max %luus",(unsigned long)touchInput_.maximumMicros()); frame_.text(11,0,26,b);
    if (raw_.down && point_.y<112) frame_.text(point_.y/16,point_.x>4 ? (point_.x-4)/12 : 0,1,UI_TEXT("+"),Accent);
  }
}
#endif
bool LocalUi::command(uint8_t n,char** a,Print& out,uint32_t now) {
  if (PSU_STRCMP(a[0], "touch")) return false;
#if PSU_ENABLE_TOUCH
  if (n==2 && !PSU_STRCMP(a[1], "status")) {
    out.print(F("touch calibrated=")); out.print(calibration_.valid); out.print(F(" rejected=")); out.print(rejected_);
    out.print(F(" read-max-us=")); out.print(touchInput_.maximumMicros());
    out.print(F(" save=")); out.print(calibrationStore_.saving() ? F("saving") : calibrationStore_.result()==StorageResult::Ok ? F("ok") :
        calibrationStore_.result()==StorageResult::NoValidRecord ? F("none") : F("failed"));
    out.print(F(" raw=")); out.print(raw_.x); out.print(','); out.print(raw_.y); out.print(F(" z=")); out.println(raw_.pressure);
  } else if (n==3 && !PSU_STRCMP(a[1], "calibration") && !PSU_STRCMP(a[2], "export")) {
    out.print(F("touch profile=")); out.print(touch::profile); out.print(F(" valid=")); out.print(calibration_.valid);
    out.print(F(" affine Q16.16=")); for (uint8_t i=0;i<6;++i) { out.print(calibration_.coefficient[i]); out.print(i==5 ? '\n' : ','); }
  } else if (n==2 && !PSU_STRCMP(a[1], "calibrate")) {
    if (!available_) out.println(F("ERR display unavailable"));
    else { model_.action({ActionKind::Calibrate},now); request(now); dirty_=true; pageReady_=false;
      out.println(model_.page()==UiPage::Calibration ? F("Touch calibration started") : F("ERR busy")); }
  } else if (n==3 && !PSU_STRCMP(a[1], "debug") && (!PSU_STRCMP(a[2], "on") || !PSU_STRCMP(a[2], "off"))) {
    if (!available_) out.println(F("ERR display unavailable"));
    else {
      const bool on=!PSU_STRCMP(a[2], "on");
      if (on) model_.action({ActionKind::TouchDebug},now);
      else if (model_.page()==UiPage::TouchDebug || model_.page()==UiPage::Calibration) model_.action({ActionKind::Back},now);
      request(now); gesture_.cancel(); dirty_=true; pageReady_=false;
      out.println(on && model_.page()!=UiPage::TouchDebug ? F("ERR busy") : F("OK"));
    }
  } else out.println(F("touch status | calibrate | debug on/off | calibration export"));
#else
  (void)n; (void)now; out.println(F("ERR touch disabled in this build"));
#endif
  return true;
}
}
#endif
