// Participation is intent, not a live readiness report. Use the production
// journal/controller/UI, including failures between review and activation.
void finishParticipation(Rig& r, MemoryManager& memory) {
  while (memory.saving()) { memory.stepSave(); r.step(1); }
  r.step(1);
}
void participationPersistence() {
  auto cfg=authorized(false); cfg.acceptedAbsent=2;
  Rig r(cfg,true,false); r.finish(); r.step(18000); FakeEeprom bytes; MemoryManager memory(bytes);
  CHECK(memory.save(cfg)==StorageResult::Ok);
  auto p=r.c.previewAvailability(AvailabilityAction::Include,2,true,r.now);
  CHECK(p.blocker==Issue::None && r.c.confirmAvailability(p,memory,r.now)==Result::Ok);
  CHECK(r.c.excluded()==2 && r.c.configuration().acceptedAbsent==2);
  finishParticipation(r,memory);
  CHECK(r.c.availabilityReport().remembered && r.c.excluded()==0 && r.c.configuration().acceptedAbsent==0);
  Configuration loaded; CHECK(memory.load(loaded)==StorageResult::Ok && !loaded.disabled && !loaded.acceptedAbsent);
  Rig reboot(loaded,true,false); CHECK(reboot.c.participants()==3 && reboot.c.requiredMembers()==3);
  // Session exclusion is stable across returning AC, but never leaks into V/A saves.
  p=r.c.previewAvailability(AvailabilityAction::Exclude,2,false,r.now);
  CHECK(p.blocker==Issue::None && r.c.confirmAvailability(p,memory,r.now)==Result::Ok);
  CHECK(r.c.excluded()==2 && !r.c.configuration().disabled && !r.c.participationStatus(1).remembered);
  r.live=1; r.step(12000); r.live=3; r.step(12000); r.finish();
  CHECK(r.c.excluded()==2 && r.c.availabilityAttention(r.now)==0 && r.current[1]==0);
  UiRig u(r); u.config(); u.click(); u.turn(1); u.click(); u.finish();
  CHECK(memory.save(r.c.configuration())==StorageResult::Ok && memory.load(loaded)==StorageResult::Ok);
  CHECK(!loaded.disabled && !loaded.acceptedAbsent && r.c.excluded()==2);
  CHECK(u.memory.load(loaded)==StorageResult::Ok && !loaded.disabled && !loaded.acceptedAbsent);
  const auto authorizedCurrent = r.c.configuration().operating.current[0];
  CHECK(r.c.set({ScopeKind::Unit,0},SettingFields::Current,1234)==Result::Ok);
  // Persist an already active session decision, then include for this session.
  p=r.c.previewAvailability(AvailabilityAction::Exclude,2,true,r.now);
  CHECK(p.blocker==Issue::None && r.c.confirmAvailability(p,memory,r.now)==Result::Ok); finishParticipation(r,memory);
  CHECK(r.c.configuration().disabled==2 && r.c.participationStatus(1).remembered);
  p=r.c.previewAvailability(AvailabilityAction::Include,2,false,r.now);
  CHECK(p.blocker==Issue::None && r.c.confirmAvailability(p,memory,r.now)==Result::Ok);
  CHECK(!r.c.excluded() && !r.c.acceptedAbsences() && r.c.configuration().disabled==2);
  CHECK(memory.save(r.c.configuration())==StorageResult::Ok && memory.load(loaded)==StorageResult::Ok && loaded.disabled==2);
  // Explicitly require a missing unit, clearing both kinds of remembered waiver.
  r.live=1; r.step(12000);
  p=r.c.previewAvailability(AvailabilityAction::Require,2,true,r.now);
  CHECK(p.blocker==Issue::None && p.participants==3 && r.c.confirmAvailability(p,memory,r.now)==Result::Ok);
  finishParticipation(r,memory); CHECK(memory.load(loaded)==StorageResult::Ok && !loaded.disabled && !loaded.acceptedAbsent);
  CHECK(r.c.requiredMembers()==3 && r.c.availabilityStatus(0,r.now).state==AvailabilityState::Review);
  // Saving ALL participation replaces only the reviewed policy, not drafts/authorizations.
  CHECK(loaded.operating.current[0]==authorizedCurrent && loaded.current[0]==1234);
}
void participationAtomicity() {
  Rig r(authorized(false),true,false); r.finish(); r.step(18000);
  FakeEeprom bytes; MemoryManager memory(bytes); CHECK(memory.save(r.c.configuration())==StorageResult::Ok);
  auto p=r.c.previewParticipation(3,1,true,r.now); CHECK(p.blocker==Issue::None && p.participants==2);
  CHECK(r.c.confirmAvailability(p,memory,r.now)==Result::Ok);
  r.c.setAutoResume(false); // Even a newer OFF while already OFF invalidates the review.
  finishParticipation(r,memory);
  CHECK(r.c.availabilityReport().result==Result::Changed && r.c.availabilityReport().remembered && !r.c.excluded());
  Configuration saved; CHECK(memory.load(saved)==StorageResult::Ok && saved.disabled==1);
  // Failed writes cannot change either runtime intent or the live saved-policy fields.
  bytes.ignoredAddress=512+32;
  p=r.c.previewParticipation(3,2,true,r.now); CHECK(r.c.confirmAvailability(p,memory,r.now)==Result::Ok);
  finishParticipation(r,memory); CHECK(r.c.availabilityReport().storage==StorageResult::WriteFailed && !r.c.excluded() && !r.c.configuration().disabled);
  bytes.ignoredAddress=-1;
  // Whole-selection validation is atomic: an available member requires OFF/zero.
  r.c.setAutoResume(true); r.finish(); r.step(6000);
  p=r.c.previewParticipation(3,1,false,r.now); CHECK(p.blocker==Issue::ParticipationHold);
  CHECK(r.c.confirmAvailability(p,memory,r.now)==Result::Blocked && !r.c.excluded());
  r.c.setAutoResume(false);
  CHECK(r.c.previewParticipation(3,1,false,r.now).blocker!=Issue::None); // OFF alone is insufficient.
  r.finish(); r.step(6000);
  p=r.c.previewParticipation(3,1,false,r.now); CHECK(p.blocker==Issue::None);
  CHECK(r.c.confirmAvailability(p,memory,r.now+limits::confirmationMs+1)==Result::Changed && !r.c.excluded());
  // Calibration owns the same storage writer; do not lose a concurrent decision.
  TouchCalibration calibration; calibration.valid=true; calibration.coefficient[0]=calibration.coefficient[4]=8192;
  CalibrationStore calibrationStore(bytes,memory); CHECK(calibrationStore.start(calibration)==StorageResult::Ok);
  p=r.c.previewParticipation(3,1,true,r.now);
  CHECK(r.c.confirmAvailability(p,memory,r.now)==Result::Blocked && !r.c.excluded());
  while(calibrationStore.saving()) calibrationStore.step();
  CHECK(calibrationStore.result()==StorageResult::Ok);
  // Every interrupted intent save preserves a coherent old or new policy pair.
  FakeEeprom interrupted; MemoryManager journal(interrupted); auto original=authorized(false); original.acceptedAbsent=2;
  CHECK(journal.save(original)==StorageResult::Ok); const auto baseline=interrupted.bytes;
  for(int cut=0; cut<MemoryManager::recordSize+12; ++cut) {
    interrupted.bytes=baseline; interrupted.calls=0; interrupted.stopAfter=cut;
    MemoryManager writer(interrupted);
    try { CHECK(writer.startAvailabilitySave(original,0,1)==StorageResult::Ok); while(writer.saving()) writer.stepSave(); } catch(const PowerCut&) {}
    interrupted.stopAfter=-1; MemoryManager reader(interrupted); Configuration value;
    CHECK(reader.load(value)==StorageResult::Ok);
    CHECK((value.acceptedAbsent==2 && value.disabled==0) || (value.acceptedAbsent==0 && value.disabled==1));
  }
  // A protection event during a participation write wins; no later ON or inclusion.
  p=r.c.previewParticipation(3,1,true,r.now); CHECK(r.c.confirmAvailability(p,memory,r.now)==Result::Ok);
  r.temperature=76; r.step(); r.c.serviceProtection(memory,r.now);
  for(unsigned i=0;i<800;++i) { memory.stepSave(); r.step(1); r.c.serviceProtection(memory,r.now); }
  CHECK(r.c.thermalStatus().stop!=ThermalStop::None && !r.c.excluded());
  CHECK(r.c.availabilityReport().result==Result::Changed && !r.c.enableControl().enabled());
  CHECK(memory.load(saved)==StorageResult::Ok && saved.thermalStop!=ThermalStop::None && !saved.autoResume);
}
void participationProfiles() {
  // All eight slots: current confirmed session choices are captured, and an
  // included identity clears a legacy waiver in the final verified boot record.
  for(uint8_t slot=0;slot<profiles::count;++slot) {
    auto cfg=authorized(false); cfg.acceptedAbsent=2;
    Rig r(cfg,true,false); r.finish(); r.step(18000); FakeEeprom bytes; MemoryManager memory(bytes);
    auto p=r.c.previewAvailability(AvailabilityAction::Include,2,false,r.now);
    CHECK(r.c.confirmAvailability(p,memory,r.now)==Result::Ok && r.c.configuration().acceptedAbsent==2);
    CHECK(memory.startProfileSave(slot,"INCLUDE",r.c.configuration(),0,true,r.c.participants())==StorageResult::Ok);
    while(memory.saving()) memory.stepSave();
    auto review=r.c.previewProfile(memory,slot,true,r.now); CHECK(review.blocker==Issue::None);
    CHECK(r.c.loadProfile(memory,review,r.now)==Result::Ok && r.c.configuration().acceptedAbsent==2);
    for(unsigned i=0;i<MemoryManager::recordSize+20;++i) { r.c.profileTick(memory); memory.stepSave(); }
    CHECK(r.c.profileLoad().state==ProfileLoadState::Complete && !r.c.profileLoad().older);
    Configuration saved; CHECK(memory.load(saved)==StorageResult::Ok && !saved.acceptedAbsent && !saved.disabled);
    CHECK(!r.c.configuration().acceptedAbsent && r.c.participationStatus(1).remembered);
    Rig reboot(saved,true,false); CHECK(reboot.c.participants()==3);
  }
  // Participation Yes also works with an ON profile whose intent is unchanged.
  Rig on(authorized(),true,false); on.finish(); on.step(18000); FakeEeprom onBytes; MemoryManager onMemory(onBytes);
  auto targets=on.c.configuration(); targets.voltage[0]=targets.voltage[1]=5600; targets.current[0]=targets.current[1]=800;
  CHECK(onMemory.startProfileSave(0,"SAME MEMBERS",targets,0,true,3)==StorageResult::Ok);
  while(onMemory.saving()) onMemory.stepSave();
  auto onReview=on.c.previewProfile(onMemory,0,true,on.now); CHECK(onReview.blocker==Issue::None);
  CHECK(on.c.loadProfile(onMemory,onReview,on.now)==Result::Ok);
  for(unsigned i=0;i<1000;++i) { on.step(20); on.c.profileTick(onMemory); onMemory.stepSave(); }
  CHECK(on.c.profileLoad().state==ProfileLoadState::Complete && !on.c.profileLoad().older);
  // Failed profile persistence does not silently promote runtime choices.
  Rig r(authorized(false),true,false); r.finish(); r.step(18000); FakeEeprom bytes; MemoryManager memory(bytes);
  CHECK(memory.startProfileSave(0,"ONLY ONE",r.c.configuration(),0,true,1)==StorageResult::Ok);
  while(memory.saving()) memory.stepSave();
  auto review=r.c.previewProfile(memory,0,true,r.now); CHECK(review.blocker==Issue::None);
  CHECK(r.c.loadProfile(memory,review,r.now)==Result::Ok && r.c.participants()==3);
  bytes.ignoredAddress=0;
  for(unsigned i=0;i<MemoryManager::recordSize+20;++i) { r.c.profileTick(memory); memory.stepSave(); }
  CHECK(r.c.profileLoad().state==ProfileLoadState::Failed && !r.c.configuration().disabled && r.c.participants()==3);
  // A newer OFF during the final profile save prevents participation activation,
  // even though the reviewed boot snapshot was successfully stored.
  Rig stale(authorized(false),true,false); stale.finish(); stale.step(18000);
  FakeEeprom staleBytes; MemoryManager staleMemory(staleBytes);
  CHECK(staleMemory.startProfileSave(0,"ONE",stale.c.configuration(),0,true,1)==StorageResult::Ok);
  while(staleMemory.saving()) staleMemory.stepSave();
  const auto staleReview=stale.c.previewProfile(staleMemory,0,true,stale.now);
  CHECK(stale.c.loadProfile(staleMemory,staleReview,stale.now)==Result::Ok);
  stale.c.profileTick(staleMemory); CHECK(stale.c.profileLoad().state==ProfileLoadState::Saving);
  stale.c.setAutoResume(false);
  for(unsigned i=0;i<MemoryManager::recordSize+20;++i) { stale.c.profileTick(staleMemory); staleMemory.stepSave(); }
  CHECK(stale.c.profileLoad().state==ProfileLoadState::Complete && stale.c.profileLoad().older);
  CHECK(stale.c.participants()==3 && !stale.c.configuration().disabled && !stale.c.enableControl().enabled());
  Configuration staleSaved; CHECK(staleMemory.load(staleSaved)==StorageResult::Ok && staleSaved.disabled==2);
  // Profiles cannot erase a hot missing-unit incident; explicit fresh review can.
  bytes.ignoredAddress=-1; r.unitTemperature[1]=70; r.step(1500); r.live=1; r.step(12000);
  CHECK(r.c.thermalStatus().contributors==2);
  review=r.c.previewProfile(memory,0,true,r.now); CHECK(review.blocker==Issue::None);
  CHECK(r.c.loadProfile(memory,review,r.now)==Result::Ok);
  for(unsigned i=0;i<MemoryManager::recordSize+20;++i) { r.c.profileTick(memory); memory.stepSave(); }
  r.step(); CHECK(r.c.participants()==1 && r.c.thermalStatus().contributors==2);
  r.step(6000);
  auto p=r.c.previewAvailability(AvailabilityAction::Exclude,2,false,r.now);
  CHECK(p.blocker==Issue::None && r.c.confirmAvailability(p,memory,r.now)==Result::Ok);
  r.step(); CHECK(!r.c.thermalStatus().contributors && r.c.thermalStatus().level==ThermalLevel::Normal);
}
void participationUiViewport() {
  Rig r(installation(9),false,false); r.finish(); r.step(18000); UiRig u(r); UiFrame f;
  u.ui.action({ActionKind::Startup},r.now); u.click(); CHECK(u.ui.page()==UiPage::Availability);
  u.ui.action({ActionKind::AvailabilityIndividuals},r.now);
  CHECK(u.ui.availabilityTop()==0 && !u.ui.availabilitySelected());
  for(uint8_t rank=0;rank<9;++rank) {
    if(rank) u.turn(1);
    CHECK(u.ui.availabilityFocus()==rank && rank>=u.ui.availabilityTop() && rank<u.ui.availabilityTop()+4);
    u.click(); CHECK(u.ui.availabilitySelected()==membersMask(rank+1));
    UiView::compose(f,u.ui,r.c,r.now);
  }
  CHECK(u.ui.availabilityTop()==5); snapshot("availability-scroll-bottom",f);
  CHECK(u.memory.startProfileSave(0,"NO DRAFT",r.c.configuration(),0,true,r.c.participants())==StorageResult::Ok);
  while(u.memory.saving()) u.memory.stepSave();
  ProfileInfo info; CHECK(u.memory.profileInfo(0,info)==StorageResult::Ok && info.included==0x1ff); // Unconfirmed checkboxes ignored.
  u.turn(1); CHECK(u.ui.availabilityFocus()==9); u.turn(1); u.click();
  CHECK(u.ui.page()==UiPage::AvailabilityConfirm && u.ui.availabilityPlan().remember && u.ui.availabilityPlan().participants==0);
  u.ui.action({ActionKind::AvailabilityRemember},r.now);
  u.ui.action({ActionKind::Back},r.now); CHECK(u.ui.page()==UiPage::AvailabilityUnits && u.ui.availabilitySelected()==0x1ff && u.ui.availabilityTop()==5);
  u.ui.action({ActionKind::AvailabilityReview},r.now); CHECK(!u.ui.availabilityPlan().remember);
  u.ui.action({ActionKind::AvailabilityConfirm},r.now); CHECK(!u.ui.working() && r.c.participants()==0);
  CHECK(r.c.configuration().disabled==0 && u.ui.availabilityResult()==Result::Ok);
  // Cancel discards an unconfirmed change. A later alert can bookmark a draft.
  u.ui.action({ActionKind::Back},r.now); u.ui.action({ActionKind::AvailabilityIndividuals},r.now); u.click();
  CHECK(u.ui.availabilitySelected()==0x1fe); u.ui.action({ActionKind::Back},r.now);
  u.ui.action({ActionKind::AvailabilityIndividuals},r.now); CHECK(u.ui.availabilitySelected()==0x1ff);
  u.turn(8); u.click(); const auto draft=u.ui.availabilitySelected();
  openIncident(u,AlertCode::Excluded,8); const auto incident=u.ui.alertDisplayId();
  u.ui.action({ActionKind::Recover},++r.now); CHECK(u.ui.page()==UiPage::AvailabilityOverview);
  u.ui.action({ActionKind::Back},++r.now); CHECK(u.ui.alertDisplayId()==incident);
  u.ui.action({ActionKind::Acknowledge},++r.now);
  CHECK(u.ui.page()==UiPage::AvailabilityUnits && u.ui.availabilitySelected()==draft && u.ui.availabilityTop()==5 && u.ui.availabilityFocus()==8);
  r.c.setVoltage(5600); u.ui.tick(r.now); CHECK(u.ui.page()==UiPage::AvailabilityResult && u.ui.availabilityResult()==Result::Changed);
  // More than three groups: wheel pages the tiles, yet Controller stays reachable.
  auto cfg=installation(9); cfg.groups[0].members=0x3f;
  for(uint8_t i=1;i<4;++i) { cfg.groups[i].members=UnitMask(1U<<(5+i)); std::snprintf(cfg.groups[i].name,sizeof(cfg.groups[i].name),"GROUP%u",i+1); }
  Rig many(cfg,false,false); many.finish(); many.step(18000); UiRig pages(many);
  pages.ui.action({ActionKind::Startup},many.now); pages.turn(3);
  CHECK(pages.ui.groupRank()==3 && pages.ui.availabilityFocus()==3); UiView::compose(f,pages.ui,many.c,many.now); snapshot("availability-overview-page2",f);
  pages.turn(1); pages.click(); CHECK(pages.ui.page()==UiPage::Controller);
  // Empty commissioning still exposes probe, retry, enable, stop and serial help.
  auto empty=defaultConfiguration(); Rig commissioning(empty,false,false); commissioning.c.configureStartup(true,false,commissioning.now);
  UiRig emptyUi(commissioning); emptyUi.hold(); emptyUi.click(); // Global config remains reachable without groups.
  CHECK(emptyUi.ui.page()==UiPage::Config); emptyUi.turn(1); emptyUi.click(); CHECK(emptyUi.ui.page()==UiPage::Tools);
  emptyUi.click(); CHECK(emptyUi.ui.page()==UiPage::AvailabilityOverview);
  UiView::compose(f,emptyUi.ui,commissioning.c,commissioning.now); snapshot("availability-empty",f);
  emptyUi.click(); CHECK(emptyUi.ui.page()==UiPage::Controller);
  emptyUi.ui.action({ActionKind::Probe},commissioning.now); CHECK(emptyUi.ui.page()==UiPage::AvailabilityConfirm && !emptyUi.ui.availabilityPlan().remember);
  CHECK(emptyUi.ui.availabilitySelection()==0); emptyUi.click(); CHECK(emptyUi.ui.page()==UiPage::Controller && !commissioning.c.startupStatus().commissioning);
}

