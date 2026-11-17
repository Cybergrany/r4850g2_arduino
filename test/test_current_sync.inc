// The real CAN scheduler, ACK handling, telemetry and presentation share the cap.
void startSync(Rig& r, uint16_t target=1000) {
  CHECK(r.c.set({},SettingFields::Current,target)==Result::Ok);
  for (uint8_t i=0;i<r.c.count();++i) r.reportedCurrent[i]=0;
  CHECK(r.c.queue(r.c.preview({},SettingFields::Current,r.now),false,r.now)==Result::Ok);
  for (unsigned n=0;n<120 && r.c.syncStatus(r.now).phase!=SyncPhase::Waiting;++n) r.step();
  CHECK(r.c.syncStatus(r.now).phase==SyncPhase::Waiting);
}
void currentSyncStages() {
  Rig r; startSync(r);
  CHECK(r.c.syncStatus(r.now).members==3 && !r.c.report().succeeded && r.c.busy());
  CHECK(r.c.controlStatus(r.now).activity==Activity::Synchronizing);
  CHECK(r.c.configuration().operating.current[0]==1000 && r.c.effectiveCurrent(0)==100);
  CHECK(r.c.preview({},SettingFields::Current,r.now).blocker==Issue::Busy);
  r.step(12000); // Repeated periodic refreshes must also retain the priming cap.
  for (const auto& f:r.can.writes()) if (f.data[1]==protocol::OnlineCurrent)
    CHECK(protocol::readBigEndian(f.data+4)<=protocol::encodeCurrent(100,5000));
  CHECK(!r.c.syncStatus(r.now).qualified);
  CHECK(r.c.setCurrent(0,3000)==Result::Busy); // Conflicting changes cannot alter the frozen operation.
  r.reportedCurrent[0]=97; r.reportedCurrent[1]=0; r.step(2500);
  CHECK(r.c.syncStatus(r.now).qualified==1 && r.c.busy());
  r.reportedCurrent[1]=97; r.step(1750); CHECK(r.c.busy() && !r.c.report().succeeded);
  r.step(500); r.finish();
  CHECK(!r.c.syncStatus(r.now).members && r.c.report().succeeded==3);
  CHECK(r.c.configuration().current[0]==1000 && r.c.configuration().operating.current[0]==1000);
  CHECK(r.c.effectiveCurrent(0)==1000 && r.current[0]>970 && r.current[1]>970);
  // A positive-to-positive adjustment does not prime again.
  r.reportedCurrent[0]=r.reportedCurrent[1]=UINT16_MAX;
  CHECK(r.c.setCurrent(0,3000)==Result::Ok);
  const auto start=r.can.writes().size(); r.apply(Operation::GroupCurrent,0);
  for (size_t i=start;i<r.can.writes().size();++i) if(r.can.writes()[i].data[1]==protocol::OnlineCurrent)
    CHECK(protocol::readBigEndian(r.can.writes()[i].data+4)>protocol::encodeCurrent(100,5000));
  Rig one(installation(1)); // Single starts retain the ordinary direct path.
  one.c.setCurrent(0,1000); one.apply(); CHECK(one.c.report().succeeded==1 && !one.c.syncStatus(one.now).members);
}
void currentSyncEvidence() {
  Rig r; startSync(r,50); // Never increase a requested current smaller than 1 A.
  CHECK(r.c.effectiveCurrent(0)==50);
  r.reportedCurrent[0]=r.reportedCurrent[1]=48; r.step(1750);
  CHECK(!r.c.syncStatus(r.now).qualified);
  r.reportedCurrent[1]=0; r.step(250);
  r.reportedCurrent[1]=48; r.step(2000);
  CHECK(r.c.syncStatus(r.now).qualified==1);
  r.step(250); r.finish(); CHECK(r.c.report().succeeded==3);
  // Bounds are relative to the encoded/decoded limit, not the unrepresentable 1 A.
  Rig band; startSync(band);
  band.reportedCurrent[0]=68; band.reportedCurrent[1]=126; band.step(3000);
  CHECK(band.c.syncStatus(band.now).qualified==2);
  band.reportedCurrent[0]=69; band.reportedCurrent[1]=128; band.step(3000);
  CHECK(band.c.syncStatus(band.now).qualified==1);
  // One stale reading resets stability; repeatedly inspecting a snapshot cannot qualify it.
  band.noCurrent=1; band.step(band.c.staleMs()+250); CHECK(!(band.c.syncStatus(band.now).qualified&1));
  band.noCurrent=0; band.step(); CHECK(!(band.c.syncStatus(band.now).qualified&1));
  for(unsigned n=0;n<20;++n) band.c.tick(band.now);
  CHECK(!(band.c.syncStatus(band.now).qualified&1));
  band.reportedCurrent[0]=band.reportedCurrent[1]=97; band.step(2250); band.finish();
  // Thermal limiting is applied once to authorized targets, then the priming cap.
  Rig thermal; startSync(thermal,200); thermal.temperature=70; thermal.step(2500);
  CHECK(thermal.c.effectiveCurrent(0)==60 && thermal.c.configuration().operating.current[0]==200);
  thermal.reportedCurrent[0]=thermal.reportedCurrent[1]=58; thermal.step(2250); thermal.finish();
  CHECK(thermal.c.effectiveCurrent(0)==60);
}
void currentSyncTimeoutAndBypass() {
  Rig r; startSync(r); const auto since=r.now;
  r.step(currentSync::timeoutMs); CHECK(r.c.syncStatus(r.now).timedOut && r.c.busy());
  const auto alert=r.c.alerts().at(AlertManager::Startup);
  CHECK(alert.active && alert.code==AlertCode::SyncTimeout && alertLevel(alert.code)==AlertLevel::Warning);
  r.c.alerts().acknowledge(alert.id); r.step(6000);
  CHECK(r.c.alerts().at(AlertManager::Startup).id==alert.id && r.c.effectiveCurrent(0)==100);
  CHECK(uint32_t(r.now-since)>=currentSync::timeoutMs);
  Terminal t(r); CHECK(t.input("diag sync\n").find("CURRENT SYNC waiting")!=std::string::npos);
  CHECK(t.input("sync bypass on confirm\n").find("REVIEW REQUIRED")!=std::string::npos);
  CHECK(t.input("sync bypass on\n").find("Session only")!=std::string::npos);
  r.c.setAutoResume(true); CHECK(t.input("sync bypass on confirm\n").find("changed")!=std::string::npos);
  t.input("sync bypass on\n"); r.step(limits::confirmationMs+1);
  CHECK(t.input("sync bypass on confirm\n").find("changed")!=std::string::npos);
  t.input("sync bypass on\n"); CHECK(t.input("sync bypass on confirm\n").find("OK")!=std::string::npos);
  r.finish(); CHECK(r.c.syncBypassed() && r.c.report().succeeded==3 && r.c.effectiveCurrent(0)==1000);
  CHECK(t.input("sync bypass off\n").find("OK")!=std::string::npos);
  CHECK(!r.c.syncBypassed() && r.c.effectiveCurrent(0)==1000);
  // A new OFF defeats a previously reviewed bypass; reconnect alone changes no setting.
  r.c.setAutoResume(false); r.finish(); r.c.setAutoResume(true); r.step(4000);
  t.input("sync bypass on\n"); r.c.setAutoResume(false);
  CHECK(t.input("sync bypass on confirm\n").find("changed")!=std::string::npos);
  r.finish(); CHECK(!r.c.syncStatus(r.now).members && !r.current[0] && !r.current[1]);
  CHECK(r.c.bypassSync(r.c.reviewSync(r.now),r.now)==Result::Ok);
  t.input("hello\n"); CHECK(r.c.syncBypassed());
  FakeEeprom bytes; MemoryManager memory(bytes); CHECK(memory.save(r.c.configuration())==StorageResult::Ok);
  Configuration saved; CHECK(memory.load(saved)==StorageResult::Ok); Rig reboot(saved,true,false);
  CHECK(!reboot.c.syncBypassed());
}
void currentSyncSafety() {
  // A rejected priming write cannot release any other member. Explicit retry/OFF remains necessary.
  Rig r; r.c.setCurrent(0,1000); r.rejectRegister=protocol::OnlineCurrent;
  CHECK(r.c.queue(r.c.preview({},SettingFields::Current,r.now),false,r.now)==Result::Ok);
  r.step(5000); CHECK(r.c.syncStatus(r.now).phase==SyncPhase::Blocked && r.c.report().failed);
  CHECK(r.c.bypassSync(r.c.reviewSync(r.now),r.now)==Result::Ok);
  r.step(3000); CHECK(r.c.syncStatus(r.now).phase==SyncPhase::Blocked && r.current[0]==0 && r.current[1]==0);
  r.rejectRegister=0xff; r.c.setAutoResume(false); r.finish();
  // A missing member latches this attempt; its spontaneous return cannot release it.
  Rig gone; startSync(gone); gone.live=1; gone.step(6000);
  CHECK(gone.c.syncStatus(gone.now).phase==SyncPhase::Blocked && gone.c.effectiveCurrent(0)==100);
  gone.live=3; gone.step(10000); CHECK(gone.c.busy() && gone.c.effectiveCurrent(0)==100);
  gone.c.setAutoResume(false); gone.finish();
  Rig swapped; startSync(swapped);
  swapped.can.incoming.push_back(info(2,identity(99))); swapped.c.tick(++swapped.now);
  CHECK(swapped.c.syncStatus(swapped.now).phase==SyncPhase::Blocked && (swapped.c.report().failed&2));
  const auto before=swapped.can.writes().size();
  CHECK(swapped.c.bypassSync(swapped.c.reviewSync(swapped.now),swapped.now)==Result::Ok);
  swapped.step(2500);
  for (size_t n=before;n<swapped.can.writes().size();++n) {
    const auto f=swapped.can.writes()[n];
    if (f.data[1]==protocol::OnlineCurrent) CHECK(protocol::readBigEndian(f.data+4)<=protocol::encodeCurrent(100,5000));
  }
  CHECK(swapped.c.syncStatus(swapped.now).phase==SyncPhase::Blocked);
  // Readiness timeout remains separate and bounded, not extended to the sync deadline.
  Rig ready; ready.reportAc=true; ready.outputReadyDelay=30000; ready.c.setCurrent(0,1000);
  CHECK(ready.c.queue(ready.c.preview({},SettingFields::Current,ready.now),false,ready.now)==Result::Ok);
  ready.step(22000); CHECK(ready.c.unitControl(0).fault==CommandState::ReadinessTimeout);
  CHECK(ready.c.syncStatus(ready.now).phase==SyncPhase::Blocked);
  // Rollover does not make the stable window or warning fire early.
  Rig wrap(installation(),false,true,UINT32_MAX-14000); startSync(wrap);
  wrap.step(15000); CHECK(!wrap.c.syncStatus(wrap.now).timedOut);
  wrap.reportedCurrent[0]=wrap.reportedCurrent[1]=97; wrap.step(2250); wrap.finish();
  CHECK(wrap.c.report().succeeded==3);
}
void currentSyncFastBatch() {
  Rig r(installation(9)); startSync(r,300);
  for (uint8_t i=0;i<9;++i) r.can.incoming.push_back(data(i+1,0x73,0));
  r.c.tick(++r.now); r.c.alerts().acknowledgeAll();
  { UiRig view(r); recoverySnapshot(view,"control-current-sync-groups"); }
  for(uint8_t i=0;i<9;++i) r.reportedCurrent[i]=97;
  uint32_t first=0,last=0; UnitMask final=0; size_t consumed=r.can.sent.size();
  for(unsigned n=0;n<400 && r.c.busy();++n) {
    r.step(25,true,n%10==0);
    for(;consumed<r.can.sent.size();++consumed) {
      const auto& f=r.can.sent[consumed];
      if(protocol::command(f.id)==protocol::setCommand && f.data[1]==protocol::OnlineCurrent &&
          protocol::readBigEndian(f.data+4)==protocol::encodeCurrent(300,5000)) {
        if(!final) first=r.now;
        last=r.now; final|=1U<<(protocol::address(f.id)-1);
      }
    }
  }
  CHECK(final==511 && !r.c.busy() && uint32_t(last-first)<1000);
  CHECK(r.c.report().succeeded==511);
}
void currentSyncUi() {
  Rig r; UiRig u(r);
  r.reportedCurrent[0]=r.reportedCurrent[1]=0;
  u.config(); u.ui.action({ActionKind::EditCurrent},++r.now);
  u.ui.action({ActionKind::Adjust,1},++r.now); u.ui.action({ActionKind::Apply},++r.now);
  for(unsigned n=0;n<300;++n) u.tick(25);
  CHECK(u.ui.working() && !u.memory.saving() && r.c.syncStatus(r.now).phase==SyncPhase::Waiting);
  CHECK(u.ui.recoveryLink().kind==RecoveryKind::Sync);
  recoverySnapshot(u,"control-current-sync-apply");
  u.ui.action({ActionKind::Recover},++r.now);
  CHECK(u.ui.page()==UiPage::Recovery && u.ui.working());
  recoverySnapshot(u,"recovery-current-sync");
  u.ui.action({ActionKind::RecoveryReview},++r.now);
  CHECK(u.ui.page()==UiPage::RecoveryConfirm && u.ui.recoverySelection()==0);
  recoverySnapshot(u,"recovery-current-sync-confirm");
  u.ui.input({0,true,false},++r.now); // Cancel is the first wheel choice.
  CHECK(!r.c.syncBypassed() && u.ui.page()==UiPage::Recovery);
  u.ui.action({ActionKind::RecoveryReview},++r.now);
  u.ui.action({ActionKind::RecoveryConfirm},r.now); CHECK(!r.c.syncBypassed());
  u.ui.action({ActionKind::RecoveryConfirm},++r.now);
  CHECK(r.c.syncBypassed()); u.finish();
  CHECK(u.ui.recovering() && u.ui.notice()==UiNotice::Saved);
  Configuration saved; CHECK(u.memory.load(saved)==StorageResult::Ok && saved.current[0]==r.c.configuration().current[0]);
  u.ui.action({ActionKind::Back},++r.now); CHECK(u.ui.page()==UiPage::Result && u.ui.notice()==UiNotice::Saved);
  // OFF during a review invalidates it and keeps the operation unsaved.
  Rig off; UiRig o(off); startSync(off); o.ui.action({ActionKind::SettingsChoice,5},++off.now);
  o.ui.action({ActionKind::RecoveryReview},++off.now); off.c.setAutoResume(false);
  o.ui.action({ActionKind::RecoveryConfirm},++off.now);
  CHECK(o.ui.recoveryResult()==Result::Changed && !off.c.syncBypassed()); off.finish();
  // Ordinary navigation exposes the session checkbox on the second menu page.
  o.ui.action({ActionKind::Back},++off.now); o.ui.action({ActionKind::Back},++off.now);
  o.ui.action({ActionKind::Configure},++off.now); o.ui.action({ActionKind::Next},++off.now);
  recoverySnapshot(o,"settings-sync-menu");
  UiButton buttons[UiLayout::capacity]; const auto count=UiLayout::buttons(o.ui,buttons);
  bool found=false; for(uint8_t i=0;i<count;++i) if(buttons[i].action.kind==ActionKind::SettingsChoice && buttons[i].action.value==5) found=true;
  CHECK(found);
  // Timeout notification links to Config without acknowledging or releasing.
  Rig timeout; startSync(timeout); timeout.step(currentSync::timeoutMs); UiRig t(timeout);
  openIncident(t,AlertCode::SyncTimeout,AlertManager::Startup);
  t.ui.action({ActionKind::Recover},++timeout.now); CHECK(t.ui.recoveryKind()==RecoveryKind::Sync);
  CHECK(!timeout.c.alerts().at(AlertManager::Startup).acknowledged && timeout.c.effectiveCurrent(0)==100);
  t.ui.action({ActionKind::Stop},++timeout.now); CHECK(!timeout.c.enableControl().enabled()); timeout.finish();
  // A failed member leaves retry accessible while the original job owns the UI.
  Rig fault; startSync(fault); UiRig f(fault); fault.live=1; fault.step(6000);
  f.ui.action({ActionKind::SettingsChoice,5},++fault.now); f.ui.action({ActionKind::RecoveryReview,1},++fault.now);
  CHECK(f.ui.page()==UiPage::RecoveryConfirm && f.ui.recoveryCommand()==RecoveryCommand::Retry);
  f.ui.input({0,true,false},++fault.now); CHECK(fault.c.syncStatus(fault.now).phase==SyncPhase::Blocked);
  f.ui.action({ActionKind::RecoveryReview,1},++fault.now); f.ui.action({ActionKind::RecoveryConfirm},++fault.now);
  CHECK(!fault.c.syncStatus(fault.now).members);
}
void currentSyncMembershipAndSources() {
  // Existing active PSUs and explicitly excluded members are not forced into priming.
  Rig mixed(installation(3)); mixed.c.set({ScopeKind::Unit,0},SettingFields::Current,500);
  CHECK(mixed.c.queue(mixed.c.preview({ScopeKind::Unit,0},SettingFields::Current,mixed.now),false,mixed.now)==Result::Ok);
  mixed.finish(); uint16_t held; CHECK(mixed.c.acknowledgedCurrent(0,held));
  startSync(mixed); CHECK(mixed.c.syncStatus(mixed.now).members==6);
  CHECK(mixed.c.effectiveCurrent(0)==uint32_t(held)*5000/1024 && mixed.current[0]>480);
  const auto refreshStart=mixed.can.writes().size(); mixed.step(15000);
  for (size_t n=refreshStart;n<mixed.can.writes().size();++n) {
    const auto f=mixed.can.writes()[n];
    if (protocol::address(f.id)==1 && f.data[1]==protocol::OnlineCurrent) CHECK(protocol::readBigEndian(f.data+4)==held);
  }
  // A running recipient is outside the priming set, but its fault still blocks
  // the shared final batch. Bypass cannot authorize an incomplete operation.
  mixed.notReady=1; mixed.step(1000);
  CHECK(mixed.c.syncStatus(mixed.now).phase==SyncPhase::Blocked && (mixed.c.report().failed&1));
  CHECK(mixed.c.effectiveCurrent(1)<=100 && mixed.c.busy());
  mixed.notReady=0;
  mixed.c.setAutoResume(false); mixed.finish();
  auto cfg=authorized(); cfg.disabled=2;
  Rig excluded(cfg,true,false); excluded.finish(); CHECK(!excluded.c.syncStatus(excluded.now).members && excluded.current[1]==0);
  // Boot preference, serial and physical input all enter the same restore path.
  for(unsigned source=0;source<3;++source) {
    auto boot=authorized(source==0); Rig r(boot,true,false,100,source==2);
    for(uint8_t i=0;i<r.c.count();++i) r.reportedCurrent[i]=0;
    if(source==1) r.c.setAutoResume(true);
    if(source==2) { r.c.sampleEnableInput(false,r.now); r.step(100); r.c.sampleEnableInput(true,r.now); r.step(100); r.c.sampleEnableInput(true,r.now); }
    r.step(20000); CHECK(r.c.syncStatus(r.now).members==3 && r.c.effectiveCurrent(0)==100);
    CHECK(r.c.controlStatus(r.now).activity!=Activity::Running);
    r.c.setAutoResume(false); r.finish(); CHECK(!r.c.syncStatus(r.now).members);
  }
}

