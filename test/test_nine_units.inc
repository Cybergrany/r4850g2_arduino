// Capacity regressions exercise bit 8 through the production controller/UI and
// migrate real historical layouts, rather than relabelling a new-schema record.
void checkSameProfile(const Configuration& actual, const Configuration& expected) {
  CHECK(actual.deploymentId == expected.deploymentId && actual.count == expected.count);
  CHECK(actual.voltage == expected.voltage && actual.offlineVoltage == expected.offlineVoltage && actual.pollMs == expected.pollMs);
  CHECK(actual.autoResume == expected.autoResume && actual.thermalStop == expected.thermalStop && actual.acceptedAbsent == expected.acceptedAbsent);
  CHECK(actual.operating.voltage == expected.operating.voltage && actual.operating.voltageAuthorized == expected.operating.voltageAuthorized);
  CHECK(actual.operating.voltageMask == expected.operating.voltageMask && actual.operating.currentMask == expected.operating.currentMask);
  for (uint8_t i = 0; i < PSU_MAX_UNITS; ++i) {
    CHECK(sameIdentity(actual.units[i].identity, expected.units[i].identity));
    CHECK(actual.units[i].ratedCurrent == expected.units[i].ratedCurrent && actual.operating.current[i] == expected.operating.current[i]);
  }
  for (uint8_t i = 0; i < limits::maxGroups; ++i) {
    CHECK(std::strcmp(actual.groups[i].name, expected.groups[i].name) == 0);
    CHECK(actual.groups[i].members == expected.groups[i].members && actual.groups[i].current == expected.groups[i].current);
    CHECK(actual.groups[i].offlineCurrent == expected.groups[i].offlineCurrent);
  }
}
void nineUnitMigration() {
  CHECK(PSU_MAX_UNITS == 9 && limits::maxGroups == 8 && Discovery::capacity == 12);
  CHECK(MemoryManager::recordSize == 211 && touch::recordAddress[0] == 216 && touch::recordAddress[1] == 472);
  auto old = installation(8); old.autoResume = true; old.acceptedAbsent = 0x80;
  old.operating.voltageAuthorized = true; old.operating.voltage = 5510;
  old.operating.voltageMask = old.operating.currentMask = 0xff;
  old.voltage = 5625; old.offlineVoltage = 5310; old.pollMs = 1234;
  const char* names[] = {"AbCdEfGh", "IjKlMnOp", "QrStUvWx", "Yz_01234", "a56789_Z", "BCDEFGHI", "JKLMNOPQ", "RSTUVWXY"};
  for (uint8_t i = 0; i < 8; ++i) {
    std::strcpy(old.groups[i].name, names[i]); old.groups[i].members = 1U << i;
    old.groups[i].current = 2000 + i; old.groups[i].offlineCurrent = 1000 + i;
    old.operating.current[i] = 700 + i; old.units[i].ratedCurrent = 5000 + i;
  }
  CHECK(validConfig(old));
  for (uint8_t schema = 2; schema <= 4; ++schema) {
    auto expected = old; if (schema == 2) expected.acceptedAbsent = 0;
    auto bytes = parallelRecord(old, schema); MemoryManager memory(bytes); Configuration loaded;
    const auto before = bytes.bytes;
    CHECK(memory.load(loaded) == StorageResult::Ok); checkSameProfile(loaded, expected);
    CHECK(bytes.bytes == before && loaded.count == 8 && !identified(loaded.units[8].identity));
    CHECK(memory.save(loaded) == StorageResult::Ok && bytes.bytes[257] == 5);
    CHECK(memory.load(loaded) == StorageResult::Ok); checkSameProfile(loaded, expected);
  }
  // A smaller historical compiled capacity also migrates without using the new
  // compiled template for bindings, groups, authorizations or the extra slots.
  auto two = installation(2); auto small = parallelRecord(two, 4, 2); MemoryManager smallMemory(small); Configuration loaded;
  CHECK(smallMemory.load(loaded) == StorageResult::Ok); checkSameProfile(loaded, two);
  // Exercise the entire six-bit alphabet in names, including short/empty names.
  FakeEeprom namesBytes; MemoryManager namesMemory(namesBytes);
  const std::string alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_";
  for (const char ch : alphabet) {
    auto cfg = installation(9); std::strcpy(cfg.groups[0].name, "Test_0"); cfg.groups[0].name[6] = ch;
    CHECK(namesMemory.save(cfg) == StorageResult::Ok && namesMemory.load(loaded) == StorageResult::Ok);
    checkSameProfile(loaded, cfg);
  }
  for (const auto stop : {ThermalStop::None, ThermalStop::Temperature, ThermalStop::Duration}) {
    auto baseline = old; baseline.thermalStop = stop; baseline.autoResume = stop == ThermalStop::None;
    auto bytes = parallelRecord(baseline); MemoryManager memory(bytes);
    TouchCalibration calibration; calibration.valid = true;
    calibration.coefficient[0] = calibration.coefficient[4] = 65536;
    CalibrationStore store(bytes, memory); CHECK(store.start(calibration) == StorageResult::Ok);
    while (store.saving()) store.step();
    CHECK(store.result() == StorageResult::Ok);
    const auto preserved = bytes.bytes;
    auto nine = baseline; nine.count = 9; nine.units[8].identity = identity(9);
    nine.groups[7].members |= 0x100; nine.acceptedAbsent |= 0x100;
    nine.operating.voltageMask |= 0x100; nine.operating.currentMask |= 0x100; nine.operating.current[8] = 222;
    CHECK(validConfig(nine));
    for (int cut = 0; cut <= MemoryManager::recordSize + 1; ++cut) {
      bytes.bytes = preserved; bytes.calls = 0; bytes.stopAfter = cut;
      try { MemoryManager writing(bytes); writing.save(nine); } catch (const PowerCut&) {}
      bytes.stopAfter = -1; MemoryManager reading(bytes);
      CHECK(reading.load(loaded) == StorageResult::Ok);
      checkSameProfile(loaded, cut <= MemoryManager::recordSize ? baseline : nine);
      for (const auto base : touch::recordAddress) for (uint16_t i = 0; i < touch::recordBytes; ++i)
        CHECK(bytes.bytes[base + i] == preserved[base + i]);
      TouchCalibration recovered; CalibrationStore reread(bytes, reading);
      CHECK(reread.load(recovered) && recovered.valid && recovered.coefficient[0] == 65536);
    }
    // New schema wins by generation; damage to its payload recovers the old
    // complete record, including a thermal stop. A second save replaces old v4.
    bytes.bytes[256 + 36] ^= 1;
    CHECK(memory.load(loaded) == StorageResult::Ok); checkSameProfile(loaded, baseline);
    CHECK(memory.save(nine) == StorageResult::Ok && memory.save(nine) == StorageResult::Ok);
    CHECK(bytes.bytes[1] == 5 && bytes.bytes[257] == 5);
    CHECK(memory.load(loaded) == StorageResult::Ok); checkSameProfile(loaded, nine);
    if (stop != ThermalStop::None) {
      FakeCan can; PsuController reboot(can); CHECK(reboot.configure(loaded, true) == Result::Ok);
      reboot.sampleEnableInput(true, 0); reboot.sampleEnableInput(true, 100);
      CHECK(!reboot.enableControl().enabled() && reboot.configuration().thermalStop == stop);
    }
  }
  // Semantic validation still applies to a CRC-valid, compact record.
  FakeEeprom corrupt; MemoryManager memory(corrupt); auto nine = installation(9);
  CHECK(memory.save(nine) == StorageResult::Ok); const auto valid = corrupt.bytes;
  const auto rejects = [&](uint16_t offset, uint8_t value) {
    corrupt.bytes = valid; corrupt.bytes[offset] = value;
    recordCrc(corrupt.bytes.data(), MemoryManager::recordSize);
    auto unchanged = old; CHECK(memory.load(unchanged) == StorageResult::NoValidRecord); checkSameProfile(unchanged, old);
  };
  rejects(15, 10); // Unsupported encoded capacity.
  rejects(26, 10); // Active count exceeds encoded slots.
  rejects(206, 9); // Group indexes are 0..8.
  rejects(210, 0xf1); // Unused high nibble must be zero.
  rejects(29, 2); // Bit 9 in the absence mask is outside the configured slots.
  rejects(14, 2); // Non-boolean authorization flag.
}
Configuration ninthGroup() {
  auto cfg = installation(9); cfg.groups[0].members = 0xff; cfg.groups[0].current = 8000;
  std::strcpy(cfg.groups[1].name, "NINTH"); cfg.groups[1].members = 0x100; cfg.groups[1].current = 1000;
  CHECK(validConfig(cfg)); return cfg;
}
void ninthProtectionAndControl() {
  Rig r(ninthGroup()); r.temperature = 30; r.apply();
  CHECK(r.c.report().succeeded == 0x1ff && r.c.currentSynchronized(8) && r.current[8] > 0);
  CHECK(r.c.setCount(10) == Result::Invalid && r.c.setGroup("BAD", 0x200) == Result::Invalid);
  uint16_t acknowledged;
  CHECK(r.c.acknowledgedCurrent(8, acknowledged) && acknowledged == protocol::encodeCurrent(1000, 5000));
  r.unitTemperature[8] = 70; r.step(12000);
  CHECK(r.c.thermalStatus().percent == 30 && r.c.thermalStatus().contributors == 0x100);
  for (uint8_t i = 0; i < 9; ++i) {
    CHECK(r.c.effectiveCurrent(i) == 300 && r.c.configuration().operating.current[i] == 1000);
    CHECK(r.c.acknowledgedCurrent(i, acknowledged) && acknowledged == protocol::encodeCurrent(300, 5000));
  }
  r.unitTemperature[8] = 30; r.step(14000);
  CHECK(r.c.thermalStatus().percent == 100 && r.c.effectiveCurrent(8) == 1000);
  r.noInputTemperature = 0x100; r.step(8000); r.finish();
  CHECK(r.c.thermalStatus().sensorFaults == 0x100 && r.current[8] == 0);
  for (uint8_t i = 0; i < 8; ++i) CHECK(r.current[i] > 0 && r.c.effectiveCurrent(i) == 1000);
  r.noInputTemperature = 0; r.step(1000);
  CHECK(r.c.thermalStatus().sensorFaults == 0x100 && r.current[8] == 0);
  CHECK(r.c.retryTemperature(0x100, r.now) == Result::Ok); r.finish(); r.step(6000);
  CHECK(!r.c.thermalStatus().sensorFaults && r.current[8] > 0);
  // The active output must also recognize the ninth PSU as the sole producer.
  CHECK(r.c.setCurrent(0, 0) == Result::Ok); r.apply();
  for (uint8_t i = 0; i < 8; ++i) CHECK(r.current[i] == 0);
  CHECK(r.current[8] > 0 && r.c.psuActive(r.now));
  CHECK(r.c.setAutoResume(false) == Result::Ok); r.finish();
  CHECK(!r.c.psuActive(r.now));
  for (uint8_t i = 0; i < 9; ++i) CHECK(r.current[i] == 0 && r.c.unitControl(i).zeroConfirmed);
  // A ninth-unit hot stop must persist while preserving unsaved drafts.
  FakeEeprom bytes; MemoryManager memory(bytes); CHECK(memory.save(r.c.configuration()) == StorageResult::Ok);
  CHECK(r.c.setVoltage(5700) == Result::Ok); r.unitTemperature[8] = 76; r.step(1000); serviceThermal(r, memory);
  Configuration saved; CHECK(memory.load(saved) == StorageResult::Ok);
  CHECK(saved.thermalStop == ThermalStop::Temperature && !saved.autoResume && saved.voltage != 5700);
  CHECK(r.c.configuration().voltage == 5700 && r.c.thermalStatus().contributors == 0x100);
  // A failed group apply reports only bit 8; no truncated success/failed mask.
  Rig rejected(ninthGroup()); rejected.rejectRegister = protocol::OnlineCurrent;
  auto plan = rejected.c.preview(Operation::GroupCurrent, 1, rejected.now);
  CHECK(plan.recipients == 0x100 && rejected.c.queue(plan, false, rejected.now) == Result::Ok); rejected.finish();
  CHECK(rejected.c.report().failed == 0x100 && rejected.c.report().succeeded == 0);
}
void ninthAvailabilityAndRouting() {
  Rig r(ninthGroup()); r.temperature = 30; r.apply(); r.live = 0xff; r.step(16000);
  CHECK(r.c.alerts().at(8).code == AlertCode::ActiveLost && r.c.availabilityAttention(r.now) == 0x100);
  r.c.alerts().acknowledgeAll(); CHECK(!(r.c.acceptedAbsences() & 0x100));
  FakeEeprom bytes; MemoryManager memory(bytes);
  auto plan = r.c.previewAvailability(AvailabilityAction::Accept, 0x100, true, r.now);
  CHECK(plan.selected == 0x100 && plan.blocker == Issue::None);
  CHECK(r.c.confirmAvailability(plan, memory, r.now) == Result::Ok);
  for (unsigned i = 0; i < 300; ++i) { memory.stepSave(); r.step(20); }
  CHECK(r.c.acceptedAbsences() == 0x100 && r.c.participants() == 0xff);
  CHECK(r.c.alerts().at(8).code == AlertCode::Excluded);
  Configuration saved; CHECK(memory.load(saved) == StorageResult::Ok && saved.acceptedAbsent == 0x100);
  r.live = allUnitsMask; r.step(16000); r.finish();
  CHECK(r.c.participants() == 0xff && r.current[8] == 0 && r.c.alerts().at(8).code == AlertCode::Returned);
  plan = r.c.previewAvailability(AvailabilityAction::Include, 0x100, false, r.now);
  CHECK(plan.selected == 0x100 && plan.blocker == Issue::None && r.c.confirmAvailability(plan, memory, r.now) == Result::Ok);
  r.finish(); CHECK(r.c.participants() == 0x1ff && r.current[8] > 0);
  // Address movement retains slot 9's identity/group; stale plans cannot write
  // to an old route or a replacement that happens to use that address.
  const auto stale = r.c.preview(Operation::GroupCurrent, 1, r.now);
  r.can.incoming.push_back(info(9, identity(99))); r.c.tick(r.now + 1);
  CHECK(r.c.queue(stale, false, r.now + 1) == Result::Changed);
  r.addresses[8] = 37; r.step(16000); r.finish();
  CHECK(r.c.deviceForSlot(8, r.now) && r.c.deviceForSlot(8, r.now)->address == 37);
  CHECK(r.c.configuration().groups[1].members == 0x100);
}
void ninthSerialAndUi() {
  Rig r(installation(9), false, false); r.temperature = 30; r.finish(); Terminal t(r);
  CHECK(t.input("count 9\n").find("OK") != std::string::npos);
  CHECK(t.input("diag psu 9\n").find("PSU 9 id=") != std::string::npos);
  CHECK(t.input("status all\n").find("PSU 9 id=") != std::string::npos);
  CHECK(t.input("telemetry all\n").find("PSU 9 output-temperature") != std::string::npos);
  CHECK(t.input("fan 8-9 max\n").find("slots=8,9") != std::string::npos);
  r.step(4000); CHECK(r.c.unitControl(8).fanAcknowledged == FanMode::Max);
  CHECK(t.input("fan 9 policy\n").find("Normal policy restored") != std::string::npos);
  CHECK(t.input("poll GROUP1\n").find("OK") != std::string::npos);
  // Ninth member is on the fifth page, with distinct rank, data and recovery.
  UiRig ui(r); ui.click(); ui.turn(8);
  CHECK(ui.ui.unit() == 8 && ui.ui.unitRank() == 8 && ui.ui.unitCount() == 9);
  UiFrame frame; UiView::compose(frame, ui.ui, r.c, r.now);
  CHECK(rowText(frame, 2).find("PSU9") != std::string::npos); snapshot("units-nine-page5", frame);
  CHECK(t.input("group set GROUP1 1-8\n").find("OK") != std::string::npos);
  CHECK(t.input("group set LAST 9\n").find("OK") != std::string::npos);
  CHECK(r.c.configuration().groups[1].members == 0x100);
  CHECK(t.input("set current LAST 10\n").find("across slots 9") != std::string::npos);
  CHECK(t.input("save\n").find("EEPROM OK") != std::string::npos);
  Configuration loaded; CHECK(t.memory.load(loaded) == StorageResult::Ok && loaded.groups[1].members == 0x100);
  CHECK(t.input("count 10\n").find("ERR") != std::string::npos);
}
