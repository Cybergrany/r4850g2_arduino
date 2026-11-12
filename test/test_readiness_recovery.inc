void loseQualifiedOutput(Rig& r, uint8_t slot=0) {
  CHECK(r.c.unitControl(slot).outputDemand==OutputDemand::Active);
  r.notReady |= 1U<<slot; r.step();
  CHECK(r.c.unitControl(slot).phase!=ControlPhase::Online);
  r.notReady &= UnitMask(~(1U<<slot)); r.finish();
}
void readinessRecoveryBudget() {
  auto cfg=installation(2); draftGroupCurrent(cfg,0,1000);
  Rig r(cfg); r.reportAc=true; r.apply(); r.finish();
  for(uint8_t n=0;n<control::outputReadyRecoveries;++n) {
    loseQualifiedOutput(r);
    CHECK(r.c.readinessLosses(0)==n+1 && r.c.readinessLosses(1)==0);
    CHECK(r.c.unitControl(0).fault==CommandState::Idle);
    CHECK(r.c.unitControl(0).outputDemand==OutputDemand::Active);
    r.step(3000); CHECK(r.c.readinessLosses(0)==n+1); // Qualification never renews the budget.
    r.c.setAutoResume(false); r.finish(); r.c.setAutoResume(true); r.finish();
    CHECK(r.c.readinessLosses(0)==n+1); // Neither serial nor switch enable edges reset it.
  }
  // A link/route reset retains the prior failures as well.
  r.live=2; r.step(r.c.staleMs()+250); r.live=3; r.step(3000); r.finish();
  CHECK(r.c.readinessLosses(0)==control::outputReadyRecoveries);
  loseQualifiedOutput(r);
  CHECK(r.c.readinessLosses(0)==control::outputReadyRecoveries+1);
  CHECK(r.c.unitControl(0).fault==CommandState::ReadinessRecoveryLimit && r.c.unitControl(0).standbyConfirmed);
  CHECK(r.c.unitControl(1).outputDemand==OutputDemand::Active && r.c.readinessLosses(1)==0);
  const auto stopped=r.can.writes().size(); r.step(30000);
  for(size_t n=stopped;n<r.can.writes().size();++n) if(protocol::address(r.can.writes()[n].id)==1) {
    const auto f=r.can.writes()[n];
    CHECK(f.data[1]==protocol::FanMode || (f.data[1]==protocol::Standby && f.data[3]));
  }
  r.c.setAutoResume(false); r.finish(); r.c.setAutoResume(true); r.step(5000);
  CHECK(r.c.unitControl(0).fault==CommandState::ReadinessRecoveryLimit);
  r.live=2; r.step(r.c.staleMs()+250); r.live=3; r.step(3000); r.finish();
  CHECK(r.c.unitControl(0).fault==CommandState::ReadinessRecoveryLimit && r.c.unitControl(0).standbyConfirmed);
  r.noAc=1; r.step(1000); r.noAc=0; r.finish();
  CHECK(r.c.unitControl(0).fault==CommandState::ReadinessRecoveryLimit);
  // Loading the same identities cannot clear the exhausted budget or its fault.
  auto saved=r.c.configuration(); CHECK(r.c.configure(saved,true)==Result::Ok); r.finish();
  CHECK(r.c.unitControl(0).fault==CommandState::ReadinessRecoveryLimit);
  r.c.configureStartup(true,true,r.now); r.step(control::inhibitMs+5000);
  CHECK(r.c.unitControl(0).fault==CommandState::ReadinessRecoveryLimit); // Internal AC reconciliation is not Retry.
  // Reordering retains identity-specific history; it is never assigned to the replacement.
  std::swap(saved.units[0],saved.units[1]); CHECK(r.c.configure(saved)==Result::Ok);
  CHECK(r.c.readinessLosses(0)==0 && r.c.readinessLosses(1)==control::outputReadyRecoveries+1);
  CHECK(r.c.unitControl(1).fault==CommandState::ReadinessRecoveryLimit);
  std::swap(saved.units[0],saved.units[1]); CHECK(r.c.configure(saved)==Result::Ok);
  r.c.reconcile(); CHECK(!r.c.readinessLosses(0) && !r.c.readinessLosses(1));
  r.finish(); CHECK(!r.c.enableControl().enabled() && r.current[0]==0 && r.current[1]==0);
  CHECK(r.c.unitControl(0).fault==CommandState::Idle);

  // Counter semantics do not depend on millis phase or the first startup window.
  Rig wrap(zeroHoldConfiguration(),false,true,UINT32_MAX-15000); wrap.apply(); wrap.finish();
  for(unsigned n=0;n<=control::outputReadyRecoveries;++n) loseQualifiedOutput(wrap);
  CHECK(wrap.c.unitControl(0).fault==CommandState::ReadinessRecoveryLimit);

  Rig failed(zeroHoldConfiguration()); failed.apply(); failed.finish();
  for(unsigned n=0;n<control::outputReadyRecoveries;++n) loseQualifiedOutput(failed);
  failed.dropRegister=protocol::Standby; loseQualifiedOutput(failed);
  CHECK(failed.c.unitControl(0).fault==CommandState::ReadinessRecoveryLimit && !failed.c.unitControl(0).standbyConfirmed);
  const auto failedWrites=failed.can.writes().size(); failed.step(30000);
  for(size_t n=failedWrites;n<failed.can.writes().size();++n) CHECK(failed.can.writes()[n].data[1]==protocol::FanMode);
  failed.c.setAutoResume(false); CHECK(failed.c.controlStatus(failed.now).activity==Activity::StopUnconfirmed);

  Rig cutoff(zeroHoldConfiguration()); cutoff.apply(); cutoff.finish();
  cutoff.c.configureStartup(true,true,cutoff.now); cutoff.step(control::inhibitMs+5000); cutoff.finish();
  for(unsigned n=0;n<=control::outputReadyRecoveries;++n) loseQualifiedOutput(cutoff);
  CHECK(cutoff.c.unitControl(0).fault==CommandState::ReadinessRecoveryLimit && cutoff.c.startupStatus().permit==PermitState::Fault);
  cutoff.c.setAutoResume(false); cutoff.c.setAutoResume(true); cutoff.step(5000);
  CHECK(!cutoff.c.chargeAllowed() && cutoff.c.readinessLosses(0)==control::outputReadyRecoveries+1);
}
void readinessRecoveryUi() {
  Rig r(zeroHoldConfiguration()); r.apply(); r.finish();
  for(unsigned n=0;n<=control::outputReadyRecoveries;++n) loseQualifiedOutput(r);
  r.step(); CHECK(r.c.alerts().error());
  Terminal t(r);
  CHECK(t.input("diag control\n").find("LATCHED=readiness restart limit reached")!=std::string::npos);
  CHECK(t.input("alerts\n").find("Output readiness restart limit reached")!=std::string::npos);
  CHECK(t.input("diag bus\n").find("output-overruns=0")!=std::string::npos);
  UiRig u(r);
  const auto code=r.c.alerts().at(0).code;
  CHECK(code==AlertCode::Control || code==AlertCode::ActiveLost);
  openIncident(u,code,0); const auto incident=u.ui.alertDisplayId();
  recoverySnapshot(u,"recovery-readiness-limit-alert");
  u.ui.action({ActionKind::Acknowledge},++r.now);
  CHECK(r.c.alerts().at(0).acknowledged && r.c.alerts().error());
  CHECK(r.c.readinessLosses(0)==control::outputReadyRecoveries+1 && r.c.unitControl(0).fault==CommandState::ReadinessRecoveryLimit);
  u.ui.action({ActionKind::Alerts},++r.now); u.ui.action({ActionKind::OpenAlert,0},++r.now);
  CHECK(u.ui.alertDisplayId()==incident);
  u.ui.action({ActionKind::Recover},++r.now); CHECK(u.ui.page()==UiPage::Detail);
  recoverySnapshot(u,"recovery-readiness-limit-details");
  u.ui.action({ActionKind::RecoveryReview},++r.now);
  CHECK(u.ui.page()==UiPage::RecoveryConfirm && u.ui.recoverySelection()==0);
  recoverySnapshot(u,"recovery-readiness-limit-retry");
  u.ui.input({0,true,false},++r.now); CHECK(u.ui.page()==UiPage::Detail); // Cancel selected initially.
  CHECK(r.c.readinessLosses(0)==control::outputReadyRecoveries+1);
  u.ui.action({ActionKind::RecoveryReview},++r.now);
  r.c.setAutoResume(false); u.ui.action({ActionKind::RecoveryConfirm},++r.now);
  CHECK(u.ui.recoveryResult()==Result::Changed && r.c.readinessLosses(0)==control::outputReadyRecoveries+1);
  r.finish(); u.ui.action({ActionKind::RecoveryReview},++r.now);
  u.ui.input({1,false,false},++r.now); u.ui.input({0,true,false},++r.now);
  CHECK(!r.c.readinessLosses(0) && !r.c.enableControl().enabled());
  r.finish(); CHECK(r.c.unitControl(0).phase==ControlPhase::Held && r.current[0]==0);
  CHECK(r.c.alerts().at(0).acknowledged); // Retry and acknowledgement remain separate.
  CHECK(t.input("diag control\n").find("readiness-losses=0 auto-restart-budget="+std::to_string(control::outputReadyRecoveries))!=std::string::npos);
}
