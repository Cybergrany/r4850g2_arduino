#include "../util/FlashString.h"
#include "Application.h"
#include <Arduino.h>
#include <string.h>
#include <new>

namespace psu {
Application::Application() : controller_(can_), memory_(eeprom_)
#if PSU_ENABLE_SERIAL
  , console_(Serial, controller_, memory_)
#endif
#if PSU_ENABLE_DISPLAY
  , localUi_(controller_, memory_, display_, eeprom_)
#endif
{}
void Application::begin() {
#if PSU_CHG_ALLOW_PIN >= 0 && !PSU_UI_DEBUG
  chargeAllow_.begin(); // First application action; reset-time LOW is hardware-owned.
#endif
  static_assert(board::eepromBudget == MemoryManager::budget, "EEPROM budgets must agree");
#if PSU_FAN_PWM_PIN >= 0 && !PSU_UI_DEBUG
  fanOutput_.begin();
#endif
#if PSU_UI_DEBUG
  can_.reset();
  Configuration config = debugConfiguration();
#else
  Configuration config = defaultConfiguration();
#endif
  const StorageResult loaded = memory_.load(config);
  controller_.configure(config, PSU_UI_DEBUG || loaded == StorageResult::Ok);
  controller_.configureStartup(PSU_CHG_ALLOW_PIN >= 0 || PSU_UI_DEBUG, PSU_UI_DEBUG || loaded == StorageResult::Ok, millis());
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
  memory_.stepSave();
#if PSU_ENABLE_DISPLAY
  localUi_.storageTick();
#endif
#if PSU_ENABLE_SERIAL
  console_.tick(millis());
#endif
#if PSU_ENABLE_DISPLAY
  localUi_.tick(millis());
#if PSU_UI_DEBUG
  const auto request = localUi_.takeRequest();
  if (request.kind != ActionKind::None) debugRequest_ = request;
#endif
#endif
#if PSU_CHG_ALLOW_PIN >= 0 && !PSU_UI_DEBUG
  chargeAllow_.write(controller_.chargeAllowed()); // Includes serial and local UI hard stops.
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
  if (priming_ && !controller_.busy() && controller_.report().succeeded == membersMask(controller_.count())) {
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
      auto cfg=controller_.configuration(); cfg.voltage+=10;
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
  if (app.localUi_.command(n,a,out,now)) return true;
#endif
  if (PSU_STRCMP(a[0], "ui-debug")) return false;
#if PSU_UI_DEBUG
  if (n==4 && (!PSU_STRCMP(a[1], "source") || !PSU_STRCMP(a[1], "battery")) && (!PSU_STRCMP(a[3], "on") || !PSU_STRCMP(a[3], "off"))) {
    const int8_t group = app.controller_.groupIndex(a[2]);
    const uint8_t mask = !PSU_STRCMP(a[2], "all") ? membersMask(app.controller_.count()) : group >= 0 ? app.controller_.configuration().groups[group].members : 0;
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
  } else out.println(F("ui-debug status | scenario NAME | restart | reset | source/battery GROUP|all on/off"));
#else
  (void)n; out.println(F("ERR PSU_UI_DEBUG disabled; real CAN build"));
#endif
  return true;
}
#endif
}
