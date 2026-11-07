// Exercise permission through the actual controller, including healthy peers
// that remain online while another unit fails. No measured-current shortcut.
void psuActiveLifecycle() {
  FakeCan can; PsuController empty(can);
  CHECK(!empty.psuActive(0));
  Rig boot(authorized(), true, false);
  CHECK(boot.c.enableControl().enabled() && !boot.c.psuActive(boot.now));
  boot.finish(); CHECK(boot.c.psuActive(boot.now));
  boot.reportedCurrent[0] = boot.reportedCurrent[1] = 0;
  boot.step(5000); CHECK(boot.c.psuActive(boot.now)); // Full battery/no load.
  CHECK(!boot.c.psuActive(boot.now + boot.c.staleMs() + 1));
  CHECK(boot.c.psuActive(boot.now)); // Query itself never changes state.

  Rig r; CHECK(r.c.enableControl().enabled() && !r.c.psuActive(r.now));
  r.c.setCurrent(0, 2000); CHECK(!r.c.psuActive(r.now));
  r.apply(); CHECK(r.c.psuActive(r.now));
  Terminal t(r);
  CHECK(t.input("diag control\n").find("PSU_ACTIVE pin=-1 physical=0 logical=HIGH") != std::string::npos);
  r.c.setCurrent(0, 0); CHECK(r.c.psuActive(r.now)); // Draft does not change permission.
  CHECK(r.c.queue(r.c.preview(Operation::GroupCurrent, 0, r.now), false, r.now) == Result::Ok);
  CHECK(!r.c.psuActive(r.now)); // New zero allocation, even before its ACK.
  r.finish(); CHECK(!r.c.psuActive(r.now));
  r.c.setCurrent(0, 2000); r.apply(Operation::GroupCurrent, 0);
  CHECK(r.c.psuActive(r.now));
  r.c.setAutoResume(false); CHECK(!r.c.psuActive(r.now));
  CHECK(r.current[0] > 0); // A LOW status is not physical stop confirmation.
  r.finish(); CHECK(!r.c.psuActive(r.now));
  CHECK(t.input("diag control\n").find("logical=LOW") != std::string::npos);
  r.c.setAutoResume(true); CHECK(!r.c.psuActive(r.now));
  r.finish(); CHECK(r.c.psuActive(r.now));

  r.c.sampleEnableInput(true, r.now); r.c.sampleEnableInput(true, r.now + 50);
  r.c.sampleEnableInput(false, r.now + 60); r.c.sampleEnableInput(false, r.now + 110);
  CHECK(!r.c.psuActive(r.now)); r.finish();
  r.c.sampleEnableInput(true, r.now); r.c.sampleEnableInput(true, r.now + 50);
  r.finish(); CHECK(r.c.psuActive(r.now));
  r.c.setAutoResume(false); r.c.sampleEnableInput(true, r.now + 100);
  CHECK(!r.c.psuActive(r.now)); // Held switch cannot undo serial OFF.

  Rig starting(zeroHoldConfiguration()); startOutput(starting);
  CHECK(starting.c.readinessStatus(0, starting.now).pending);
  CHECK(!starting.c.currentSynchronized(0) && starting.c.psuActive(starting.now));
  starting.step(control::startupReadyMs + 1000);
  CHECK(starting.c.unitControl(0).fault == CommandState::ReadinessTimeout);
  CHECK(!starting.c.psuActive(starting.now));
}
void psuActiveProtection() {
  Rig r; r.c.setCurrent(0, 4000); r.apply();
  r.temperature = 70; r.step(4000);
  CHECK(r.c.thermalStatus().percent == 30 && r.c.psuActive(r.now));
  r.temperature = 30; r.step(4000);
  r.noInputTemperature = 1; r.step(7000);
  CHECK(r.c.thermalStatus().sensorFaults == 1 && r.c.psuActive(r.now));
  r.noInputTemperature = 3; r.step(7000);
  CHECK(r.c.thermalStatus().sensorFaults == 3 && !r.c.psuActive(r.now));
  r.noInputTemperature = 0; r.step(5000);
  CHECK(!r.c.psuActive(r.now)); // Fresh temperatures alone do not clear a latch.
  CHECK(r.c.retryTemperature(1, r.now) == Result::Ok); r.step(7000);
  CHECK(r.c.thermalStatus().sensorFaults == 2 && r.c.psuActive(r.now));
  r.temperature = 76; r.step();
  CHECK(r.c.thermalStatus().stop == ThermalStop::Temperature && !r.c.psuActive(r.now));
  r.temperature = 30; r.step(5000); CHECK(!r.c.psuActive(r.now));

  Rig dropped; dropped.apply(); dropped.live = 1; dropped.step(7000);
  CHECK(dropped.c.psuActive(dropped.now)); // ANY, not an all-online status.
  dropped.live = 0; dropped.step(7000); CHECK(!dropped.c.psuActive(dropped.now));

  Rig ac; ac.apply(); ac.reportAc = true; ac.noAc = 2; ac.step(1000);
  CHECK(ac.c.psuActive(ac.now));
  ac.noAc = 3; ac.step(1000); CHECK(!ac.c.psuActive(ac.now));

  Rig identityChange(zeroHoldConfiguration()); identityChange.apply();
  identityChange.can.incoming.push_back(info(1, identity(9))); identityChange.c.tick(identityChange.now);
  CHECK(!identityChange.c.psuActive(identityChange.now));

  Rig cutoff(authorized(), true, false); cutoff.c.configureStartup(true, true, cutoff.now);
  CHECK(!cutoff.c.psuActive(cutoff.now)); cutoff.finish(); CHECK(cutoff.c.psuActive(cutoff.now));
  CHECK(cutoff.c.hardStop() == Result::Ok && !cutoff.c.psuActive(cutoff.now));
  cutoff.c.setAutoResume(true); cutoff.step(5000); CHECK(!cutoff.c.psuActive(cutoff.now));

  Rig failed; failed.apply(); failed.can.initOk = false;
  CHECK(!failed.c.begin() && !failed.c.psuActive(failed.now));
  Rig rejected; rejected.c.setCurrent(0, 2000); rejected.rejectRegister = protocol::OnlineCurrent;
  CHECK(rejected.c.queue(rejected.c.preview(Operation::All, -1, rejected.now), false, rejected.now) == Result::Ok);
  rejected.finish(); CHECK(rejected.c.report().failed && !rejected.c.psuActive(rejected.now));
}
void psuActiveAbsences() {
  auto config = authorized(); config.acceptedAbsent = 2;
  Rig partial(config, true, false); partial.live = 1;
  partial.step(20000); partial.finish();
  CHECK(partial.c.participants() == 1 && partial.c.psuActive(partial.now));

  // Only the absent/returning unit has a positive allocation. Its return alone
  // must not assert the output before inclusion and fresh reconciliation.
  config.operating.current[0] = 0;
  Rig excluded(config, true, false); excluded.live = 1;
  excluded.step(20000); excluded.finish(); CHECK(!excluded.c.psuActive(excluded.now));
  excluded.live = 3; excluded.step(20000); excluded.finish();
  CHECK(excluded.c.excluded() == 2 && !excluded.c.psuActive(excluded.now));
  FakeEeprom bytes; MemoryManager memory(bytes);
  const auto plan = excluded.c.previewAvailability(AvailabilityAction::Include, 2, false, excluded.now);
  CHECK(plan.blocker == Issue::None && excluded.c.confirmAvailability(plan, memory, excluded.now) == Result::Ok);
  excluded.finish(); CHECK(excluded.c.psuActive(excluded.now));
}
