// Fault injection uses the real controller, scheduler, EEPROM codec and UI model.
void alertEpisodes() {
  AlertManager a;
  a.set(0,AlertCode::Missing,100); const auto first=a.at(0).id;
  CHECK(a.warning() && !a.error()); CHECK(a.notification(100)==first);
  a.set(0,AlertCode::Missing,101); CHECK(a.at(0).id==first); CHECK(!a.notification(102));
  CHECK(a.acknowledge(first)); CHECK(a.warning());
  a.set(1,AlertCode::Command,103); CHECK(a.error() && a.warning());
  CHECK(a.notification(103)==a.at(1).id); a.acknowledgeAll(); CHECK(a.error() && a.warning());
  a.set(0,AlertCode::None,200); CHECK(!a.warning());
  a.set(0,AlertCode::Missing,300); CHECK(a.at(0).id==first && !a.notification(301));
  a.set(0,AlertCode::None,400); a.set(0,AlertCode::None,60400); a.set(0,AlertCode::Missing,60401);
  CHECK(a.at(0).id!=first && a.notification(60401)==a.at(0).id);
  a.set(0,AlertCode::ActiveLost,60402); CHECK(a.notification(60402)==a.at(0).id);
  a.acknowledgeAll();
  for(unsigned i=0;i<100;++i) {
    a.set(0,AlertCode::Returned,61000+i*2); a.set(0,AlertCode::ActiveLost,61001+i*2);
    CHECK(!a.notification(61001+i*2));
  }
  a.event(AlertManager::Receive,AlertCode::ReceiveLoss,UINT32_MAX-100);
  a.expire(AlertManager::Receive,100); CHECK(a.at(AlertManager::Receive).active);
  a.expire(AlertManager::Receive,30000); CHECK(!a.at(AlertManager::Receive).active);
}
void thermalLimitsAndSensors() {
  Rig r; CHECK(r.c.setCurrent(0,4000)==Result::Ok); r.apply();
  const auto revision=r.c.configurationRevision();
  r.temperature=64; r.step(2000); CHECK(r.c.thermalStatus().percent==100);
  r.inputTemperature=65; r.step(4000); CHECK(r.c.thermalStatus().percent==85);
  for(uint8_t i=0;i<2;++i) { CHECK(r.c.effectiveCurrent(i)==1700); uint16_t raw;
    CHECK(r.c.acknowledgedCurrent(i,raw) && raw==protocol::encodeCurrent(1700,5000)); }
  CHECK(r.c.configurationRevision()==revision && r.c.configuration().groups[0].current==4000);
  r.inputTemperature=70; r.step(4000); CHECK(r.c.thermalStatus().percent==30);
  CHECK(r.c.effectiveCurrent(0)==600 && r.c.configuration().operating.current[0]==2000);
  r.inputTemperature=66; r.temperature=60; r.step(1000); CHECK(r.c.thermalStatus().percent==30 && r.c.thermalStatus().hotMs==0);
  r.inputTemperature=59; r.step(1000); CHECK(r.c.thermalStatus().percent==30); // output exactly 60 is not cool
  r.temperature=59; r.step(4000); CHECK(r.c.thermalStatus().percent==100 && r.c.effectiveCurrent(0)==2000);
  // Apply/new authorizations and refreshes always recompute from the base share.
  r.temperature=65; r.step(4000); r.c.setCurrent(0,6000); r.apply(Operation::GroupCurrent,0); r.step(12000);
  CHECK(r.c.effectiveCurrent(0)==2550 && r.c.effectiveCurrent(1)==2550);
  CHECK(r.c.configuration().operating.current[0]==3000);
  r.temperature=30; r.step(5000);
  r.noInputTemperature=1; r.step(7000); CHECK(r.c.thermalStatus().sensorFaults==1);
  CHECK(r.current[0]==0 && r.current[1]>2900 && r.c.enableControl().enabled());
  CHECK(r.c.alerts().at(0).code==AlertCode::TemperatureSensor);
  r.noInputTemperature=0; r.step(5000); CHECK(r.current[0]==0 && r.c.effectiveCurrent(0)==0);
  r.c.alerts().acknowledgeAll(); CHECK(r.c.alerts().error() && r.current[0]==0);
  CHECK(r.c.retryTemperature(1,r.now)==Result::Ok); r.step(7000); CHECK(r.current[0]>2900 && !r.c.thermalStatus().sensorFaults);
  // One hot input still protects when its output sensor is invalid.
  r.noOutputTemperature=1; r.inputTemperature=71; r.step(7000);
  CHECK(r.c.thermalStatus().level==ThermalLevel::Error && r.c.thermalStatus().sensorFaults==1);
  CHECK(r.current[0]==0 && r.c.effectiveCurrent(1)==900);
  Rig fan(authorized(false),true,false); fan.temperature=70; fan.finish();
  CHECK(fan.c.setFanOverride(3,FanOverride::Auto,fan.now)==Result::Ok); fan.step(2000);
  CHECK(fan.c.unitControl(0).fanRequested==FanMode::Max && fan.c.unitControl(0).fanAcknowledged==FanMode::Max);
  // A broad sensor retry must not retry a failed setting on a healthy sensor.
  Rig rejected; rejected.c.setCurrent(0,2000); rejected.apply();
  rejected.c.setCurrent(0,4000); rejected.rejectRegister=protocol::OnlineCurrent;
  CHECK(rejected.c.queue(rejected.c.preview(Operation::All,-1,rejected.now),false,rejected.now)==Result::Ok);
  rejected.finish(); CHECK(rejected.c.report().failed==3);
  rejected.rejectRegister=0xff; CHECK(rejected.c.retryTemperature(3,rejected.now)==Result::Ok);
  rejected.step(15000); CHECK(rejected.current[0]>900 && rejected.current[0]<1100 && rejected.current[1]>900 && rejected.current[1]<1100);
  CHECK(rejected.c.alerts().at(0).code==AlertCode::Command);
}
void thermalTimingAndAbsence() {
  Rig r(installation(),false,true,UINT32_MAX-10000);
  r.c.setCurrent(0,2000); r.apply(); r.temperature=70; r.step(3000);
  const auto initial=r.c.thermalStatus().hotMs; CHECK(initial>=2500);
  r.noTemperature=3; r.step(r.c.staleMs()+500); const auto paused=r.c.thermalStatus().hotMs;
  r.step(5000); CHECK(r.c.thermalStatus().hotMs==paused && paused>=initial);
  r.noTemperature=2; r.temperature=69; r.step(1000); CHECK(r.c.thermalStatus().hotMs==paused); // unknown partner pauses
  r.noTemperature=0; r.step(1000); CHECK(r.c.thermalStatus().hotMs==0);
  r.temperature=70; r.step(2000); r.noTemperature=2; r.step(5000);
  CHECK(r.c.thermalStatus().hotMs>=6500); // known hot peer keeps timer running
  r.temperature=30; r.noTemperature=0; r.step(3000);
  Rig hot; hot.c.setCurrent(0,2000); hot.apply(); hot.unitTemperature[1]=70; hot.step(3000);
  hot.live=1; hot.step(10000); CHECK(hot.c.thermalStatus().percent==30 && (hot.c.thermalStatus().contributors&2));
  FakeEeprom bytes; MemoryManager memory(bytes);
  auto accept=hot.c.previewAvailability(AvailabilityAction::Accept,2,false,hot.now);
  CHECK(accept.blocker==Issue::None && hot.c.confirmAvailability(accept,memory,hot.now)==Result::Ok);
  hot.step(1000); CHECK(hot.c.thermalStatus().percent==100);
  CHECK(hot.c.alerts().at(1).code==AlertCode::Excluded);
  hot.live=3; hot.step(5000); CHECK(hot.c.thermalStatus().percent==30); // returning hot PSU is checked again
  auto cfg=authorized(); cfg.acceptedAbsent=2;
  Rig remembered(cfg,true,false); remembered.unitTemperature[1]=70; remembered.step(10000);
  remembered.live=1; remembered.step(10000);
  CHECK(remembered.c.thermalStatus().percent==30 && (remembered.c.thermalStatus().contributors&2));
  accept=remembered.c.previewAvailability(AvailabilityAction::Accept,2,false,remembered.now);
  CHECK(accept.blocker==Issue::None && remembered.c.confirmAvailability(accept,memory,remembered.now)==Result::Ok);
  remembered.step(1000); CHECK(remembered.c.thermalStatus().percent==100);
  Rig duration; duration.temperature=70; duration.step(alerts::errorMs-250); CHECK(duration.c.configuration().thermalStop==ThermalStop::None);
  duration.step(1000); CHECK(duration.c.configuration().thermalStop==ThermalStop::Duration && !duration.c.enableControl().enabled());
  Rig boundary; boundary.temperature=75; boundary.step(2000); CHECK(boundary.c.configuration().thermalStop==ThermalStop::None);
  boundary.temperature=76; boundary.step(); CHECK(boundary.c.configuration().thermalStop==ThermalStop::Temperature);
}
void serviceThermal(Rig& r, MemoryManager& memory, unsigned loops=500) {
  for(unsigned i=0;i<loops;++i) { r.step(20); r.c.serviceProtection(memory,r.now); memory.stepSave(); r.c.serviceProtection(memory,r.now); }
}
void thermalPersistence() {
  Rig r; r.c.setCurrent(0,2000); r.apply(); FakeEeprom bytes; MemoryManager memory(bytes);
  CHECK(memory.save(r.c.configuration())==StorageResult::Ok);
  r.c.setVoltage(5700); // unsaved operator draft must not be persisted by stop
  r.temperature=76; r.step(); CHECK(!r.c.enableControl().enabled()); serviceThermal(r,memory);
  Configuration saved; CHECK(memory.load(saved)==StorageResult::Ok);
  CHECK(saved.thermalStop==ThermalStop::Temperature && !saved.autoResume && saved.voltage==5400);
  CHECK(r.c.configuration().voltage==5700 && r.c.thermalStatus().persisted);
  Rig reboot(saved,true,false); reboot.c.sampleEnableInput(true,reboot.now); reboot.step(1000); reboot.c.sampleEnableInput(true,reboot.now);
  CHECK(!reboot.c.enableControl().enabled()); reboot.finish();
  CHECK(!reboot.c.enableControl().enabled());
  auto clean=saved; clean.thermalStop=ThermalStop::None; clean.autoResume=true;
  CHECK(reboot.c.configure(clean,true)==Result::Ok && !reboot.c.enableControl().enabled());
  CHECK(reboot.c.configuration().thermalStop==ThermalStop::Temperature);
  r.temperature=30; r.step(5000); CHECK(r.c.thermalStatus().cool);
  CHECK(r.c.setAutoResume(true)==Result::Ok && !r.c.enableControl().enabled());
  r.c.serviceProtection(memory,r.now); CHECK(memory.saving());
  for(unsigned i=0;i<60;++i) memory.stepSave();
  r.c.setAutoResume(false); serviceThermal(r,memory);
  CHECK(!r.c.enableControl().enabled()); CHECK(memory.load(saved)==StorageResult::Ok && saved.thermalStop!=ThermalStop::None);
  CHECK(r.c.setAutoResume(true)==Result::Ok); serviceThermal(r,memory);
  CHECK(r.c.enableControl().enabled() && r.c.configuration().thermalStop==ThermalStop::None);
  CHECK(memory.load(saved)==StorageResult::Ok && saved.thermalStop==ThermalStop::None && !saved.autoResume);
  // Every interrupted protection write retains either complete prior state or
  // the complete stopped state; no torn hybrid and no calibration bytes changed.
  const auto baseline=bytes;
  for(int cut=0;cut<=218;++cut) {
    bytes=baseline; bytes.calls=0; bytes.stopAfter=cut; MemoryManager trial(bytes);
    CHECK(trial.startProtectionSave(ThermalStop::Duration)==StorageResult::Ok);
    try { while(trial.saving()) trial.stepSave(); } catch(const PowerCut&) {}
    bytes.stopAfter=-1; MemoryManager reader(bytes); Configuration cfg;
    CHECK(reader.load(cfg)==StorageResult::Ok && !cfg.autoResume && cfg.voltage==5400);
    CHECK(cfg.thermalStop==ThermalStop::None || cfg.thermalStop==ThermalStop::Duration);
    for(uint16_t i=0;i<512;++i) if((i>=216&&i<256)||i>=472) CHECK(bytes.bytes[i]==baseline.bytes[i]);
  }
  bytes=baseline; bytes.ignoredAddress=0; // force failure separately using all-write suppression below
  Rig failed; failed.temperature=76; failed.step(); FakeEeprom broken; broken.capacity=100; MemoryManager bad(broken);
  failed.c.serviceProtection(bad,failed.now); failed.step();
  CHECK(failed.c.thermalStatus().saveFailed && failed.c.alerts().at(AlertManager::Storage).code==AlertCode::Storage);
}
void thermalQueuedChanges() {
  Rig r; r.c.setCurrent(0,4000); r.apply();
  r.temperature=70; r.step(250,false); // protection identity check / current target can be pending
  r.c.setAutoResume(false); r.step(10000); CHECK(!r.current[0] && !r.current[1]);
  r.temperature=30; r.step(3000); r.c.setAutoResume(true); r.step(10000); CHECK(r.current[0]>1900);
  r.temperature=70; r.rejectRegister=protocol::OnlineCurrent; r.step(10000);
  CHECK(r.c.unitControl(0).fault!=CommandState::Idle && r.c.alerts().error());
  // A reduction inside output qualification never restarts its deadline.
  Rig starting; starting.reportAc=true; starting.outputReadyDelay=7000; starting.c.setCurrent(0,4000);
  CHECK(starting.c.queue(starting.c.preview(Operation::All,-1,starting.now),false,starting.now)==Result::Ok);
  for(unsigned i=0;i<80 && starting.c.unitControl(0).outputDemand!=OutputDemand::Starting;++i) starting.step();
  CHECK(starting.c.unitControl(0).outputDemand==OutputDemand::Starting);
  const auto since=starting.c.unitControl(0).outputSince;
  starting.temperature=70; starting.step(1500);
  CHECK(starting.c.unitControl(0).outputSince==since && starting.c.effectiveCurrent(0)==600);
  starting.step(10000); CHECK(starting.current[0]<=600 && starting.c.unitControl(0).fault==CommandState::Idle);
}
void alertUiAndSerial() {
  // A startup missing-members popup has one wheel action. Acknowledge returns
  // to the overview without accepting absences, enabling output or saving.
  Rig bench; FakeEeprom benchBytes; MemoryManager benchMemory(benchBytes);
  CHECK(bench.c.setAutoResume(false)==Result::Ok);
  UiModel benchUi(bench.c,benchMemory); benchUi.tick(bench.now);
  bench.c.alerts().acknowledgeAll(); bench.c.alerts().reset(AlertManager::Startup);
  bench.c.alerts().set(AlertManager::Startup,AlertCode::Missing,++bench.now);
  const auto revision=bench.c.configurationRevision();
  const auto accepted=bench.c.acceptedAbsences();
  benchUi.notifications(bench.now); CHECK(benchUi.page()==UiPage::AlertDetail);
  benchUi.input({1,false,false},++bench.now);
  CHECK(benchUi.page()==UiPage::AlertDetail && benchUi.alertSelection()==0);
  benchUi.input({0,true,false},++bench.now);
  CHECK(benchUi.page()==UiPage::Groups && bench.c.alerts().at(AlertManager::Startup).acknowledged);
  CHECK(bench.c.alerts().at(AlertManager::Startup).active && bench.c.alerts().warning());
  CHECK(bench.c.acceptedAbsences()==accepted && bench.c.configurationRevision()==revision);
  CHECK(!bench.c.enableControl().enabled() && !benchMemory.saving());

  Rig r; FakeEeprom bytes; MemoryManager memory(bytes); UiModel model(r.c,memory); model.tick(r.now);
  model.action({ActionKind::Configure},r.now); model.input({0,true,false},r.now); model.input({1,false,false},r.now);
  const auto edit=model.editValue(); const auto underlying=model.page();
  r.c.alerts().acknowledgeAll(); r.c.alerts().set(AlertManager::Thermal,AlertCode::TemperatureWarning,r.now);
  model.notifications(++r.now); CHECK(model.page()==UiPage::AlertDetail && model.editValue()==edit);
  const auto displayed=model.alertDisplayId(); model.action({ActionKind::Acknowledge},r.now); CHECK(model.alertDisplayId()==displayed);
  r.c.alerts().set(0,AlertCode::Command,++r.now); model.notifications(r.now); CHECK(model.alertDisplayId()==displayed);
  model.action({ActionKind::Acknowledge},++r.now); CHECK(model.page()==underlying && model.editValue()==edit);
  model.notifications(++r.now); CHECK(model.page()==underlying);
  CHECK(!r.c.alerts().at(0).acknowledged && r.c.alerts().at(0).active);
  model.notifications(r.now+=alerts::popupMs); CHECK(model.page()==underlying);
  r.c.alerts().set(AlertManager::Thermal,AlertCode::ThermalStop,++r.now);
  model.notifications(++r.now); CHECK(model.page()==UiPage::AlertDetail);
  model.action({ActionKind::Acknowledge},++r.now); CHECK(model.page()==underlying);
  // Calibration defers notifications and explicit lists are not availability pages.
#if PSU_ENABLE_TOUCH
  model.action({ActionKind::Cancel},++r.now); model.action({ActionKind::Calibrate},++r.now);
  r.c.alerts().set(AlertManager::Startup,AlertCode::Cutoff,++r.now); model.notifications(r.now);
  CHECK(model.page()==UiPage::Calibration);
  model.action({ActionKind::Back},++r.now);
#endif
  model.action({ActionKind::Alerts},++r.now);
  CHECK(model.page()==UiPage::Alerts && !model.availabilityPage());
}