void currentSyncAckAndThermalRaces() {
  Rig r; startSync(r);
  CanFrame prime;
  for(const auto& f:r.can.writes()) if(f.data[1]==protocol::OnlineCurrent && protocol::address(f.id)==1) prime=f;
  r.reportedCurrent[0]=r.reportedCurrent[1]=97;
  for(unsigned n=0;n<40 && r.c.syncStatus(r.now).phase!=SyncPhase::Releasing;++n) r.step(100);
  CHECK(r.c.syncStatus(r.now).phase==SyncPhase::Releasing);
  r.dropRegister=protocol::OnlineCurrent;
  for(unsigned n=0;n<10 && protocol::readBigEndian(r.can.writes().back().data+4)<=protocol::encodeCurrent(100,5000);++n) r.step(100);
  const auto final=r.can.writes().back();
  CHECK(final.data[1]==protocol::OnlineCurrent && protocol::readBigEndian(final.data+4)==protocol::encodeCurrent(1000,5000));
  r.can.incoming.push_back(ack(prime)); r.c.tick(++r.now);
  CHECK(!r.c.currentSynchronized(0) && !r.c.report().succeeded);
  r.can.incoming.push_back(ack(final)); r.c.tick(++r.now);
  CHECK(r.c.currentSynchronized(0) && (r.c.report().succeeded&1));
  r.dropRegister=0xff; r.finish();

  Rig hot; hot.answerIdentityQueries=false; hot.c.setCurrent(0,200);
  CHECK(hot.c.queue(hot.c.preview({},SettingFields::Current,hot.now),false,hot.now)==Result::Ok);
  hot.step(25,false,false); CHECK(hot.can.writes().empty());
  hot.temperature=70; hot.step(25);
  CHECK(hot.c.effectiveCurrent(0)==60);
  for(const auto& f:hot.can.writes()) if(f.data[1]==protocol::OnlineCurrent)
    CHECK(protocol::readBigEndian(f.data+4)<=protocol::encodeCurrent(60,5000));
  hot.answerIdentityQueries=true; hot.finish(); CHECK(hot.c.report().succeeded==3);

  // A final ACK cannot complete a job whose thermal target changed, including
  // when the temperature frame follows that ACK in the same receive batch.
  for (bool sameBatch : {false,true}) {
    Rig changed(installation(1)); changed.c.setCurrent(0,1000);
    changed.dropRegister=protocol::OnlineCurrent;
    CHECK(changed.c.queue(changed.c.preview({},SettingFields::Current,changed.now),false,changed.now)==Result::Ok);
    for (unsigned n=0;n<40 && changed.can.writes().empty();++n) changed.step(25);
    CHECK(!changed.can.writes().empty()); const auto sent=changed.can.writes().back();
    CHECK(sent.data[1]==protocol::OnlineCurrent && protocol::readBigEndian(sent.data+4)==protocol::encodeCurrent(1000,5000));
    const auto deadlineStart=changed.c.unitControl(0).outputSince;
    if (sameBatch) changed.can.incoming.push_back(ack(sent));
    changed.can.incoming.push_back(data(1,0x7f,70*1024)); changed.c.tick(++changed.now);
    if (!sameBatch) { changed.can.incoming.push_back(ack(sent)); changed.c.tick(++changed.now); }
    CHECK(changed.c.busy() && !changed.c.report().succeeded && !changed.c.currentSynchronized(0));
    CHECK(changed.c.effectiveCurrent(0)==300 && changed.c.unitControl(0).outputSince==deadlineStart);
    changed.temperature=70; changed.dropRegister=0xff; changed.finish();
    uint16_t accepted; CHECK(changed.c.acknowledgedCurrent(0,accepted));
    CHECK(accepted==protocol::encodeCurrent(300,5000) && changed.c.report().succeeded==1);
  }
  // Protection during priming lowers the qualification target; old priming
  // ACKs do not release final limits or extend the readiness deadline.
  Rig primeHot; primeHot.c.setCurrent(0,200); primeHot.dropRegister=protocol::OnlineCurrent;
  for (uint8_t i=0;i<2;++i) primeHot.reportedCurrent[i]=0;
  CHECK(primeHot.c.queue(primeHot.c.preview({},SettingFields::Current,primeHot.now),false,primeHot.now)==Result::Ok);
  for (unsigned n=0;n<40 && primeHot.can.writes().empty();++n) primeHot.step(25);
  CHECK(!primeHot.can.writes().empty()); const auto oldPrime=primeHot.can.writes().back();
  primeHot.can.incoming.push_back(ack(oldPrime)); primeHot.can.incoming.push_back(data(1,0x80,70*1024));
  primeHot.c.tick(++primeHot.now);
  CHECK(primeHot.c.busy() && !primeHot.c.report().succeeded && primeHot.c.effectiveCurrent(0)==60);
  primeHot.temperature=70; primeHot.dropRegister=0xff; primeHot.step(3000);
  CHECK(primeHot.c.syncStatus(primeHot.now).phase==SyncPhase::Waiting && !primeHot.c.syncStatus(primeHot.now).qualified);
  primeHot.reportedCurrent[0]=primeHot.reportedCurrent[1]=58; primeHot.step(2500); primeHot.finish();
  CHECK(primeHot.c.report().succeeded==3 && primeHot.c.effectiveCurrent(0)==60);
}
void currentSyncProfileOwnership() {
  for (bool stop : {false,true}) {
    Rig r; FakeEeprom bytes; MemoryManager memory(bytes);
    CHECK(r.c.setCurrent(0,1000)==Result::Ok);
    CHECK(memory.startProfileSave(0,"SYNC TEST",r.c.configuration(),0)==StorageResult::Ok);
    while (memory.saving()) memory.stepSave();
    CHECK(r.c.setCurrent(0,200)==Result::Ok && memory.save(r.c.configuration())==StorageResult::Ok);
    const auto priorToken=memory.completedToken();
    for (uint8_t i=0;i<2;++i) r.reportedCurrent[i]=0;
    const auto review=r.c.previewProfile(memory,0,false,r.now);
    CHECK(review.blocker==Issue::None && r.c.loadProfile(memory,review,r.now)==Result::Ok);
    for (unsigned n=0;n<120 && r.c.syncStatus(r.now).phase!=SyncPhase::Waiting;++n) {
      r.step(); r.c.profileTick(memory); memory.stepSave();
    }
    CHECK(r.c.syncStatus(r.now).phase==SyncPhase::Waiting);
    CHECK(r.c.profileLoad().state==ProfileLoadState::Applying && !memory.saving() && memory.completedToken()==priorToken);
    if (stop) r.c.setAutoResume(false);
    else CHECK(r.c.bypassSync(r.c.reviewSync(r.now),r.now)==Result::Ok);
    r.finish();
    for (unsigned n=0;n<2000;++n) { r.c.profileTick(memory); memory.stepSave(); }
    Configuration saved; CHECK(memory.load(saved)==StorageResult::Ok);
    if (stop) {
      CHECK(r.c.profileLoad().state==ProfileLoadState::Failed && memory.completedToken()==priorToken);
      CHECK(saved.current[0]==200);
    } else {
      CHECK(r.c.profileLoad().state==ProfileLoadState::Complete && memory.completedToken()!=priorToken);
      CHECK(saved.current[0]==1000 && saved.operating.current[0]==1000);
    }
  }
}
