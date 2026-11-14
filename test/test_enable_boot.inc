// Exercise the same configure(..., resumeSaved, useSwitchAtBoot) entry used by
// Application, including saved ON plus the real OFF/AC-loss/reset sequence.
Configuration enableBootConfiguration(bool savedOn) {
  auto cfg = installation(9); cfg.autoResume = savedOn;
  const char* names[] = {"ONE", "TWO", "THREE"};
  for (uint8_t g = 0; g < 3; ++g) {
    std::strcpy(cfg.groups[g].name, names[g]); cfg.groups[g].members = UnitMask(7U << (3 * g));
  }
  for (uint8_t i = 0; i < cfg.count; ++i) {
    cfg.voltage[i] = cfg.operating.voltage[i] = 5400;
    cfg.current[i] = cfg.operating.current[i] = 100;
  }
  cfg.operating.voltageMask = cfg.operating.currentMask = membersMask(cfg.count);
  CHECK(validConfig(cfg)); return cfg;
}
void enableBootZeroWrites(const Rig& r) {
  for (const auto& f : r.can.sent)
    if (protocol::command(f.id) == protocol::setCommand && f.data[1] == protocol::OnlineCurrent)
      CHECK(protocol::readBigEndian(f.data + 4) == 0);
}
void enableBootSample(Rig& r, bool on) {
  r.c.sampleEnableInput(on, r.now);
  r.step(control::enableDebounceMs);
  r.c.sampleEnableInput(on, r.now);
}
void enableBootPolicy() {
  for (const bool savedOn : {false, true}) for (const bool switchOn : {false, true}) {
    for (const uint32_t start : {0U, UINT32_MAX - 20}) {
      EnableControl e; e.boot(savedOn, true); CHECK(!e.enabled());
      CHECK(!e.sample(switchOn, start));
      CHECK(!e.sample(switchOn, start + 49) && !e.enabled());
      CHECK(e.sample(switchOn, start + 50));
      CHECK(e.settled() && e.enabled() == switchOn && e.source() == EnableSource::Switch);
      CHECK(!e.sample(switchOn, start + 500));
    }
    for (const bool cutoff : {false, true}) {
      Rig r(enableBootConfiguration(savedOn), true, false, 100, true);
      r.reportAc = true; r.temperature = 30; r.c.configureStartup(cutoff, true, r.now);
      // Even fully reconciled, already-live PSUs cannot get positive current
      // while the declared boot input has not supplied a settled decision.
      r.finish(); CHECK(!r.c.enableControl().enabled() && !r.c.psuActive(r.now));
      enableBootZeroWrites(r);
      r.c.sampleEnableInput(switchOn, r.now);
      r.step(49); r.c.sampleEnableInput(switchOn, r.now);
      CHECK(!r.c.enableControl().enabled()); enableBootZeroWrites(r);
      r.step(1); r.c.sampleEnableInput(switchOn, r.now); r.finish();
      CHECK(r.c.enableControl().enabled() == switchOn);
      CHECK(r.c.enableControl().source() == EnableSource::Switch);
      CHECK(r.c.controlStatus(r.now).activity == (switchOn ? Activity::Running : Activity::Holding));
      CHECK(r.c.psuActive(r.now) == switchOn);
      for (uint8_t i = 0; i < r.c.count(); ++i) {
        CHECK((r.current[i] > 0) == switchOn);
        CHECK(r.c.configuration().operating.current[i] == 100);
      }
      if (!switchOn) enableBootZeroWrites(r);
    }
  }
  // No physical switch: the saved preference still owns boot.
  for (const bool savedOn : {false, true}) {
    Rig r(enableBootConfiguration(savedOn), true, false); r.finish();
    CHECK(r.c.enableControl().enabled() == savedOn && r.c.enableControl().source() == EnableSource::Boot);
    CHECK(!r.c.enableControl().present() && (r.current[0] > 0) == savedOn);
  }
  // Continuous bounce cannot resolve the initial decision, even with saved ON.
  Rig bouncing(enableBootConfiguration(true), true, false, 100, true);
  for (uint8_t n = 0; n < 40; ++n) {
    bouncing.c.sampleEnableInput(n & 1, bouncing.now); bouncing.step(25);
    CHECK(!bouncing.c.enableControl().settled() && !bouncing.c.enableControl().enabled());
  }
  enableBootZeroWrites(bouncing); enableBootSample(bouncing, true); bouncing.finish();
  CHECK(bouncing.c.enableControl().enabled());
  // An explicit serial/UI request is itself a decision. Its priority survives
  // completion of the initial switch debounce; only a later edge overrides it.
  for (const bool commandOn : {false, true}) {
    Rig r(enableBootConfiguration(true), true, false, 100, true);
    r.c.sampleEnableInput(!commandOn, r.now); r.step(25);
    CHECK(r.c.setAutoResume(commandOn) == Result::Ok);
    r.step(25); r.c.sampleEnableInput(!commandOn, r.now); r.finish();
    CHECK(r.c.enableControl().enabled() == commandOn && r.c.enableControl().source() == EnableSource::Serial);
    r.step(1000); r.c.sampleEnableInput(!commandOn, r.now);
    CHECK(r.c.enableControl().enabled() == commandOn);
    enableBootSample(r, commandOn);
    CHECK(r.c.enableControl().source() == EnableSource::Switch);
    enableBootSample(r, !commandOn); r.finish();
    CHECK(r.c.enableControl().enabled() == !commandOn);
  }
}
void enableBootReboot() {
  FakeEeprom bytes; MemoryManager memory(bytes); const auto cfg = enableBootConfiguration(true);
  CHECK(memory.save(cfg) == StorageResult::Ok);
  Rig before(cfg, true, false, 100, true); before.reportAc = true;
  enableBootSample(before, false); before.finish();
  enableBootSample(before, true); before.finish(); before.step(60000);
  CHECK(before.c.enableControl().enabled() && before.current[0] > 0);
  enableBootSample(before, false); before.finish();
  CHECK(!before.c.enableControl().enabled() && !before.c.configuration().autoResume);
  for (uint8_t i = 0; i < before.c.count(); ++i) CHECK(before.current[i] == 0);
  before.noAc = membersMask(before.c.count()); before.step(1000); before.live = 0; before.step(5000);
  Configuration saved; CHECK(memory.load(saved) == StorageResult::Ok && saved.autoResume);
  // Both simultaneous AC/reset and delayed AC. No UI edit or EEPROM save was
  // needed to reproduce the old bug; the earlier saved ON survives the OFF.
  for (const bool delayedAc : {false, true}) {
    Rig reboot(saved, true, false, 100, true); reboot.reportAc = true;
    reboot.c.configureStartup(true, true, reboot.now);
    if (delayedAc) reboot.live = 0;
    enableBootSample(reboot, false); reboot.step(delayedAc ? 10000 : 1000);
    reboot.live = membersMask(reboot.c.count());
    // finish() treats AwaitIdentity as idle; supply the two fresh INFO replies
    // required after a real absence before waiting on reconciliation.
    reboot.step(); reboot.step(); reboot.finish();
    CHECK(!reboot.c.enableControl().enabled() && reboot.c.enableControl().source() == EnableSource::Switch);
    CHECK(reboot.c.controlStatus(reboot.now).activity == Activity::Holding);
    CHECK(!reboot.c.psuActive(reboot.now)); enableBootZeroWrites(reboot);
    // A new ON works, but a runtime load or retry cannot replay a held ON.
    enableBootSample(reboot, true); reboot.finish(); CHECK(reboot.current[0] > 0);
    CHECK(reboot.c.configure(saved) == Result::Ok);
    reboot.c.sampleEnableInput(true, reboot.now); reboot.finish();
    CHECK(!reboot.c.enableControl().enabled());
    reboot.c.reconcile(); reboot.c.sampleEnableInput(true, reboot.now); reboot.finish();
    CHECK(!reboot.c.enableControl().enabled() && reboot.current[0] == 0);
    enableBootSample(reboot, false); enableBootSample(reboot, true); reboot.finish();
    CHECK(reboot.c.enableControl().enabled() && reboot.current[0] > 0);
  }
  CHECK(memory.load(saved) == StorageResult::Ok && saved.autoResume);
}
void enableBootProtection() {
  auto cfg = enableBootConfiguration(false); cfg.thermalStop = ThermalStop::Temperature;
  FakeEeprom bytes; MemoryManager memory(bytes); CHECK(memory.save(cfg) == StorageResult::Ok);
  Rig r(cfg, true, false, 100, true); r.temperature = 30;
  enableBootSample(r, true); r.finish(); serviceThermal(r, memory);
  CHECK(r.c.thermalStatus().cool && !r.c.enableControl().enabled());
  CHECK(r.c.configuration().thermalStop == ThermalStop::Temperature);
  CHECK(r.c.enableControl().source() == EnableSource::Protection && !r.c.thermalStatus().resuming);
  enableBootZeroWrites(r);
  // Settled ON and ordinary reconciliation do not count as a fresh ON action.
  r.c.reconcile(); r.c.sampleEnableInput(true, r.now); r.finish();
  CHECK(!r.c.enableControl().enabled()); enableBootZeroWrites(r);
  enableBootSample(r, false); enableBootSample(r, true);
  CHECK(r.c.thermalStatus().resuming && !r.c.enableControl().enabled());
  r.c.serviceProtection(memory, r.now); CHECK(memory.saving());
  for (unsigned n = 0; n < 60; ++n) memory.stepSave();
  enableBootSample(r, false); serviceThermal(r, memory);
  CHECK(!r.c.enableControl().enabled() && r.c.configuration().thermalStop != ThermalStop::None);
  enableBootZeroWrites(r); // Newer OFF cancelled the pending clear/resume.
  enableBootSample(r, true); CHECK(!r.c.enableControl().enabled());
  serviceThermal(r, memory); r.finish();
  CHECK(r.c.enableControl().enabled() && r.c.configuration().thermalStop == ThermalStop::None);
  CHECK(r.current[0] > 0);
}
