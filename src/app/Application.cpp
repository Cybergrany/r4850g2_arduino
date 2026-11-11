#include "../util/FlashString.h"
#include "Application.h"
#include <Arduino.h>
#include <string.h>
#include <new>
#include <stdlib.h>

namespace psu {
#if PSU_ENABLE_DISPLAY && PSU_ENABLE_SERIAL
namespace {
void phaseTime(uint32_t& maximum, uint32_t& started) {
  const uint32_t now = micros(), elapsed = now - started;
  if (elapsed > maximum) maximum = elapsed;
  started = now;
}
}
#endif
Application::Application() : controller_(can_), memory_(eeprom_)
#if PSU_ENABLE_SERIAL
  , console_(Serial, controller_, memory_)
#endif
#if PSU_ENABLE_DISPLAY
  , localUi_(controller_, memory_, display_, eeprom_)
#endif
{}
StorageResult Application::configureFromStorage() {
#if PSU_UI_DEBUG
  can_.reset();
  Configuration config = debugConfiguration();
  const StorageResult loaded = memory_.load(config);
#else
  Configuration config;
  const StorageResult loaded = memory_.loadCandidate(config);
  if (loaded != StorageResult::Ok && loaded != StorageResult::Migrated && loaded != StorageResult::WrongDeployment)
    defaultConfiguration(config);
#endif
  controller_.configure(config, PSU_UI_DEBUG || loaded == StorageResult::Ok);
  controller_.storageStatus(loaded);
  controller_.configureStartup(PSU_CHG_ALLOW_PIN >= 0 || PSU_UI_DEBUG, PSU_UI_DEBUG || loaded == StorageResult::Ok, millis());
  return loaded;
}
void Application::begin() {
#if PSU_CHG_ALLOW_PIN >= 0 && !PSU_UI_DEBUG
  chargeAllow_.begin(); // First application action; reset-time LOW is hardware-owned.
#endif
#if PSU_ACTIVE_PIN >= 0 && !PSU_UI_DEBUG
  psuActive_.begin();
#endif
#if !PSU_UI_DEBUG && (PSU_WARNING_PIN >= 0 || PSU_ERROR_PIN >= 0)
  alertOutputs_.begin();
#endif
  static_assert(board::eepromBudget == MemoryManager::budget, "EEPROM budgets must agree");
#if PSU_FAN_PWM_PIN >= 0 && !PSU_UI_DEBUG
  fanOutput_.begin();
#endif
  const auto loaded = configureFromStorage();
#if PSU_ENABLE_ENABLE_IN && !PSU_UI_DEBUG
  pinMode(control::enablePin, control::enablePullup ? INPUT_PULLUP : INPUT);
  controller_.sampleEnableInput((digitalRead(control::enablePin) == LOW) == control::enableActiveLow, millis());
#endif
  // Complete display reset/clear before CAN reception. Runtime drawing is bounded.
#if PSU_ENABLE_DISPLAY
  localUi_.begin();
#endif
  controller_.begin();
#if PSU_ENABLE_SERIAL
  Serial.begin(board::serialBaud); // Boot does not wait for an attached serial monitor.
  console_.extension(command,this);
  console_.begin(loaded);
#else
  (void)loaded;
#endif
#if PSU_ENABLE_SERIAL
#if PSU_ENABLE_DISPLAY
  console_.displayStatus(localUi_.available());
#endif
  console_.finishStartup();
#endif
}
void Application::tick() {
  const uint32_t started = micros();
#if PSU_ENABLE_DISPLAY && PSU_ENABLE_SERIAL
  uint32_t phaseStarted = started;
#endif
  // Receive/process CAN before optional producers. Storage has one owner and
  // advances only when EEPROM is ready, including serial-only builds.
#if PSU_UI_DEBUG
  debugTick(millis());
  can_.permit(controller_.chargeAllowed());
#endif
#if PSU_ENABLE_ENABLE_IN && !PSU_UI_DEBUG
  controller_.sampleEnableInput((digitalRead(control::enablePin) == LOW) == control::enableActiveLow, millis());
#endif
  controller_.tick(millis());
#if PSU_ACTIVE_PIN >= 0 && !PSU_UI_DEBUG
  psuActive_.write(controller_.psuActive(millis()));
#endif
#if PSU_UI_DEBUG
  can_.permit(controller_.chargeAllowed());
#endif
#if PSU_CHG_ALLOW_PIN >= 0 && !PSU_UI_DEBUG
  chargeAllow_.write(controller_.chargeAllowed());
#endif
#if PSU_FAN_PWM_PIN >= 0 && !PSU_UI_DEBUG
  const uint32_t fanNow = millis();
  if (fanOutput_.due(fanNow)) fanOutput_.write(controller_.externalFanStatus(fanNow).duty);
#endif
#if PSU_ENABLE_DISPLAY && PSU_ENABLE_SERIAL
  phaseTime(phaseMaxUs_[0], phaseStarted);
#endif
  controller_.serviceProtection(memory_, millis());
  memory_.stepSave();
  controller_.profileTick(memory_);
#if PSU_ENABLE_DISPLAY
  localUi_.storageTick();
#endif
  controller_.serviceProtection(memory_, millis());
#if PSU_ENABLE_DISPLAY && PSU_ENABLE_SERIAL
  phaseTime(phaseMaxUs_[1], phaseStarted);
#endif
#if PSU_ENABLE_SERIAL
  console_.tick(millis());
#if PSU_ENABLE_DISPLAY
  phaseTime(phaseMaxUs_[2], phaseStarted);
#endif
#endif
#if PSU_ENABLE_DISPLAY
  localUi_.tick(millis());
  // A foreground page/highlight gets consecutive short slices. Service the RX
  // queue and thermal protection between them rather than repeating all control,
  // availability, presentation and serial work for every few pixels.
  const uint32_t paintStarted = micros();
  for (uint8_t n = 0; localUi_.priorityDrawing() && n < ui::priorityRenderSlices &&
      uint32_t(micros() - paintStarted) < ui::priorityRenderUs; ++n) {
    controller_.serviceReceive(millis());
#if PSU_CHG_ALLOW_PIN >= 0 && !PSU_UI_DEBUG
    chargeAllow_.write(controller_.chargeAllowed());
#endif
#if PSU_ACTIVE_PIN >= 0 && !PSU_UI_DEBUG
    if (!controller_.enableControl().enabled()) psuActive_.write(false);
#endif
    localUi_.paint();
  }
#if PSU_ENABLE_SERIAL
  phaseTime(phaseMaxUs_[3], phaseStarted);
#endif
#if PSU_UI_DEBUG
  const auto request = localUi_.takeRequest();
  if (request.kind != ActionKind::None) debugRequest_ = request;
#endif
#endif
#if PSU_CHG_ALLOW_PIN >= 0 && !PSU_UI_DEBUG
  chargeAllow_.write(controller_.chargeAllowed()); // Includes serial and local UI hard stops.
#endif
#if PSU_ACTIVE_PIN >= 0 && !PSU_UI_DEBUG
  psuActive_.write(controller_.psuActive(millis())); // Includes serial/UI OFF in this loop.
#endif
#if !PSU_UI_DEBUG && (PSU_WARNING_PIN >= 0 || PSU_ERROR_PIN >= 0)
  alertOutputs_.write(controller_.alerts());
#endif
  controller_.recordLoopTime(uint32_t(micros() - started));
}
}

