// Real-controller regressions for the G2 bench trace: fresh CAN/AC, zero current,
// 0x0183 = 0x400/0x600, with an output-ready flag that drops while held at zero.
Configuration zeroHoldConfiguration(bool resume = false) {
  auto c = installation(1); c.groups[0].current = 500;
  c.autoResume = resume;
  if (resume) {
    c.operating.voltageAuthorized = true; c.operating.voltage = 5400;
    c.operating.voltageMask = c.operating.currentMask = 1; c.operating.current[0] = 500;
  }
  return c;
}
void zeroHoldReadiness() {
  Rig r(zeroHoldConfiguration(), true, false);
  r.reportAc = r.notReadyAtZero = true; r.temperature = 16;
  r.statusFlags = protocol::statusShutdown | protocol::statusFanMax;
  const auto started = r.now;
  r.finish();
  CHECK(r.now - started >= control::startupReadyMs);
  CHECK(r.c.unitControl(0).phase == ControlPhase::Held && r.c.unitControl(0).fault == CommandState::Idle);
  CHECK(r.c.unitControl(0).zeroConfirmed && r.c.unitControl(0).awakeConfirmed);
  CHECK(!r.c.deviceForSlot(0,r.now)->telemetry.ready && r.c.issue(0,r.now) == Issue::None);
  const auto first = r.can.writes().size();
  const auto heldSince = r.c.unitControl(0).since, coolSince = r.c.unitControl(0).coolSince;
  for (unsigned n=0; n<80; ++n) {
    r.notReadyAtZero = n % 2; r.statusFlags = n % 2 ? 0x600 : 0x400;
    r.step(1000);
    CHECK(r.c.unitControl(0).phase == ControlPhase::Held);
    CHECK(r.c.unitControl(0).since == heldSince && r.c.unitControl(0).coolSince == coolSince);
    CHECK(r.c.controlStatus(r.now).activity == Activity::Holding);
  }
  CHECK(r.c.unitControl(0).fanAcknowledged == FanMode::Auto && r.c.externalFanStatus(r.now).duty == 0);
  const auto writes = r.can.writes(); unsigned autoWrites = 0;
  for (size_t n=first; n<writes.size(); ++n) {
    const auto& f = writes[n];
    CHECK(f.data[1] != protocol::Standby);
    if (f.data[1] == protocol::FanMode) { CHECK(!f.data[3]); ++autoWrites; }
    if (f.data[1] == protocol::OnlineCurrent) CHECK(protocol::readBigEndian(f.data+4) == 0);
  }
  CHECK(autoWrites == 1);
  CHECK(r.c.setFanOverride(1,FanOverride::Auto,r.now) == Result::Ok);
  Terminal terminal(r);
  const auto diagnostic = terminal.input("diag control\n");
  CHECK(diagnostic.find("broadcast-ready=0 current-state=zero") != std::string::npos);
  CHECK(diagnostic.find("zero-confirmed=1 awake=1") != std::string::npos);
}
void zeroHoldOutputStart() {
  Rig r(zeroHoldConfiguration(),true,false);
  r.reportAc = r.notReadyAtZero = true; r.temperature = 16;
  r.finish(); r.step(35000);
  CHECK(r.c.setAutoResume(true) == Result::Ok);
  CHECK(r.c.preview(Operation::All,-1,r.now).blocker == Issue::None);
  const auto first = r.can.writes().size();
  r.outputReadyDelay = 5000;
  CHECK(r.c.queue(r.c.preview(Operation::All,-1,r.now),false,r.now) == Result::Ok);
  bool starting = false;
  for (unsigned n=0;n<60;++n) {
    r.step();
    starting |= r.c.controlStatus(r.now).activity == Activity::Starting;
    CHECK(r.c.unitControl(0).fault == CommandState::Idle);
  }
  CHECK(starting && r.c.report().succeeded == 1 && !r.c.report().failed);
  CHECK(r.c.unitControl(0).outputDemand == OutputDemand::Active);
  CHECK(r.c.controlStatus(r.now).activity == Activity::Running);
  CHECK(r.current[0] >= 490 && r.current[0] <= 500);
  const auto writes = r.can.writes();
  for (size_t n=first;n<writes.size();++n) {
    CHECK(writes[n].data[1] != protocol::Standby);
    if (writes[n].data[1] == protocol::OnlineCurrent)
      CHECK(protocol::readBigEndian(writes[n].data+4) == protocol::encodeCurrent(500,5000));
  }
  // Applying zero while enabled also retains its accepted zero state.
  CHECK(r.c.setCurrent(0,0) == Result::Ok); r.apply(Operation::GroupCurrent,0); r.step(1000);
  CHECK(r.c.unitControl(0).zeroConfirmed && r.current[0] == 0);
  CHECK(r.c.unitControl(0).outputDemand == OutputDemand::Zero);
  CHECK(!r.c.deviceForSlot(0,r.now)->telemetry.ready);
  CHECK(r.c.setAutoResume(false) == Result::Ok); r.finish();
  CHECK(r.c.controlStatus(r.now).activity == Activity::Holding);
  // A saved profile uses the same bounded zero-to-current transition.
  Rig resumed(zeroHoldConfiguration(true),true,false);
  resumed.reportAc = resumed.notReadyAtZero = true; resumed.outputReadyDelay = 5000;
  resumed.finish();
  CHECK(resumed.c.report().succeeded == 1 && resumed.c.unitControl(0).outputDemand == OutputDemand::Active);
  // OFF preempts an accepted current request whose power stage is still starting.
  Rig stopped(zeroHoldConfiguration(true),true,false);
  stopped.reportAc = stopped.notReadyAtZero = true; stopped.outputReadyDelay = 10000;
  for (unsigned n=0; n<200 && stopped.c.unitControl(0).outputDemand!=OutputDemand::Starting; ++n) stopped.step();
  CHECK(stopped.c.unitControl(0).outputDemand == OutputDemand::Starting);
  stopped.step(); // Accept the current ACK; output-ready remains pending.
  stopped.can.incoming.push_back(data(1,0x73,0)); stopped.c.tick(stopped.now);
  UiRig startingUi(stopped); UiFrame startingFrame;
  UiView::compose(startingFrame,startingUi.ui,stopped.c,stopped.now);
  CHECK(rowText(startingFrame,1).find("STARTING / VERIFYING PSU") != std::string::npos);
  CHECK(rowText(startingFrame,12).find("VERIFY") != std::string::npos);
  snapshot("control-output-starting",startingFrame);
  startingUi.click(); startingUi.click(); UiView::compose(startingFrame,startingUi.ui,stopped.c,stopped.now);
  CHECK(rowText(startingFrame,4).find("VERIFYING OUTPUT READY") != std::string::npos);
  CHECK(rowText(startingFrame,5).find("Current ACK received") != std::string::npos);
  CHECK(rowText(startingFrame,6).find("Ready 0/2000ms (0/3)") != std::string::npos);
  CHECK(rowText(startingFrame,8).find("Output may be live") != std::string::npos);
  snapshot("control-output-wait",startingFrame);
  const auto beforeOff = stopped.can.writes().size();
  CHECK(stopped.c.setAutoResume(false) == Result::Ok); stopped.step(30000);
  CHECK(stopped.c.controlStatus(stopped.now).activity == Activity::Holding);
  CHECK(stopped.c.unitControl(0).fault == CommandState::Idle && stopped.current[0] == 0);
  const auto offWrites = stopped.can.writes();
  for (size_t n=beforeOff; n<offWrites.size(); ++n) {
    CHECK(offWrites[n].data[1] != protocol::Standby);
    if (offWrites[n].data[1] == protocol::OnlineCurrent) CHECK(!protocol::readBigEndian(offWrites[n].data+4));
  }
}
void zeroHoldFailures() {
  // Idle handling requires fresh, understood status; it never bypasses an alarm.
  for (bool unknownAlarm : {false,true}) {
    Rig r(zeroHoldConfiguration(),true,false);
    r.notReadyAtZero = true; r.reportAc = unknownAlarm; r.statusFlags = uint32_t(1)<<7;
    r.finish(); CHECK(r.c.unitControl(0).fault == CommandState::ObservationTimeout);
    for (const auto& f:r.can.writes()) if(f.data[1] == protocol::OnlineCurrent) CHECK(!protocol::readBigEndian(f.data+4));
  }
  // A nonzero command can be accepted without the power stage becoming ready.
  // Bound that wait; periodic settings refresh must not extend the deadline.
  Rig failed(zeroHoldConfiguration(true),true,false);
  failed.reportAc = true; failed.notReadyUntil = failed.now + 100000;
  failed.step(45000);
  CHECK(failed.c.unitControl(0).fault == CommandState::ReadinessTimeout);
  CHECK(failed.c.unitControl(0).standbyConfirmed);
  UiRig faultUi(failed); faultUi.click(); faultUi.click(); UiFrame faultFrame;
  UiView::compose(faultFrame,faultUi.ui,failed.c,failed.now);
  CHECK(rowText(faultFrame,6).find("explicit reconcile") != std::string::npos);
  CHECK(rowText(faultFrame,7).find("required") != std::string::npos);
  snapshot("control-output-timeout",faultFrame);
  Terminal faultTerminal(failed);
  const auto diagnostic = faultTerminal.input("diag control\n");
  CHECK(diagnostic.find("LATCHED=output readiness timed out; explicit reconcile required") != std::string::npos);
  CHECK(diagnostic.find("current-state=fault") != std::string::npos);
  CHECK(faultTerminal.input("diag bus\n").find("output-overruns=0") != std::string::npos);
  faultTerminal.io.output.clear(); faultTerminal.io.throttled = true;
  faultTerminal.io.feed("diag control\n");
  for (unsigned n=0; n<1200; ++n) { faultTerminal.io.room = 3; faultTerminal.drain(1); }
  CHECK(faultTerminal.io.output.find("LATCHED=output readiness timed out; explicit reconcile required") != std::string::npos);
  CHECK(faultTerminal.io.output.find("current-state=fault") != std::string::npos);
  faultTerminal.io.throttled = false; faultTerminal.drain();
  CHECK(faultTerminal.input("diag bus\n").find("output-overruns=0") != std::string::npos);
  unsigned positives=0;
  for (const auto& f:failed.can.writes()) if(f.data[1] == protocol::OnlineCurrent && protocol::readBigEndian(f.data+4)) ++positives;
  CHECK(positives == 1);
  const auto stopped = failed.can.writes().size(); failed.step(30000);
  for (size_t n=stopped;n<failed.can.writes().size();++n) CHECK(failed.can.writes()[n].data[1] == protocol::FanMode);
  // The same failure trips a fitted AC cutoff and cannot restart on its own.
  Rig cutoff(zeroHoldConfiguration(true),true,false);
  cutoff.reportAc = true; cutoff.notReadyUntil = cutoff.now + 100000;
  cutoff.c.configureStartup(true,true,cutoff.now); cutoff.step(45000);
  CHECK(cutoff.c.unitControl(0).fault == CommandState::ReadinessTimeout);
  CHECK(cutoff.c.startupStatus().permit == PermitState::Fault && !cutoff.c.chargeAllowed());
  cutoff.step(30000);
  CHECK(cutoff.c.startupStatus().permit == PermitState::Fault && !cutoff.c.chargeAllowed());
  // Actual AC absence still invalidates reconciliation, even with live CAN.
  Rig ac(zeroHoldConfiguration(),true,false); ac.reportAc = ac.notReadyAtZero = true; ac.finish();
  ac.noAc = 1; ac.step(2000);
  CHECK(ac.c.availabilityReason(0,ac.now) == AvailabilityReason::NoAc && !ac.c.unitControl(0).awakeConfirmed);
  CHECK(ac.c.unitControl(0).fault == CommandState::Idle);
  ac.noAc = 0; ac.finish(); CHECK(ac.c.unitControl(0).phase == ControlPhase::Held);
  // Observed current at a confirmed zero limit still invokes the stop fallback.
  ac.leaking = true; ac.step();
  CHECK(ac.c.unitControl(0).fault == CommandState::Changed);
}
