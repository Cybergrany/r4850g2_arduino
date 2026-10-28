// Exercise actual frames and the production controller, not a readiness model.
void startOutput(Rig& r) {
  r.reportAc = true; r.notReady = membersMask(r.c.count()); r.step();
  CHECK(r.c.queue(r.c.preview(Operation::GroupCurrent,0,r.now),false,r.now) == Result::Ok);
  CHECK(r.c.controlStatus(r.now).activity != Activity::Running);
  for (unsigned n=0; n<60 && r.c.unitControl(0).outputDemand!=OutputDemand::Starting; ++n) r.step();
  CHECK(r.c.unitControl(0).outputDemand == OutputDemand::Starting);
  CHECK(!r.c.currentSynchronized(0));
  CHECK(r.c.unitControl(0).outputSince == r.now);
}
void pendingReadiness(const Rig& r, uint32_t since, uint8_t slot = 0) {
  CHECK(r.c.unitControl(slot).phase == ControlPhase::Online);
  CHECK(r.c.unitControl(slot).fault == CommandState::Idle);
  CHECK(r.c.unitControl(slot).outputDemand == OutputDemand::Starting);
  CHECK(r.c.unitControl(slot).outputSince == since);
  CHECK(r.c.controlStatus(r.now).activity == Activity::Starting);
  CHECK(!(r.c.excluded() & (1U<<slot)));
}
void readinessQualification() {
  Rig r(zeroHoldConfiguration()); startOutput(r); const auto since = r.now;
  r.step(); CHECK(r.c.currentSynchronized(0));
  const auto writes = r.can.writes().size();
  r.notReady = 0; r.step(125);
  CHECK(r.c.readinessStatus(0,r.now).frames == 1);
  CHECK(r.c.readinessStatus(0,r.now).stableMs == 0);
  pendingReadiness(r,since);
  // This is the bench-shaped pulse that previously promoted Active and then
  // re-zeroed/re-woke the PSU roughly 0.6 seconds after the first write.
  r.notReady = 1; r.step(250);
  pendingReadiness(r,since); CHECK(r.c.readinessStatus(0,r.now).frames == 0);
  for (unsigned n=0; n<8; ++n) { r.notReady = n%2; r.step(250); pendingReadiness(r,since); }
  for (size_t n=writes; n<r.can.writes().size(); ++n) CHECK(r.can.writes()[n].data[1] == protocol::FanMode);
  r.notReady = 0; r.voltage = 5263; r.step();
  for (unsigned n=0; n<7; ++n) { r.step(); pendingReadiness(r,since); }
  CHECK(r.c.readinessStatus(0,r.now).stableMs == 1750);
  Terminal t(r); const auto diagnostics = t.input("diag control\n");
  CHECK(diagnostics.find("Current accepted; verifying readiness") != std::string::npos);
  CHECK(diagnostics.find("ready-stable=1750/2000ms frames=3/3") != std::string::npos);
  CHECK(diagnostics.find("startup-remaining=") != std::string::npos);
  CHECK(t.input("diag bus\n").find("output-overruns=0") != std::string::npos);
  t.io.output.clear(); t.io.throttled = true; t.io.feed("diag psu 1\n");
  for(unsigned n=0;n<1200;++n) { t.io.room = 3; t.drain(1); }
  CHECK(t.io.output.find("ready-stable=1750/2000ms frames=3/3") != std::string::npos);
  t.io.throttled = false; t.drain(); CHECK(t.input("diag bus\n").find("output-overruns=0") != std::string::npos);
  r.can.incoming.push_back(data(1,0x73,250*1024)); r.c.tick(r.now);
  UiRig ui(r); ui.click(); ui.click(); UiFrame frame; UiView::compose(frame,ui.ui,r.c,r.now);
  CHECK(rowText(frame,1).find("STARTING") != std::string::npos);
  CHECK(rowText(frame,6).find("Ready 1750/2000ms (3/3)") != std::string::npos);
  snapshot("control-output-qualifying",frame);
  r.step(); CHECK(r.c.unitControl(0).outputDemand == OutputDemand::Active);
  CHECK(r.c.controlStatus(r.now).activity == Activity::Running);
  CHECK(r.c.configuration().operating.current[0] == 500);
  // Voltage/current need not equal their limits (CC charging or no load).
  float volts; CHECK(r.c.freshMetric(0,protocol::OutputVoltage,volts,r.now)); close(volts,52.63f,0.01f);
  r.notReady = 1; r.step(); // An actual readiness loss after qualification still reconciles.
  CHECK(r.c.unitControl(0).phase != ControlPhase::Online);
}
void readinessFrameOrdering() {
  Rig r(zeroHoldConfiguration()); startOutput(r); r.step(); const auto since = r.c.unitControl(0).outputSince;
  r.noBroadcast = 1;
  for (unsigned n=0; n<3; ++n) r.can.incoming.push_back(broadcast(1));
  r.c.tick(r.now);
  CHECK(r.c.readinessStatus(0,r.now).frames == 3 && r.c.readinessStatus(0,r.now).stableMs == 0);
  for (unsigned n=0; n<10; ++n) r.c.tick(r.now); // Re-reading a snapshot is not a frame.
  pendingReadiness(r,since);
  r.step(1000); CHECK(r.c.readinessStatus(0,r.now).stableMs == 0);
  r.can.incoming.push_back(broadcast(1)); r.c.tick(r.now);
  CHECK(r.c.readinessStatus(0,r.now).stableMs == 1000);
  r.step(1000);
  r.can.incoming.push_back(broadcast(1));
  r.can.incoming.push_back(broadcast(1,false));
  r.can.incoming.push_back(broadcast(1));
  r.c.tick(r.now);
  pendingReadiness(r,since);
  CHECK(r.c.readinessStatus(0,r.now).frames == 1 && r.c.readinessStatus(0,r.now).stableMs == 0);
  r.step(1001); // CAN/measurements remain fresh, but this broadcast run expired.
  CHECK(r.c.readinessStatus(0,r.now).frames == 0);
  r.can.incoming.push_back(broadcast(1)); r.c.tick(r.now);
  CHECK(r.c.readinessStatus(0,r.now).frames == 1);
  pendingReadiness(r,since);
  r.step(1000); r.can.incoming.push_back(broadcast(1)); r.c.tick(r.now);
  pendingReadiness(r,since);
  r.step(1000); r.can.incoming.push_back(broadcast(1)); r.c.tick(r.now);
  CHECK(r.c.unitControl(0).outputDemand == OutputDemand::Active);
}
void readinessAckOrdering() {
  Rig r(zeroHoldConfiguration()); startOutput(r);
  const auto command = r.can.writes().back(); CHECK(command.data[1] == protocol::OnlineCurrent);
  r.dropRegister = protocol::OnlineCurrent; r.noBroadcast = 1; r.step(100);
  // These frames precede the ACK in one drain. None may count toward its run.
  r.can.incoming.push_back(broadcast(1)); r.can.incoming.push_back(broadcast(1));
  r.can.incoming.push_back(ack(command)); r.c.tick(r.now);
  CHECK(r.c.currentSynchronized(0) && r.c.readinessStatus(0,r.now).frames == 0);
  r.can.incoming.push_back(broadcast(1)); r.c.tick(r.now);
  CHECK(r.c.readinessStatus(0,r.now).frames == 1);
  r.can.incoming.push_back(ack(command)); r.c.tick(r.now); // Duplicate ACK cannot restart the timer.
  CHECK(r.c.readinessStatus(0,r.now).frames == 1);
  // A rejected/late/missing ACK must never be replaced by ready broadcasts.
  Rig late(zeroHoldConfiguration()); startOutput(late);
  const auto lateCommand = late.can.writes().back();
  late.dropRegister = protocol::OnlineCurrent; late.notReady = 0; late.step(1000);
  late.can.incoming.push_back(ack(lateCommand)); late.c.tick(late.now);
  CHECK(!late.c.currentSynchronized(0));
  late.step(3000); CHECK(late.c.readinessStatus(0,late.now).frames == 0);
  CHECK(late.c.unitControl(0).outputDemand == OutputDemand::Starting);
  late.step(15000); CHECK(late.c.unitControl(0).fault == CommandState::ReadinessTimeout);
  Rig rejected(zeroHoldConfiguration()); startOutput(rejected);
  rejected.rejectRegister = protocol::OnlineCurrent; rejected.notReady = 0; rejected.step(3000);
  CHECK(rejected.c.report().units[0].state == CommandState::Rejected);
  CHECK(!rejected.c.currentSynchronized(0) && rejected.c.readinessStatus(0,rejected.now).frames == 0);
  rejected.step(15000); CHECK(rejected.c.unitControl(0).fault == CommandState::ReadinessTimeout);
}
void readinessDeadlineAndLatch() {
  Rig r(zeroHoldConfiguration()); startOutput(r); const auto since = r.now; r.step();
  CHECK(r.c.setVoltage(5700) == Result::Ok); CHECK(r.c.setCurrent(0,1000) == Result::Ok);
  for (auto op : {Operation::All,Operation::Voltage,Operation::GroupCurrent}) {
    const auto p = r.c.preview(op,op==Operation::GroupCurrent?0:-1,r.now);
    CHECK(p.blocker == Issue::ReadinessPending && p.issues[0] == Issue::ReadinessPending);
    CHECK(r.c.queue(p,false,r.now) == Result::Blocked);
  }
  CHECK(r.c.configuration().operating.voltage == 5400 && r.c.configuration().operating.current[0] == 500);
  CHECK(r.c.setPollInterval(10000) == Result::Ok); // Even changing pollMs cannot extend this attempt.
  for (unsigned n=0; uint32_t(r.now-since)<control::startupReadyMs-250; ++n) {
    r.notReady = n%2; r.step(250); pendingReadiness(r,since);
  }
  r.step(250); CHECK(uint32_t(r.now-since) == control::startupReadyMs);
  CHECK(r.c.unitControl(0).fault == CommandState::ReadinessTimeout);
  r.finish(); CHECK(r.c.unitControl(0).standbyConfirmed);
  const auto stopped = r.can.writes().size();
  CHECK(r.c.setAutoResume(false) == Result::Ok);
  CHECK(r.c.setPollInterval(1000) == Result::Ok);
  r.live = 0; r.step(r.c.staleMs()+250);
  CHECK(r.c.unitControl(0).fault == CommandState::ReadinessTimeout);
  CHECK(!r.c.unitControl(0).standbyConfirmed);
  CHECK(r.c.controlStatus(r.now).activity == Activity::StopUnconfirmed);
  r.live = 1; r.step(3000); r.finish();
  CHECK(r.c.unitControl(0).fault == CommandState::ReadinessTimeout && r.c.unitControl(0).standbyConfirmed);
  CHECK(r.c.controlStatus(r.now).activity == Activity::StartBlocked);
  CHECK(r.c.setAutoResume(true) == Result::Ok); r.step(1000);
  CHECK(r.c.unitControl(0).fault == CommandState::ReadinessTimeout);
  r.noAc = 1; r.step(1000); CHECK(r.c.unitControl(0).fault == CommandState::ReadinessTimeout);
  r.noAc = 0; r.step(1000); r.finish();
  CHECK(r.c.unitControl(0).fault == CommandState::ReadinessTimeout && r.c.unitControl(0).standbyConfirmed);
  for (size_t n=stopped; n<r.can.writes().size(); ++n) {
    const auto f = r.can.writes()[n];
    CHECK(f.data[1] != protocol::OnlineVoltage && f.data[1] != protocol::OnlineCurrent);
    if (f.data[1] == protocol::Standby) CHECK(f.data[3]);
  }
  r.notReady = 0; r.c.reconcile(); r.finish();
  CHECK(r.c.unitControl(0).fault == CommandState::Idle && r.c.unitControl(0).outputDemand == OutputDemand::Active);
  CHECK(r.current[0] <= 500); // Retry restores authorized settings, never the staged 10 A.
  // A failed fallback retains the readiness cause and cannot claim stopped.
  Rig failed(zeroHoldConfiguration()); startOutput(failed); failed.step();
  failed.dropRegister = protocol::Standby; failed.step(20000);
  CHECK(failed.c.unitControl(0).fault == CommandState::ReadinessTimeout);
  CHECK(!failed.c.unitControl(0).standbyConfirmed);
  failed.c.setAutoResume(false); CHECK(failed.c.controlStatus(failed.now).activity == Activity::StopUnconfirmed);
}
void readinessTelemetryAndBoundary() {
  Rig silent(zeroHoldConfiguration()); startOutput(silent); silent.step(); silent.noBroadcast = 1;
  silent.step(control::startupReadyMs);
  CHECK(silent.c.unitControl(0).fault == CommandState::ReadinessTimeout);
  // Fresh output-ready broadcasts cannot qualify stale voltage measurements.
  Rig stale(zeroHoldConfiguration()); startOutput(stale); stale.step(); stale.noVoltage = 1;
  stale.step(stale.c.staleMs()+250); const auto since = stale.c.unitControl(0).outputSince;
  stale.notReady = 0; stale.step(2500); pendingReadiness(stale,since);
  CHECK(stale.c.readinessStatus(0,stale.now).frames == 0);
  stale.noVoltage = 0; stale.step(2250); CHECK(stale.c.unitControl(0).outputDemand == OutputDemand::Active);
  // Losing current telemetry while relying on the idle-status exception still
  // leaves the normal safety/reconciliation path in charge.
  Rig current(zeroHoldConfiguration()); startOutput(current); current.step(); current.noCurrent = 1;
  current.step(current.c.staleMs()+250);
  CHECK(!current.c.readinessStatus(0,current.now).pending && current.c.unitControl(0).outputDemand != OutputDemand::Active);
  // Finishing qualification exactly at the deadline is too late; a ready batch
  // drained then must not win over the fixed observation deadline.
  Rig boundary(zeroHoldConfiguration()); startOutput(boundary); const auto start = boundary.now; boundary.step();
  while(uint32_t(boundary.now-start)<control::startupReadyMs-control::outputReadyStableMs-250) boundary.step();
  boundary.notReady = 0; boundary.step(); boundary.step(control::outputReadyStableMs-250);
  pendingReadiness(boundary,start); CHECK(boundary.c.readinessStatus(0,boundary.now).stableMs == control::outputReadyStableMs-250);
  boundary.step(); CHECK(boundary.c.unitControl(0).fault == CommandState::ReadinessTimeout);
}
void readinessSafetyEdges() {
  for (unsigned edge=0; edge<5; ++edge) {
    Rig r(zeroHoldConfiguration()); startOutput(r); r.step();
    if (edge==0) {
      CHECK(r.c.setCurrent(0,0) == Result::Ok);
      r.apply(Operation::GroupCurrent,0);
      CHECK(r.c.unitControl(0).outputDemand == OutputDemand::Zero && r.c.unitControl(0).zeroConfirmed);
    } else if (edge==1) {
      r.c.setAutoResume(false); r.finish(); CHECK(r.c.controlStatus(r.now).activity == Activity::Holding);
    } else if (edge==2) {
      r.noAc = 1; r.step(); CHECK(r.c.availabilityReason(0,r.now) == AvailabilityReason::NoAc);
    } else if (edge==3) {
      r.live = 0; r.step(r.c.staleMs()+250); CHECK(r.c.issue(0,r.now) != Issue::None);
    } else {
      r.can.incoming.push_back(info(1,identity(9))); r.c.tick(r.now); CHECK(r.c.issue(0,r.now) != Issue::None);
    }
    CHECK(!r.c.readinessStatus(0,r.now).pending);
    CHECK(r.c.unitControl(0).outputDemand != OutputDemand::Active);
  }
}
void readinessMultiUnitAndWrap() {
  auto config = installation(2); config.groups[0].members = 1; config.groups[0].current = 1000;
  std::strcpy(config.groups[1].name,"GROUP2"); config.groups[1].members = 2; config.groups[1].current = 1000;
  Rig r(config); r.reportAc = true; r.notReady = 2; r.step();
  CHECK(r.c.queue(r.c.preview(Operation::All,-1,r.now),false,r.now) == Result::Ok);
  for (unsigned n=0; n<60 && (r.c.busy() || r.c.unitControl(0).outputDemand!=OutputDemand::Active); ++n) r.step();
  CHECK(r.c.unitControl(0).outputDemand == OutputDemand::Active);
  const auto since = r.c.unitControl(1).outputSince;
  pendingReadiness(r,since,1); CHECK(r.c.participants() == 3 && r.c.configuration().operating.current[1] == 1000);
  CHECK(r.c.preview(Operation::GroupCurrent,0,r.now).blocker == Issue::None);
  CHECK(r.c.preview(Operation::GroupCurrent,1,r.now).blocker == Issue::ReadinessPending);
  r.notReady = 0; r.step(2250); CHECK(r.c.unitControl(1).outputDemand == OutputDemand::Active);
  CHECK(r.c.controlStatus(r.now).activity == Activity::Running);
  Rig wrap(zeroHoldConfiguration(),false,true,UINT32_MAX-12000);
  startOutput(wrap); wrap.step(); const auto start = wrap.c.unitControl(0).outputSince;
  while (wrap.now < UINT32_MAX-500) wrap.step(250);
  wrap.notReady = 0; wrap.step(250);
  wrap.step(1750); pendingReadiness(wrap,start);
  wrap.step(250); CHECK(wrap.now < start && wrap.c.unitControl(0).outputDemand == OutputDemand::Active);
}
