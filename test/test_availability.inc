// Included inside test_main's fixture namespace. These tests exercise the real
// controller and journal, including decisions that race CAN or EEPROM changes.
void availabilityProtocol() {
  Discovery d;
  d.receive(info(1,identity(1)),100); d.receive(info(1,identity(1)),300); d.tick(300,5000);
  d.receive(data(1,0x83,uint32_t(1)<<29),400);
  CHECK(d.address(1)->acState(400,5000)==AcState::Absent);
  const auto epoch=d.address(1)->epoch;
  d.receive(data(1,0x81,0),500); d.receive(broadcast(1,true),500);
  d.receive(data(1,0x83,0),600);
  CHECK(d.address(1)->epoch!=epoch && !d.address(1)->telemetry.broadcastSeen && !d.address(1)->telemetry.freshMask);
  CHECK(d.address(1)->acState(600,5000)==AcState::Present);
  d.tick(6001,5000); CHECK(d.address(1)->acState(6001,5000)==AcState::Unknown);
  CanFrame negotiation={}; negotiation.extended=true; negotiation.length=8;
  negotiation.id=0x1000107e; negotiation.data[0]=0x30; negotiation.data[1]=1;
  CHECK(protocol::isNegotiation(negotiation)); d.receive(negotiation,6100);
  CHECK(d.negotiating() && !d.identity(identity(1),6100));
  negotiation.data[1]=2; d.receive(negotiation,7000);
  d.tick(9999,5000); CHECK(d.negotiating()); d.tick(10000,5000); CHECK(!d.negotiating());
  negotiation.extended=false; CHECK(!protocol::isNegotiation(negotiation));
}
void startupPowerPolicy() {
  FakeEeprom bytes; MemoryManager memory(bytes);
  Rig r(authorized(),true,false); r.reportAc=true; r.notReadyUntil=r.now+8000;
  r.c.configureStartup(true,true,r.now);
  CHECK(!r.c.chargeAllowed()); r.step(1999); CHECK(!r.c.chargeAllowed()); r.step(1); CHECK(r.c.chargeAllowed());
  r.step(5000); CHECK(r.c.unitControl(0).fault==CommandState::Idle && r.c.unitControl(1).fault==CommandState::Idle);
  for (const auto& f:r.can.writes()) if(f.data[1]==protocol::OnlineCurrent) CHECK(!protocol::readBigEndian(f.data+4));
  r.finish(); CHECK(r.c.controlStatus(r.now).activity==Activity::Running);
  // OFF preserves power for cooling; explicit hard stop is latched independently.
  r.c.setAutoResume(false); r.finish(); CHECK(r.c.chargeAllowed() && r.current[0]==0 && r.current[1]==0);
  CHECK(r.c.hardStop()==Result::Ok && !r.c.chargeAllowed());
  r.c.setAutoResume(true); r.step(5000); CHECK(r.c.startupStatus().permit==PermitState::Fault);
  r.c.reconcile(); r.step(1999); CHECK(!r.c.chargeAllowed()); r.step(1); CHECK(r.c.chargeAllowed()); r.finish();
  // A missing configuration cannot energize AC, even with a live ON request.
  r.c.configureStartup(true,false,r.now); r.step(5000); CHECK(!r.c.chargeAllowed());
  auto p=r.c.previewAvailability(AvailabilityAction::Probe,0,false,r.now);
  CHECK(p.blocker==Issue::None); CHECK(r.c.confirmAvailability(p,memory,r.now)==Result::Ok);
  CHECK(!r.c.enableControl().enabled() && r.c.startupStatus().commissioning);
  r.step(2000); CHECK(r.c.chargeAllowed()); r.finish(); CHECK(r.current[0]==0);
  // AC absence with live battery-backed CAN waits without manufacturing a fault.
  Rig dc(authorized(false),true,false); dc.reportAc=true; dc.noAc=3; dc.step(25000);
  CHECK(dc.c.availabilityReason(0,dc.now)==AvailabilityReason::NoAc);
  CHECK(dc.c.unitControl(0).fault==CommandState::Idle);
  dc.noAc=0; dc.notReadyUntil=dc.now+8000; dc.step(7000);
  CHECK(dc.c.unitControl(0).fault==CommandState::Idle && dc.c.unitControl(0).phase!=ControlPhase::Held);
  dc.finish(); CHECK(dc.c.unitControl(0).phase==ControlPhase::Held);
  // Without an AC alarm, readiness has a bounded longer wait, not an infinite retry.
  Rig slow(authorized(false),true,false); slow.notReadyUntil=slow.now+60000;
  slow.step(22000); CHECK(slow.c.unitControl(0).fault==CommandState::ObservationTimeout);
  // Observed non-zero current never gets the extended startup/no-AC exception.
  Rig leak(authorized(false),true,false); leak.reportAc=true; leak.noAc=3; leak.leaking=true;
  leak.c.configureStartup(true,true,leak.now); leak.step(10000);
  CHECK(leak.c.startupStatus().permit==PermitState::Fault);
  CHECK(leak.c.previewAvailability(AvailabilityAction::Accept,3,false,leak.now).blocker!=Issue::None);
  // Reconcile retries a failed local CAN init while AC remains inhibited.
  Rig retry(authorized(false),true,false); retry.can.initOk=false; CHECK(!retry.c.begin());
  retry.c.configureStartup(true,true,retry.now); retry.step(5000); CHECK(!retry.c.chargeAllowed());
  retry.can.initOk=true; retry.c.reconcile(); retry.step(2000); CHECK(retry.c.chargeAllowed());
}
void acceptedAbsences() {
  FakeEeprom bytes; MemoryManager memory(bytes);
  auto cfg=authorized(); cfg.acceptedAbsent=2;
  Rig r(cfg,true,false); r.live=1; r.finish(); r.step(6000); r.finish();
  CHECK(r.c.participants()==1 && r.c.requiredMembers()==1 && r.current[0]>0 && r.current[1]==0);
  CHECK(r.c.configuration().operating.current[1]==1000);
  // A returning accepted identity is fully reconciled at zero before inclusion.
  r.live=3; r.finish(); r.step(8000);
  CHECK(r.c.excluded()==2 && r.current[1]==0 && r.c.unitControl(1).phase==ControlPhase::Held);
  CHECK(r.c.availabilityAttention(r.now)==2);
  auto p=r.c.previewAvailability(AvailabilityAction::Include,3,false,r.now);
  CHECK(p.blocker==Issue::None && p.selected==2 && p.participants==3);
  CHECK(r.c.setCurrent(0,4000)==Result::Ok); // Staging invalidates old confirmation.
  CHECK(r.c.confirmAvailability(p,memory,r.now)==Result::Changed);
  r.apply(Operation::GroupCurrent,0);
  CHECK(r.c.report().recipients==1 && r.c.report().skipped==2);
  CHECK(r.c.configuration().operating.current[0]==2000 && r.c.configuration().operating.current[1]==2000);
  CHECK(r.current[1]==0);
  p=r.c.previewAvailability(AvailabilityAction::Include,2,false,r.now); CHECK(p.blocker==Issue::None);
  CHECK(r.c.confirmAvailability(p,memory,r.now)==Result::Ok); r.finish();
  CHECK(r.c.participants()==3 && r.current[1]>1900 && r.c.configuration().acceptedAbsent==2);
  CHECK(r.c.availabilityAttention(r.now)==0);
  // Inclusion is session-only. A later loss excludes this physical member again.
  r.live=1; r.step(6000); CHECK(r.c.excluded()==2 && r.c.participants()==1);
  r.live=3; r.finish(); r.step(4000); CHECK(r.current[1]==0);
  // Revoking a waiver must not silently admit a currently excluded PSU.
  p=r.c.previewAvailability(AvailabilityAction::Require,2,false,r.now);
  CHECK(p.blocker==Issue::None && r.c.confirmAvailability(p,memory,r.now)==Result::Ok);
  CHECK(r.c.acceptedAbsences()==0 && r.c.excluded()==2 && r.c.requiredMembers()==3);
  CHECK(r.c.preview(Operation::GroupCurrent,0,r.now).blocker==Issue::ApprovalRequired);
  // Newly missing identities are not covered by the saved exception.
  Rig another(cfg,true,false); another.live=2; another.step(12000);
  CHECK(another.current[1]==0 && another.c.requiredMembers()==1);
  auto absent=another.c.previewAvailability(AvailabilityAction::Accept,1,false,another.now);
  CHECK(absent.blocker==Issue::None);
  CHECK(another.c.confirmAvailability(absent,memory,another.now)==Result::Ok && another.c.participants()==0);
  CHECK(another.c.controlStatus(another.now).blocker==Issue::NoParticipants);
  CHECK(another.c.setCurrent(0,3000)==Result::Ok);
  const auto before=another.can.writes().size(); another.apply(Operation::GroupCurrent,0);
  CHECK(another.c.configuration().operating.current[0]==1500 && another.c.configuration().operating.current[1]==1500);
  CHECK(another.c.report().recipients==0 && another.c.report().succeeded==0);
  for(size_t n=before;n<another.can.writes().size();++n) {
    const auto f=another.can.writes()[n]; CHECK(f.data[1]!=protocol::OnlineCurrent || !protocol::readBigEndian(f.data+4));
  }
  // Bindings/group edits invalidate permissions along with old current shares.
  CHECK(another.c.removeGroup(0)==Result::Ok && another.c.configuration().acceptedAbsent==0 && another.c.acceptedAbsences()==0);
}
void availabilityDecisionsAndStorage() {
  FakeEeprom bytes; MemoryManager memory(bytes);
  Rig r(authorized(),true,false); r.live=1; r.c.configureStartup(true,true,r.now);
  r.step(14000); auto p=r.c.previewAvailability(AvailabilityAction::Accept,3,false,r.now);
  CHECK(p.blocker==Issue::Settling && r.current[0]==0);
  r.step(6000); p=r.c.previewAvailability(AvailabilityAction::Accept,3,true,r.now);
  CHECK(p.blocker==Issue::None && p.selected==2 && p.participants==1);
  CHECK(r.c.confirmAvailability(p,memory,r.now)==Result::Ok && memory.saving());
  CHECK(r.c.excluded()==0 && r.current[0]==0);
  while(memory.saving()) { memory.stepSave(); r.step(1); }
  r.step(); CHECK(r.c.availabilityReport().remembered && r.c.configuration().acceptedAbsent==2);
  r.finish(); CHECK(r.current[0]>0 && r.c.participants()==1);
  Configuration loaded=defaultConfiguration(); CHECK(memory.load(loaded)==StorageResult::Ok && loaded.acceptedAbsent==2);
  CHECK(loaded.operating.current[1]==1000 && loaded.autoResume);
  // Remember includes current drafts but never authorizes them implicitly.
  CHECK(r.c.setCurrent(0,5000)==Result::Ok);
  p=r.c.previewAvailability(AvailabilityAction::Require,2,true,r.now); CHECK(p.blocker==Issue::None);
  CHECK(r.c.confirmAvailability(p,memory,r.now)==Result::Ok);
  while(memory.saving()) { memory.stepSave(); r.step(1); }
  r.step(); CHECK(memory.load(loaded)==StorageResult::Ok && loaded.acceptedAbsent==0);
  CHECK(loaded.groups[0].current==5000 && loaded.operating.current[0]==1000);
  // Session permission does not leak into an ordinary save.
  p=r.c.previewAvailability(AvailabilityAction::Accept,2,false,r.now); CHECK(p.blocker==Issue::None);
  CHECK(r.c.confirmAvailability(p,memory,r.now)==Result::Ok);
  CHECK(memory.save(r.c.configuration())==StorageResult::Ok);
  CHECK(memory.load(loaded)==StorageResult::Ok && loaded.acceptedAbsent==0);
  // A topology change in the same tick as save completion prevents activation.
  Rig race(authorized(),true,false); race.live=1; race.step(16000);
  p=race.c.previewAvailability(AvailabilityAction::Accept,2,true,race.now); CHECK(p.blocker==Issue::None);
  CHECK(race.c.confirmAvailability(p,memory,race.now)==Result::Ok);
  while(memory.saving()) memory.stepSave();
  race.live=3; race.step();
  CHECK(race.c.availabilityReport().remembered && race.c.availabilityReport().result==Result::Changed && race.c.excluded()==0);
  CHECK(memory.load(loaded)==StorageResult::Ok && loaded.acceptedAbsent==2);
  // A failed journal write never applies the in-memory decision.
  FakeEeprom bad; MemoryManager badMemory(bad); bad.ignoredAddress=0;
  Rig fail(authorized(),true,false); fail.live=1; fail.step(16000);
  p=fail.c.previewAvailability(AvailabilityAction::Accept,2,true,fail.now);
  CHECK(fail.c.confirmAvailability(p,badMemory,fail.now)==Result::Ok);
  while(badMemory.saving()) badMemory.stepSave();
  fail.step();
  CHECK(fail.c.availabilityReport().result==Result::Blocked && !fail.c.availabilityReport().remembered && !fail.c.excluded());
  // Every interrupted byte preserves either the old complete decision or the new one.
  FakeEeprom journal; MemoryManager journalMemory(journal); auto old=authorized(); old.acceptedAbsent=1;
  CHECK(journalMemory.save(old)==StorageResult::Ok); const auto baseline=journal.bytes;
  auto next=old; next.acceptedAbsent=2; next.groups[0].current=4400;
  for(int cut=0;cut<218;++cut) {
    journal.bytes=baseline; journal.stopAfter=cut; journal.calls=0;
    try { MemoryManager writing(journal); writing.save(next); } catch(const PowerCut&) {}
    journal.stopAfter=-1; MemoryManager reading(journal); Configuration value=defaultConfiguration();
    CHECK(reading.load(value)==StorageResult::Ok);
    CHECK((value.acceptedAbsent==1 && value.groups[0].current==old.groups[0].current) ||
          (value.acceptedAbsent==2 && value.groups[0].current==4400));
  }
  // Schema 2 migrates without an absence exception; reserved bits stay validated.
  journal.bytes=baseline; journal.bytes[1]=2; journal.bytes[28]=0; recordCrc(journal.bytes.data(),216);
  CHECK(journalMemory.load(loaded)==StorageResult::Ok && loaded.acceptedAbsent==0);
  journal.bytes[28]=1; recordCrc(journal.bytes.data(),216);
  CHECK(journalMemory.load(loaded)!=StorageResult::Ok);
}
void availabilityRefreshAndSoak() {
  FakeEeprom bytes; MemoryManager memory(bytes);
  Rig r; r.apply(); const auto sequence=r.c.report().sequence;
  CHECK(r.c.setCurrent(0,1000)==Result::Ok && r.c.setVoltage(5600)==Result::Ok);
  const auto before=r.can.writes().size(); r.step(22000);
  bool sawV=false,sawI=false;
  for(size_t n=before;n<r.can.writes().size();++n) {
    const auto f=r.can.writes()[n]; const auto value=protocol::readBigEndian(f.data+4);
    CHECK(f.data[1]==protocol::OnlineVoltage || f.data[1]==protocol::OnlineCurrent);
    if(f.data[1]==protocol::OnlineVoltage) { CHECK(value==protocol::encodeVoltage(5400)); sawV=true; }
    else { CHECK(value==protocol::encodeCurrent(2750,5000)); sawI=true; }
  }
  CHECK(sawV && sawI && r.c.report().sequence==sequence);
  // A rejected command is not covertly retried by the refresh scheduler.
  r.rejectRegister=protocol::OnlineCurrent; r.apply(Operation::Voltage,-1);
  auto apply=r.c.preview(Operation::GroupCurrent,0,r.now); CHECK(r.c.queue(apply,false,r.now)==Result::Ok); r.finish();
  CHECK(r.c.report().failed==3); const auto failed=r.can.writes().size(); r.step(20000);
  CHECK(r.can.writes().size()==failed);
  // Changing availability continuously keeps whole-group approval blocked.
  Rig unstable(authorized(false),true,false); unstable.reportAc=true; unstable.finish();
  for(unsigned n=0;n<18;++n) { unstable.noAc=n%2 ? 0 : 3; unstable.step(2000); }
  CHECK(!unstable.c.availabilitySettled(0,unstable.now) && unstable.c.availabilityUnstable(0,unstable.now));
  unstable.noAc=3; unstable.step(4000);
  CHECK(unstable.c.availabilitySettled(0,unstable.now));
  const auto p=unstable.c.previewAvailability(AvailabilityAction::Accept,3,false,unstable.now);
  CHECK(p.blocker==Issue::None && p.selected==3);
  // A negotiated address epoch cancels an old confirmation.
  CanFrame negotiation={}; negotiation.extended=true; negotiation.length=8;
  negotiation.id=0x1000107e; negotiation.data[0]=0x30; negotiation.data[1]=1;
  unstable.can.incoming.push_back(negotiation); unstable.step();
  CHECK(unstable.c.confirmAvailability(p,memory,unstable.now)==Result::Changed);
}
void availabilityInterfaces() {
  Rig r(authorized(),true,false); r.live=1; r.step(16000); Terminal terminal(r);
  const auto diag=terminal.input("diag startup\n");
  CHECK(diag.find("CHG_ALLOW not fitted")!=std::string::npos && diag.find("NO CAN; output unknown")!=std::string::npos);
  CHECK(diag.find("SETTLED")!=std::string::npos);
  auto text=terminal.input("startup accept GROUP1 remember\n");
  CHECK(text.find("selected=2 participants-after=1")!=std::string::npos && text.find("saves current configuration too")!=std::string::npos);
  CHECK(text.find("ARE YOU SURE?")!=std::string::npos);
  CHECK(terminal.input("\x18" "confirm yes\n").find("no valid confirmation")!=std::string::npos);
  terminal.input("startup accept GROUP1 remember\n"); terminal.input("confirm yes\n");
  for(unsigned n=0;n<500;++n) { terminal.memory.stepSave(); r.step(1); terminal.console.tick(r.now); }
  r.finish(); CHECK(r.c.configuration().acceptedAbsent==2 && r.current[0]>0);
  CHECK(terminal.input("diag bus\n").find("output-overruns=0")!=std::string::npos);
  CHECK(terminal.input("startup include GROUP1 remember\n").find("ARE YOU SURE?")==std::string::npos);
  r.live=3; r.step(12000);
  CHECK(terminal.input("startup include GROUP1\n").find("selected=2 participants-after=1,2")!=std::string::npos);
  terminal.input("confirm no\n"); CHECK(r.c.excluded()==2);
  terminal.input("startup include GROUP1\n"); terminal.input("confirm yes\n"); r.finish();
  CHECK(r.c.excluded()==0 && r.current[1]>0);
  // The local group-first flow uses exactly the same confirmation and writer.
  Rig local(authorized(),true,false); local.live=1; local.step(16000); UiRig u(local); UiFrame frame;
  u.ui.action({ActionKind::Startup},local.now);
  CHECK(u.ui.page()==UiPage::Availability);
  UiView::compose(frame,u.ui,local.c,local.now); snapshot("availability-group",frame);
  CHECK(rowText(frame,3).find("Availability settled")!=std::string::npos);
  u.ui.action({ActionKind::AvailabilityIndividuals},local.now);
  CHECK(u.ui.page()==UiPage::AvailabilityUnits && u.ui.availabilitySelected()==2);
  UiView::compose(frame,u.ui,local.c,local.now); snapshot("availability-individual",frame);
  u.ui.action({ActionKind::AvailabilityReview},local.now);
  CHECK(u.ui.availabilityPlan().selected==2 && u.ui.availabilityPlan().blocker==Issue::None);
  u.ui.action({ActionKind::AvailabilityRemember},local.now);
  UiView::compose(frame,u.ui,local.c,local.now); snapshot("availability-confirm",frame);
  CHECK(rowText(frame,10).find("Remember + save config")!=std::string::npos);
  u.ui.action({ActionKind::AvailabilityConfirm},local.now); CHECK(u.ui.working()); u.finish(); local.finish();
  CHECK(u.ui.availabilityResult()==Result::Ok && u.ui.availabilityRemembered());
  UiView::compose(frame,u.ui,local.c,local.now); snapshot("availability-result",frame);
  CHECK(rowText(frame,10).find("AC cutoff not fitted")!=std::string::npos);
  UiButton buttons[UiLayout::capacity];
  auto buttonCount=UiLayout::buttons(u.ui,buttons);
  for(uint8_t i=0;i<buttonCount;++i) CHECK(buttons[i].action.kind!=ActionKind::HardStop);
  u.ui.action({ActionKind::Back},local.now); u.ui.action({ActionKind::Back},local.now);
  local.can.incoming.push_back(data(1,0x73,540*1024UL)); local.c.tick(local.now);
  UiView::compose(frame,u.ui,local.c,local.now);
  CHECK(rowText(frame,1).find("RUN 1/2")!=std::string::npos && rowText(frame,12).find("ACT 1/2")!=std::string::npos);
  local.live=3; local.step(12000); u.ui.action({ActionKind::Startup},local.now);
  CHECK(u.ui.availabilityAction()==AvailabilityAction::Include);
  u.ui.action({ActionKind::AvailabilityPrimary},local.now); CHECK(u.ui.page()==UiPage::AvailabilityConfirm);
  local.c.setAutoResume(false); u.ui.tick(local.now);
  CHECK(u.ui.page()==UiPage::AvailabilityResult && u.ui.availabilityResult()==Result::Changed);
  // Faults remain red and cannot be hidden by the accepted-absence count.
  local.finish(); local.rejectRegister=protocol::FanMode; local.c.setFanOverride(1,FanOverride::Auto,local.now);
  local.step(2000); u.ui.action({ActionKind::Back},local.now); u.ui.action({ActionKind::Back},local.now);
  UiView::compose(frame,u.ui,local.c,local.now); CHECK(rowText(frame,12).find("ERR")!=std::string::npos);
  // A failed CAN initialization is directly retryable on the local interface.
  Rig retry(authorized(false),true,false); retry.c.configureStartup(true,true,retry.now);
  retry.can.initOk=false; CHECK(!retry.c.begin()); UiRig retryUi(retry);
  retryUi.ui.action({ActionKind::Startup},retry.now);
  UiView::compose(frame,retryUi.ui,retry.c,retry.now);
  CHECK(rowText(frame,3).find("CAN INIT FAILED: RETRY")!=std::string::npos);
  CHECK(rowText(frame,7).find("Retry startup")!=std::string::npos);
  retry.can.initOk=true; retryUi.ui.action({ActionKind::AvailabilityPrimary},retry.now);
  CHECK(retry.c.ready() && !retry.c.chargeAllowed()); retry.step(2000); CHECK(retry.c.chargeAllowed());
  // A stale confirmation must not inherit an earlier decision's storage error.
  Rig stale(authorized(),true,false); stale.live=1; stale.step(16000); UiRig staleUi(stale);
  staleUi.bytes.ignoredAddress=0;
  staleUi.ui.action({ActionKind::Startup},stale.now);
  staleUi.ui.action({ActionKind::AvailabilityPrimary},stale.now);
  staleUi.ui.action({ActionKind::AvailabilityRemember},stale.now);
  staleUi.ui.action({ActionKind::AvailabilityConfirm},stale.now); staleUi.finish();
  CHECK(staleUi.ui.storageResult()==StorageResult::WriteFailed);
  staleUi.ui.action({ActionKind::Back},stale.now);
  staleUi.ui.action({ActionKind::AvailabilityPrimary},stale.now);
  stale.c.setAutoResume(false);
  staleUi.ui.action({ActionKind::AvailabilityConfirm},stale.now);
  CHECK(staleUi.ui.availabilityResult()==Result::Changed && staleUi.ui.storageResult()==StorageResult::Ok);
}
