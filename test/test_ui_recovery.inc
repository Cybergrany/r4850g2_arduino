// Exercise the same model/actions used by touch hit targets and the wheel.
void openIncident(UiRig& u, AlertCode code, uint8_t source) {
  auto& r=u.r; r.c.alerts().reset(source); r.c.alerts().set(source,code,++r.now);
  u.ui.action({ActionKind::Alerts},++r.now);
  uint8_t rank=0; while(rank<AlertManager::Count && u.ui.alertSourceAt(rank)!=source) ++rank;
  CHECK(rank<AlertManager::Count); u.ui.action({ActionKind::OpenAlert,rank},++r.now);
  CHECK(u.ui.page()==UiPage::AlertDetail && u.ui.displayedAlert().code==code);
  ++r.now;
}
void recoverySnapshot(UiRig& u,const char* name) { UiFrame f; UiView::compose(f,u.ui,u.r.c,u.r.now); snapshot(name,f); }
void uiRecoveryAvailability() {
  auto cfg=authorized(false); cfg.groups[0].members=1;
  std::strcpy(cfg.groups[1].name,"GROUP2"); cfg.groups[1].members=2; draftGroupCurrent(cfg, 1, 1000);
  Rig r(cfg,true,false); r.live=0; r.step(18000); UiRig u(r);
  // An editor survives a read-only trip through the affected group's recovery.
  u.ui.action({ActionKind::EditVoltage},++r.now); u.turn(1); CHECK(u.ui.editing()); const auto edit=u.ui.editValue();
  openIncident(u,AlertCode::Missing,1); const auto id=u.ui.alertDisplayId();
  CHECK(u.ui.recoveryLink().kind==RecoveryKind::Availability && u.ui.recoveryLink().slot==1);
  recoverySnapshot(u,"recovery-unavailable-alert");
  u.ui.input({1,false,false},++r.now); CHECK(u.ui.alertSelection()==1);
  u.ui.input({0,true,false},++r.now); CHECK(u.ui.page()==UiPage::Availability && u.ui.group()==1);
  CHECK(!r.c.alerts().at(1).acknowledged && !r.c.acceptedAbsences() && !r.c.enableControl().enabled());
  recoverySnapshot(u,"recovery-unavailable-config");
  r.c.alerts().set(0,AlertCode::Control,++r.now); u.ui.notifications(++r.now); CHECK(u.ui.page()==UiPage::Availability);
  u.ui.action({ActionKind::AvailabilityIndividuals},++r.now); CHECK(u.ui.availabilitySelected()==2);
  u.ui.action({ActionKind::Back},++r.now); CHECK(u.ui.page()==UiPage::Availability);
  u.ui.action({ActionKind::Back},++r.now); CHECK(u.ui.alertDisplayId()==id && !u.ui.recovering());
  u.ui.action({ActionKind::Acknowledge},++r.now); CHECK(u.ui.editing() && u.ui.editValue()==edit && u.ui.group()==0);
  // Acceptance remains a separate confirmed decision and remembered save.
  u.ui.action({ActionKind::Cancel},++r.now); openIncident(u,AlertCode::Missing,1);
  u.ui.action({ActionKind::Recover},++r.now); u.ui.action({ActionKind::AvailabilityPrimary},++r.now);
  CHECK(u.ui.page()==UiPage::AvailabilityConfirm && u.ui.availabilityPlan().selected==2);
  u.ui.action({ActionKind::AvailabilityConfirm},++r.now); CHECK(r.c.acceptedAbsences()==2);
  CHECK(!r.c.alerts().at(1).acknowledged);
  u.ui.action({ActionKind::Back},++r.now); u.ui.action({ActionKind::Back},++r.now); u.ui.action({ActionKind::Acknowledge},++r.now);
  // Returned PSU routes to Include, never silently back into service.
  r.live=2; r.step(18000); openIncident(u,AlertCode::Returned,1);
  u.ui.action({ActionKind::Recover},++r.now); CHECK(u.ui.availabilityAction()==AvailabilityAction::Include && u.ui.group()==1);
  CHECK(r.c.excluded()&2); recoverySnapshot(u,"recovery-returned-config");
  u.ui.action({ActionKind::Back},++r.now); u.ui.action({ActionKind::Acknowledge},++r.now);
  // Serial configuration invalidates the suspended edit.
  u.ui.action({ActionKind::EditVoltage},++r.now); openIncident(u,AlertCode::Missing,1);
  u.ui.action({ActionKind::Recover},++r.now); CHECK(r.c.setVoltage(5500)==Result::Ok);
  u.ui.action({ActionKind::Back},++r.now); u.ui.action({ActionKind::Acknowledge},++r.now);
  CHECK(!u.ui.editing() && u.ui.notice()==UiNotice::Changed);
}
void uiRecoveryGuards() {
  Rig r(authorized(false),true,false); r.finish(); UiRig u(r);
  openIncident(u,AlertCode::Command,0); u.ui.action({ActionKind::Recover},++r.now);
  CHECK(u.ui.page()==UiPage::Detail && u.ui.unit()==0); recoverySnapshot(u,"recovery-unit-details");
  u.ui.action({ActionKind::RecoveryReview},++r.now); CHECK(u.ui.page()==UiPage::RecoveryConfirm && u.ui.recoverySelection()==0);
  recoverySnapshot(u,"recovery-retry-confirm");
  // Default wheel click cancels, and opening any screen does not send writes.
  const auto before=r.can.writes().size(); const auto phase=r.c.unitControl(0).phase;
  u.ui.input({0,true,false},++r.now); CHECK(u.ui.page()==UiPage::Detail && r.c.unitControl(0).phase==phase);
  CHECK(r.can.writes().size()==before);
  u.ui.action({ActionKind::RecoveryReview},++r.now);
  CHECK(r.c.setVoltage(5500)==Result::Ok); u.ui.action({ActionKind::RecoveryConfirm},++r.now);
  CHECK(u.ui.page()==UiPage::Detail && u.ui.recoveryResult()==Result::Changed && r.c.unitControl(0).phase==phase);
  u.ui.action({ActionKind::RecoveryReview},++r.now); u.ui.tick(r.now+=limits::confirmationMs+1);
  CHECK(u.ui.page()==UiPage::Detail && u.ui.recoveryResult()==Result::Changed);
  // A replaced incident never executes the old confirmation.
  u.ui.action({ActionKind::RecoveryReview},++r.now); r.c.alerts().reset(0); r.c.alerts().set(0,AlertCode::Identity,++r.now);
  u.ui.action({ActionKind::RecoveryConfirm},++r.now); CHECK(u.ui.recoveryResult()==Result::Changed);
  CHECK(r.c.unitControl(0).phase==phase);
  u.ui.action({ActionKind::Back},++r.now); u.ui.action({ActionKind::Acknowledge},++r.now); // refresh changed incident
  CHECK(!r.c.alerts().at(0).acknowledged); u.ui.action({ActionKind::Acknowledge},++r.now);
  openIncident(u,AlertCode::Identity,0); u.ui.action({ActionKind::Recover},++r.now); CHECK(u.ui.recoveryKind()==RecoveryKind::Bus);
  recoverySnapshot(u,"recovery-bus");
  u.ui.action({ActionKind::Back},++r.now); u.ui.action({ActionKind::Acknowledge},++r.now);
  r.step(2000); r.finish();
  openIncident(u,AlertCode::Control,0); u.ui.action({ActionKind::Recover},++r.now); u.ui.action({ActionKind::RecoveryReview},++r.now);
  // Binding edits advance revision even if the same observed unit is rebound.
  CHECK(r.c.bind(0,identity(1),5000,r.now)==Result::Ok);
  u.ui.action({ActionKind::RecoveryConfirm},++r.now); CHECK(u.ui.recoveryResult()==Result::Changed);
  r.finish(); u.ui.action({ActionKind::RecoveryReview},++r.now); u.ui.input({1,false,false},++r.now); u.ui.input({0,true,false},++r.now);
  CHECK(r.c.unitControl(0).phase!=ControlPhase::Held && !r.c.enableControl().enabled());
  r.finish(); CHECK(r.current[0]==0 && r.current[1]==0);
}
void uiRecoverySave() {
  Rig r; UiRig u(r); CHECK(u.memory.save(r.c.configuration())==StorageResult::Ok);
  openIncident(u,AlertCode::OlderSave,AlertManager::Storage);
  u.ui.action({ActionKind::Recover},++r.now); CHECK(u.ui.recoveryKind()==RecoveryKind::Storage);
  const auto writes=r.can.writes().size(); recoverySnapshot(u,"recovery-save-review");
  u.ui.action({ActionKind::RecoveryReview},++r.now); u.ui.input({0,true,false},++r.now); CHECK(!u.memory.saving());
  u.ui.action({ActionKind::RecoveryReview},++r.now); u.ui.action({ActionKind::RecoveryConfirm},++r.now);
  CHECK(u.ui.recoverySaving() && !u.ui.recoverySaved());
  u.ui.action({ActionKind::Back},++r.now); CHECK(u.ui.recovering());
  // Newer operator draft cannot be described as saved; it remains unapplied.
  CHECK(r.c.setVoltage(5510)==Result::Ok);
  while(u.memory.saving()) u.memory.stepSave();
  u.ui.tick(++r.now);
  CHECK(!u.ui.recoverySaving() && u.ui.recoveryResult()==Result::Changed && !u.ui.recoverySaved());
  Configuration loaded; CHECK(u.memory.load(loaded)==StorageResult::Ok && loaded.voltage[0]==5400);
  CHECK(r.c.configuration().voltage[0]==5510 && r.can.writes().size()==writes);
  u.ui.action({ActionKind::RecoveryReview},++r.now); CHECK(u.ui.page()==UiPage::Recovery); // refresh RAM revision first
  u.ui.action({ActionKind::RecoveryReview},++r.now); u.ui.action({ActionKind::RecoveryConfirm},++r.now);
  while(u.memory.saving()) u.memory.stepSave();
  u.ui.tick(++r.now);
  CHECK(u.ui.recoverySaved() && u.memory.load(loaded)==StorageResult::Ok && loaded.voltage[0]==5510);
  CHECK(!r.c.alerts().at(AlertManager::Storage).acknowledged && r.can.writes().size()==writes);
  // A competing journal owner blocks a duplicate save without stealing its token.
  CHECK(u.memory.startSave(r.c.configuration())==StorageResult::Ok);
  u.ui.action({ActionKind::RecoveryReview},++r.now); CHECK(u.ui.recoveryResult()==Result::Busy && !u.ui.recoverySaving());
  while(u.memory.saving()) u.memory.stepSave();
  // Failed verification reports a failure; an older valid record survives.
  CHECK(r.c.setVoltage(5600)==Result::Ok); u.ui.action({ActionKind::RecoveryReview},++r.now); u.ui.action({ActionKind::RecoveryReview},++r.now);
  u.bytes.capacity=100; u.ui.action({ActionKind::RecoveryConfirm},++r.now);
  CHECK(u.ui.recoveryResult()==Result::Invalid && !u.ui.recoverySaved());
}
void uiRecoveryResultsAndThermal() {
  Rig off(authorized(false),true,false); off.finish(); UiRig u(off);
  u.ui.action({ActionKind::EditCurrent},++off.now); u.ui.action({ActionKind::Apply},++off.now); u.finish();
  CHECK(u.ui.notice()==UiNotice::Prepared && u.ui.recoveryLink().kind==RecoveryKind::Enable);
  recoverySnapshot(u,"recovery-blocked-result");
  u.ui.action({ActionKind::Recover},++off.now); u.ui.action({ActionKind::RecoveryReview},++off.now);
  off.c.setAutoResume(false); u.ui.action({ActionKind::RecoveryConfirm},++off.now);
  CHECK(!off.c.enableControl().enabled() && u.ui.recoveryResult()==Result::Changed);
  u.ui.action({ActionKind::RecoveryReview},++off.now); u.ui.action({ActionKind::RecoveryConfirm},++off.now);
  CHECK(off.c.enableControl().enabled());
  Rig failedSave; UiRig saved(failedSave); saved.bytes.ignoredAddress=0;
  saved.ui.action({ActionKind::EditCurrent},++failedSave.now); saved.ui.action({ActionKind::Adjust,1},++failedSave.now);
  saved.ui.action({ActionKind::Apply},++failedSave.now); saved.finish(); CHECK(saved.ui.notice()==UiNotice::SaveFailed);
  saved.bytes.ignoredAddress=-1; saved.ui.action({ActionKind::Recover},++failedSave.now);
  CHECK(saved.ui.recoveryKind()==RecoveryKind::Storage);
  saved.ui.action({ActionKind::RecoveryReview},++failedSave.now); saved.ui.action({ActionKind::RecoveryConfirm},++failedSave.now);
  while(saved.memory.saving()) saved.memory.stepSave();
  saved.ui.tick(++failedSave.now); CHECK(saved.ui.recoverySaved()); saved.ui.action({ActionKind::Back},++failedSave.now);
  CHECK(saved.ui.notice()==UiNotice::ConfigSaved); // Never call a save-only retry an Apply.
  Rig hot; hot.temperature=76; hot.step(3000); UiRig h(hot);
  openIncident(h,AlertCode::ThermalStop,AlertManager::Thermal); h.ui.action({ActionKind::Recover},++hot.now);
  CHECK(h.ui.recoveryKind()==RecoveryKind::Temperatures && h.ui.availableRecoveryCommand()==RecoveryCommand::Enable);
  recoverySnapshot(h,"recovery-temperatures");
  UiFrame f; UiView::compose(f,h.ui,hot.c,hot.now); CHECK(rowText(f,1).find("TEMPERATURES PSU 1/")!=std::string::npos);
  h.ui.action({ActionKind::RecoveryReview},++hot.now); h.ui.action({ActionKind::RecoveryConfirm},++hot.now);
  CHECK(!hot.c.enableControl().enabled() && h.ui.recoveryResult()!=Result::Ok);
  Rig sensor; sensor.apply(); sensor.noInputTemperature=1; sensor.step(7000); UiRig s(sensor);
  CHECK(sensor.c.thermalStatus().sensorFaults&1);
  openIncident(s,AlertCode::TemperatureSensor,0); s.ui.action({ActionKind::Recover},++sensor.now);
  CHECK(s.ui.availableRecoveryCommand()==RecoveryCommand::Sensors);
  s.ui.action({ActionKind::RecoveryReview},++sensor.now); s.ui.action({ActionKind::RecoveryConfirm},++sensor.now);
  CHECK(sensor.c.thermalStatus().sensorFaults&1);
  sensor.noInputTemperature=0; sensor.step(3000); s.ui.action({ActionKind::RecoveryReview},++sensor.now);
  s.ui.action({ActionKind::RecoveryConfirm},++sensor.now); CHECK(!(sensor.c.thermalStatus().sensorFaults&1));
}
void decimalFormatting() {
  struct Example {float input; const char* expected;};
  for(const auto e: {Example{54,"54.00"},Example{54.10f,"54.10"},Example{0.5f,"0.50"},Example{-12.34f,"-12.34"},
      Example{1.994f,"1.99"},Example{1.996f,"2.00"},Example{0.004f,"0.00"},Example{0.006f,"0.01"},Example{-0.006f,"-0.01"}}) {
    char out[18]; formatValue(out,sizeof(out),e.input,"",2); CHECK(std::string(out)==e.expected);
  }
  char out[18]; formatValue(out,sizeof(out),54.1f,"V",2); CHECK(std::string(out)=="54.10V");
  formatValue(out,6,54.1f,"V",2); CHECK(std::string(out)=="54.1V");
  formatValue(out,2,54.1f,"V",2); CHECK(std::strlen(out)<=1);
  formatValue(out,sizeof(out),1e30f,"A",2); CHECK(std::string(out)=="OVF");
  formatValue(out,sizeof(out),54.1f,"",1); CHECK(std::string(out)=="54.1");
  UiFrame f; f.clear(); f.largeNumber(6,out);
  CHECK(uint8_t(f.cell(6*26+9).character)==128+5*4);
  CHECK(uint8_t(f.cell(6*26+13).character)==128+10*4);
  CHECK(uint8_t(f.cell(6*26+15).character)==128+1*4);
}