void participationChangedGroup() {
  // Latched faults require action even when the availability reason otherwise
  // looks like reconciliation. Acknowledgement must not make them Settled.
  Rig sensor(authorized(),true,false); sensor.finish(); sensor.step(18000);
  sensor.noInputTemperature=1; sensor.step(10000);
  CHECK(sensor.c.thermalStatus().sensorFaults==1);
  CHECK(sensor.c.availabilityStatus(0,sensor.now).state==AvailabilityState::Review);
  sensor.c.alerts().acknowledgeAll();
  CHECK(sensor.c.availabilityStatus(0,sensor.now).state==AvailabilityState::Review);
  Rig thermal(authorized(false),true,false); thermal.finish(); thermal.step(18000);
  thermal.temperature=76; thermal.step(1000);
  CHECK(thermal.c.thermalStatus().stop!=ThermalStop::None);
  CHECK(thermal.c.availabilityStatus(0,thermal.now).state==AvailabilityState::Review);
  Rig r(authorized(false),true,false); r.finish(); r.step(18000); UiRig u(r);
  u.ui.action({ActionKind::Startup},r.now); u.click(); u.ui.action({ActionKind::AvailabilityIndividuals},r.now); u.click();
  CHECK(r.c.removeGroup(0)==Result::Ok); u.ui.tick(r.now);
  CHECK(u.ui.page()==UiPage::AvailabilityResult && u.ui.availabilityResult()==Result::Changed);
  CHECK(!r.c.excluded() && !u.memory.saving());
}