namespace psu {
#if PSU_UI_DEBUG
void Application::resetDebug(bool restart, bool erase, DebugScenario scenario) {
  if (erase) eeprom_.clear();
  eeprom_.failWrites = false;
  Configuration cfg = debugConfiguration();
  const bool loaded = restart && memory_.load(cfg) == StorageResult::Ok;
  can_.reset(); desiredScenario_ = scenario; changed_ = false; scenarioResult_ = Result::Ok;
  controller_.~PsuController(); new (&controller_) PsuController(can_);
  controller_.configure(cfg,true); controller_.begin();
  controller_.configureStartup(true, true, millis());
  // A saved profile owns its own restore policy; it may authorize only a subset
  // of units or disable restore entirely. Only the known fixture needs priming.
  priming_ = !loaded;
#if PSU_ENABLE_DISPLAY
  localUi_.resetNavigation();
#endif
#if PSU_ENABLE_SERIAL
  console_.controllerReset();
#endif
}
void Application::debugTick(uint32_t now) {
  if (debugInputPresent_) controller_.sampleEnableInput(debugInput_, now);
  if (debugRequest_.kind != ActionKind::None && !controller_.busy() && !memory_.saving()) {
    const auto r = debugRequest_; debugRequest_ = {};
    resetDebug(r.kind == ActionKind::DebugRestart,r.kind == ActionKind::DebugReset,
        r.kind == ActionKind::ScenarioStart ? DebugScenario(r.value) : DebugScenario::Healthy);
  }
  // Inject scenarios only after every output has qualified, not just after
  // the final setting ACK. With nine units those are visibly different times.
  if (priming_ && !controller_.busy() && controller_.report().succeeded == membersMask(controller_.count()) &&
      controller_.controlStatus(now).activity == Activity::Running) {
    priming_ = false; can_.scenario(desiredScenario_);
    if (desiredScenario_ == DebugScenario::ZeroHold || desiredScenario_ == DebugScenario::ZeroReject ||
        desiredScenario_ == DebugScenario::StopUnconfirmed) controller_.setAutoResume(false);
    // A software reconcile preserves an accepted fan mode. Model a fresh
    // controller startup here so the rejection scenario actually needs a write.
    if (desiredScenario_ == DebugScenario::FanReject) scenarioResult_ = controller_.configure(controller_.configuration(),true);
    if (desiredScenario_ == DebugScenario::WakeCurrent) controller_.reconcile();
    eeprom_.failWrites = desiredScenario_ == DebugScenario::SaveFailed;
    if (desiredScenario_ == DebugScenario::BusOffline) controller_.begin();
    if (desiredScenario_ == DebugScenario::Empty) {
      for (uint8_t i=0;i<limits::maxGroups;++i) if (controller_.configuration().groups[i].members) {
        scenarioResult_ = controller_.removeGroup(i);
        if (scenarioResult_ != Result::Ok) break;
      }
    } else if (desiredScenario_ == DebugScenario::Unassigned) scenarioResult_ = controller_.removeGroup(0);
    else if (desiredScenario_ == DebugScenario::Extremes) {
      auto cfg=controller_.configuration(); strcpy(cfg.groups[0].name,"LONGGRP1"); scenarioResult_ = controller_.configure(cfg,true);
    } else if (desiredScenario_ == DebugScenario::Unbound) scenarioResult_ = controller_.unbind(PSU_MAX_UNITS>1 ? 1 : 0);
    else if (desiredScenario_ == DebugScenario::VoltageUnsynced) {
      auto cfg=controller_.configuration(); for (auto& v : cfg.voltage) v += 10;
      // Loading a draft without restore clears runtime voltage ACKs. Merely
      // staging a different voltage correctly retains the authorized voltage.
      scenarioResult_ = controller_.configure(cfg);
    }
    else if (desiredScenario_ == DebugScenario::WrongDeployment) {
      auto cfg = controller_.configuration(); ++cfg.deploymentId; scenarioResult_ = controller_.configure(cfg);
    }
  }
#if PSU_ENABLE_DISPLAY
  if (!priming_ && (desiredScenario_ == DebugScenario::Changed || desiredScenario_ == DebugScenario::SaveChanged)) {
    const bool editing = desiredScenario_ == DebugScenario::SaveChanged ? memory_.saving() : localUi_.editing();
    if (editing && !changed_ && !controller_.busy()) controller_.setPollInterval(controller_.pollInterval()==1000 ? 1001 : 1000);
    changed_ = editing;
  }
#endif
}
#endif
#if PSU_ENABLE_SERIAL
bool Application::command(void* context,uint8_t n,char** a,Print& out,uint32_t now) {
  auto& app=*static_cast<Application*>(context); (void)app; (void)now;
#if PSU_ENABLE_DISPLAY
  if (app.localUi_.command(n,a,out,now)) {
    if (n >= 2 && !PSU_STRCMP(a[0], "diag") && !PSU_STRCMP(a[1], "ui")) {
      if (n == 3) memset(app.phaseMaxUs_, 0, sizeof(app.phaseMaxUs_));
      else {
        out.print(F("phase-us control/storage/serial/ui="));
        for (uint8_t i = 0; i < 4; ++i) { if (i) out.print('/'); out.print(app.phaseMaxUs_[i]); }
        out.println();
      }
    }
    return true;
  }
#else
  if (n == 2 && !PSU_STRCMP(a[0], "diag") && !PSU_STRCMP(a[1], "ui")) {
    out.println(F("UI disabled in this build")); return true;
  }
#endif
  if (PSU_STRCMP(a[0], "ui-debug")) return false;
#if PSU_UI_DEBUG
  if (n==4 && !PSU_STRCMP(a[1], "temperature")) {
    int16_t values[2];
    for (uint8_t i=0;i<2;++i) {
      char* end; const long value = strtol(a[i+2], &end, 10);
      if (!PSU_STRCMP(a[i+2], "unknown")) values[i]=1000;
      else if (!*a[i+2] || *end || value < -40 || value > 150) { out.println(F("ERR temperature -40..150 C or unknown")); return true; }
      else values[i]=value;
    }
    app.can_.temperatures(values[0], values[1]);
    out.println(F("SIM temperature changed; real thermal policy and stop persistence apply."));
  } else if (n==4 && (!PSU_STRCMP(a[1], "source") || !PSU_STRCMP(a[1], "battery")) && (!PSU_STRCMP(a[3], "on") || !PSU_STRCMP(a[3], "off"))) {
    const int8_t group = app.controller_.groupIndex(a[2]);
    const UnitMask mask = !PSU_STRCMP(a[2], "all") ? membersMask(app.controller_.count()) : group >= 0 ? app.controller_.configuration().groups[group].members : 0;
    if (!mask) out.println(F("ERR SIM target: all or group"));
    else {
      if (!PSU_STRCMP(a[1], "source")) app.can_.sourcePower(mask, !PSU_STRCMP(a[3], "on"));
      else app.can_.batteryPower(mask, !PSU_STRCMP(a[3], "on"));
      out.println(F("SIM power changed; real startup/availability policy applies"));
    }
  } else if (n==3 && !PSU_STRCMP(a[1], "enable-input") && (!PSU_STRCMP(a[2], "on") || !PSU_STRCMP(a[2], "off"))) {
    app.debugInputPresent_ = true; app.debugInput_ = !PSU_STRCMP(a[2], "on");
    out.println(F("SIM ENABLE_IN changed; normal debounce and latest-wins policy apply"));
  } else if (n==2 && !PSU_STRCMP(a[1], "status")) {
    out.print(F("SIM: CAN excluded; RAM storage; scenario="));
    out.print(reinterpret_cast<const __FlashStringHelper*>(scenarioName(app.desiredScenario_).data));
    out.print(F(" priming=")); out.print(app.priming_); out.print(F(" pending=")); out.println(app.debugRequest_.kind!=ActionKind::None);
    if (app.scenarioResult_ != Result::Ok) { out.print(F("ERR SIM scenario setup rejected; code=")); out.println(uint8_t(app.scenarioResult_)); }
  } else if (n==2 && (!PSU_STRCMP(a[1], "reset") || !PSU_STRCMP(a[1], "restart"))) {
    app.debugRequest_={!PSU_STRCMP(a[1], "reset") ? ActionKind::DebugReset : ActionKind::DebugRestart};
    out.println(F("SIM request queued; waits for active apply/save"));
  } else if (n==3 && !PSU_STRCMP(a[1], "scenario")) {
    for (uint8_t i=0;i<uint8_t(DebugScenario::Count);++i) {
#ifdef __AVR__
      const bool match=!strcmp_P(a[2],scenarioName(DebugScenario(i)).data);
#else
      const bool match=!strcmp(a[2],scenarioName(DebugScenario(i)).data);
#endif
      if (match) { app.debugRequest_={ActionKind::ScenarioStart,i}; out.println(F("SIM scenario queued; priming healthy devices first")); return true; }
    }
    out.println(F("ERR unknown scenario; use Tools > SIM scenarios or docs/TOUCH.md"));
  } else out.println(F("ui-debug status | scenario NAME | restart | reset | source/battery GROUP|all on/off | temperature OUT IN"));
#else
  (void)n; out.println(F("ERR PSU_UI_DEBUG disabled; real CAN build"));
#endif
  return true;
}
#endif
}