void thermalAckRacesAndMigration() {
  Rig r; r.c.setCurrent(0,2000); r.apply();
  r.c.setCurrent(0,4000); CHECK(r.c.queue(r.c.preview(Operation::GroupCurrent,0,r.now),false,r.now)==Result::Ok);
  CanFrame old={}; bool waiting=false;
  for(unsigned n=0;n<100 && !waiting;++n) {
    r.step(100,false);
    for(const auto& f:r.can.writes()) if(protocol::address(f.id)==1 && f.data[1]==protocol::OnlineCurrent &&
        protocol::readBigEndian(f.data+4)==protocol::encodeCurrent(2000,5000)) { old=f; waiting=true; }
  }
  CHECK(waiting); r.temperature=70; r.heartbeat(); r.c.tick(++r.now);
  r.can.incoming.push_back(ack(old)); r.c.tick(++r.now);
  CHECK(!r.c.currentSynchronized(0));
  uint16_t raw; CHECK(r.c.acknowledgedCurrent(0,raw) && raw==protocol::encodeCurrent(2000,5000));
  r.step(8000); CHECK(r.c.acknowledgedCurrent(0,raw) && raw==protocol::encodeCurrent(600,5000));
  // Overtake an unsent 85% target while its INFO guard is outstanding.
  Rig identityWait; identityWait.c.setCurrent(0,4000); identityWait.apply();
  identityWait.temperature=65; identityWait.step(100,false);
  const auto before=identityWait.can.writes().size(); identityWait.temperature=70; identityWait.step(6000);
  const auto writes=identityWait.can.writes();
  for(size_t i=before;i<writes.size();++i) if(writes[i].data[1]==protocol::OnlineCurrent)
    CHECK(protocol::readBigEndian(writes[i].data+4)<=protocol::encodeCurrent(600,5000));
  // Schema 2 and 3 migrate with no thermal latch; a schema-4 stop keeps size and tails.
  for(uint8_t schema=2;schema<=3;++schema) {
    FakeEeprom bytes; MemoryManager memory(bytes); auto cfg=authorized();
    CHECK(memory.save(cfg)==StorageResult::Ok); bytes.bytes[1]=schema; recordCrc(bytes.bytes.data(),MemoryManager::recordSize);
    Configuration loaded; CHECK(memory.load(loaded)==StorageResult::Ok && loaded.thermalStop==ThermalStop::None && loaded.autoResume);
    CHECK(memory.startProtectionSave(ThermalStop::Temperature)==StorageResult::Ok);
    while(memory.saving()) memory.stepSave();
    CHECK(memory.load(loaded)==StorageResult::Ok && loaded.thermalStop==ThermalStop::Temperature && !loaded.autoResume);
    CHECK(bytes.bytes[257]==4 && MemoryManager::recordSize==216);
  }
  // Calibration/normal save gate: stop wins after the old snapshot finishes.
  Rig saving; FakeEeprom bytes; MemoryManager memory(bytes);
  CHECK(memory.save(saving.c.configuration())==StorageResult::Ok);
  saving.c.setVoltage(5600); CHECK(memory.startSave(saving.c.configuration())==StorageResult::Ok);
  saving.temperature=76; saving.step(); saving.c.serviceProtection(memory,saving.now);
  CHECK(!saving.c.thermalStatus().persisted); serviceThermal(saving,memory);
  Configuration saved; CHECK(memory.load(saved)==StorageResult::Ok && saved.thermalStop==ThermalStop::Temperature && saved.voltage==5600);
  saving.temperature=30; saving.step(4000); CHECK(saving.c.setAutoResume(true)==Result::Ok);
  memory.auxiliaryBusy(true); saving.c.serviceProtection(memory,saving.now); CHECK(!saving.c.enableControl().enabled());
  memory.auxiliaryBusy(false); saving.c.serviceProtection(memory,saving.now); CHECK(memory.saving());
  for(unsigned n=0;n<100;++n) memory.stepSave();
  saving.temperature=76; saving.step(); serviceThermal(saving,memory);
  CHECK(!saving.c.enableControl().enabled() && memory.load(saved)==StorageResult::Ok && saved.thermalStop!=ThermalStop::None);
  // Automatic thermal OFF preserves the editor's unsaved value.
  Rig editor; FakeEeprom editBytes; MemoryManager editMemory(editBytes); UiModel model(editor.c,editMemory);
  model.tick(editor.now); model.action({ActionKind::Configure},editor.now); model.input({0,true,false},editor.now);
  model.input({1,false,false},editor.now); const auto edit=model.editValue();
  editor.temperature=76; editor.step(); model.tick(editor.now);
  CHECK(model.editing() && model.editValue()==edit);
}
void alertSerialCommands() {
  Rig r; r.c.setCurrent(0,2000); r.apply(); Terminal terminal(r);
  r.temperature=65; r.step(5000);
  auto output=terminal.input("alerts\n"); CHECK(output.find("Temperature warning")!=std::string::npos);
  CHECK(output.find("All active limits reduced")!=std::string::npos);
  const auto id=r.c.alerts().at(AlertManager::Thermal).id;
  CHECK(terminal.input("alerts ack "+std::to_string(id)+"\n").find("Acknowledgement only")!=std::string::npos);
  CHECK(r.c.alerts().warning() && r.c.alerts().at(AlertManager::Thermal).acknowledged);
  terminal.input("hello\n"); terminal.input(std::string(1,char(24))); terminal.input("watch off\n");
  CHECK(r.c.alerts().at(AlertManager::Thermal).acknowledged);
  output=terminal.input("diag thermal\n"); CHECK(output.find("factor=85%")!=std::string::npos);
  CHECK(output.find("authorized/effective/acknowledged A=10.00/8.50/")!=std::string::npos);
  CHECK(terminal.input("help\n").find("alerts ack ID|all")!=std::string::npos);
  CHECK(terminal.input("alerts ack 0\n").find("BLOCKED")!=std::string::npos);
  CHECK(terminal.input("diag bus\n").find("output-overruns=0")!=std::string::npos);
}

void activeIdentityAlert() {
  Rig r; r.c.setCurrent(0,2000); r.apply();
  r.can.incoming.push_back(info(1,identity(9))); r.c.tick(++r.now);
  CHECK(r.c.alerts().at(0).code==AlertCode::Identity && r.c.alerts().error());
  r.c.alerts().acknowledgeAll(); CHECK(r.c.alerts().error());
  Rig losses; losses.can.losses=UINT8_MAX; losses.can.receiveLoss=true; losses.step();
  CHECK(losses.c.alerts().at(AlertManager::Receive).active);
  losses.step(alerts::recentMs-1000); losses.can.receiveLoss=true; losses.step();
  losses.step(2000); CHECK(losses.c.alerts().at(AlertManager::Receive).active);
  losses.step(alerts::recentMs); CHECK(!losses.c.alerts().at(AlertManager::Receive).active);
  CHECK(losses.can.losses==UINT8_MAX);
}
