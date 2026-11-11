// Settings have no stored override hierarchy. Each accepted field update writes
// its complete scope, and the shared preview/apply path consumes those targets.
void scopedSettings() {
  auto cfg=installation(3); cfg.groups[0].members=3;
  std::strcpy(cfg.groups[1].name,"SECOND"); cfg.groups[1].members=4;
  Rig r(cfg,false,false); r.finish();
  const SettingScope global, group(ScopeKind::Group,0), unit(ScopeKind::Unit,1);
  CHECK(r.c.set(global,SettingFields::Current,5001)==Result::Ok);
  CHECK(r.c.configuration().current[0]==2501 && r.c.configuration().current[1]==2500 && r.c.configuration().current[2]==5001);
  CHECK(r.c.set(global,SettingFields::Voltage,5500)==Result::Ok);
  CHECK(r.c.set(group,SettingFields::Voltage,5600)==Result::Ok);
  CHECK(r.c.set(unit,SettingFields::Voltage,5700)==Result::Ok);
  CHECK(r.c.configuration().voltage[0]==5600 && r.c.configuration().voltage[1]==5700 && r.c.configuration().voltage[2]==5500);
  CHECK(r.c.set(unit,SettingFields::Current,1000)==Result::Ok);
  auto values=settingSummary(r.c.configuration(),global,SettingFields::Current);
  CHECK(values.minimum==3501 && values.maximum==5001);
  CHECK(settingSummary(r.c.configuration(),group,SettingFields::Current).custom);
  const auto before=r.c.configuration(); const auto revision=r.c.configurationRevision();
  CHECK(r.c.set(global,SettingFields::Current,6001)==Result::Invalid);
  CHECK(r.c.configurationRevision()==revision); checkSameProfile(r.c.configuration(),before);
  CHECK(r.c.set(group,SettingFields::Current,3001)==Result::Ok);
  CHECK(r.c.configuration().current[0]==1501 && r.c.configuration().current[1]==1500);
  CHECK(!settingSummary(r.c.configuration(),group,SettingFields::Current).custom);
  auto plan=r.c.preview(global,SettingFields::Both,r.now); const auto writes=r.can.writes().size();
  CHECK(plan.prepared && plan.blocker==Issue::None && !plan.recipients);
  CHECK(r.c.queue(plan,false,r.now)==Result::Ok && !r.c.busy() && r.can.writes().size()==writes);
  CHECK(!r.c.enableControl().enabled() && r.c.configuration().operating.voltage[1]==5700);
  r.finish(); CHECK(r.current[0]==0 && r.current[1]==0);
  CHECK(r.c.setAutoResume(true)==Result::Ok); r.finish();
  CHECK(r.c.currentSynchronized(0) && r.c.currentSynchronized(1) && r.c.currentSynchronized(2));
  bool seen[3]={};
  for(const auto& f:r.can.writes()) if(f.data[1]==protocol::OnlineVoltage) {
    const auto i=protocol::address(f.id)-1;
    if(i<3 && protocol::readBigEndian(f.data+4)==protocol::encodeVoltage(r.c.configuration().operating.voltage[i])) seen[i]=true;
  }
  CHECK(seen[0] && seen[1] && seen[2]);
  CHECK(r.c.set(unit,SettingFields::Current,900)==Result::Ok);
  plan=r.c.preview(unit,SettingFields::Current,r.now); CHECK(plan.blocker==Issue::None);
  CHECK(r.c.queue(plan,false,r.now)==Result::Ok); r.finish();
  CHECK(r.c.configuration().operating.current[0]==1501 && r.c.configuration().operating.current[1]==900);
  CHECK(r.c.configuration().operating.voltage[1]==5700);
  // Membership changes preserve each configured allocation.
  const auto current0=r.c.configuration().current[0];
  CHECK(r.c.setGroup("GROUP1",1)==Result::Ok);
  CHECK(groupCurrent(r.c.configuration(),0)==current0 && r.c.configuration().current[1]==900);
}
void profileRecords() {
  FakeEeprom bytes; MemoryManager memory(bytes); auto cfg=installation(9);
  for(uint8_t i=0;i<cfg.count;++i) { cfg.voltage[i]=4800+i*50; cfg.current[i]=i*100; }
  for(uint8_t slot=0;slot<profiles::count;++slot) {
    CHECK(memory.startProfileSave(slot,slot==0 ? "BENCH 1_TEST" : nullptr,cfg,0,true,0x101)==StorageResult::Ok);
    while(memory.saving()) memory.stepSave();
    CHECK(memory.completedResult()==StorageResult::Ok);
    ProfileInfo info; CHECK(memory.profileInfo(slot,info)==StorageResult::Ok && info.count==9 && info.hasInclusion && info.included==0x101);
    CHECK(std::strlen(info.name)<=12);
    uint16_t v[9]={},a[9]={}; uint8_t mismatch; UnitMask included;
    CHECK(memory.profileTargets(slot,info.generation,cfg,v,a,mismatch,&included)==StorageResult::Ok && included==0x101);
    for(uint8_t i=0;i<9;++i) CHECK(v[i]==cfg.voltage[i] && a[i]==cfg.current[i]);
  }
  CHECK(memory.startProfileSave(0,"BAD!",cfg,1)==StorageResult::InvalidConfig);
  CHECK(memory.startProfileSave(0,"TOO LONG NAME!",cfg,1)==StorageResult::InvalidConfig);
  CHECK(memory.startProfileSave(0,"TEST",cfg,0)==StorageResult::Changed);
  auto reordered=cfg; std::swap(reordered.units[0],reordered.units[8]);
  uint16_t v[9]={},a[9]={}; uint8_t mismatch; UnitMask included;
  CHECK(memory.profileTargets(0,1,reordered,v,a,mismatch,&included)==StorageResult::Ok);
  CHECK(v[0]==cfg.voltage[8] && v[8]==cfg.voltage[0] && a[0]==cfg.current[8]);
  auto participationReorder=cfg; std::swap(participationReorder.units[0],participationReorder.units[1]);
  CHECK(memory.profileTargets(0,1,participationReorder,v,a,mismatch,&included)==StorageResult::Ok && included==0x102);
  auto replaced=cfg; replaced.units[0].identity=identity(50);
  CHECK(memory.profileTargets(0,1,replaced,v,a,mismatch)==StorageResult::IdentityMismatch);
  auto smaller=cfg; --smaller.count;
  CHECK(memory.profileTargets(0,1,smaller,v,a,mismatch)==StorageResult::IdentityMismatch);
  auto rating=cfg; rating.units[8].ratedCurrent=100;
  CHECK(memory.profileTargets(0,1,rating,v,a,mismatch)==StorageResult::InvalidConfig);
  auto foreign=cfg; ++foreign.deploymentId;
  CHECK(memory.profileTargets(0,1,foreign,v,a,mismatch)==StorageResult::WrongDeployment);
  // Every interrupted replacement retains either the complete old copy or the
  // complete new copy, including names and participation, with calibration intact.
  const auto original=bytes.bytes; auto changed=cfg; changed.current[0]=123;
  for(int cut=0;cut<=profiles::recordBytes+2;++cut) {
    bytes.bytes=original; bytes.calls=0; bytes.stopAfter=cut;
    try { MemoryManager writing(bytes); CHECK(writing.startProfileSave(0,"NEW",changed,1,false)==StorageResult::Ok); while(writing.saving()) writing.stepSave(); }
    catch(const PowerCut&) {}
    bytes.stopAfter=-1; MemoryManager reading(bytes); ProfileInfo info;
    CHECK(reading.profileInfo(0,info)==StorageResult::Ok);
    CHECK(reading.profileTargets(0,info.generation,cfg,v,a,mismatch)==StorageResult::Ok);
    CHECK((info.generation==1 && a[0]==0 && info.hasInclusion) || (info.generation==2 && a[0]==123 && !info.hasInclusion));
    for(unsigned i=0;i<1536;++i) CHECK(bytes.bytes[i]==original[i]);
  }
  bytes.bytes=original; bytes.bytes[profiles::base+36]^=1;
  ProfileInfo info; CHECK(memory.profileInfo(0,info)==StorageResult::NoValidRecord);
  bytes.bytes=original;
  CHECK(memory.startProfileSave(0,"PREEMPT",cfg,1)==StorageResult::Ok);
  for(int i=0;i<20;++i) memory.stepSave();
  const auto token=memory.saveToken(); CHECK(memory.cancelProfileSave());
  CHECK(memory.completedToken()==token && memory.completedResult()==StorageResult::Cancelled);
  CHECK(memory.profileInfo(0,info)==StorageResult::Ok && info.generation==1);
}
void profileLoadAndParticipation() {
  Rig r(installation(),false,false); r.finish(); r.step(18000); r.finish();
  FakeEeprom bytes; MemoryManager memory(bytes); auto saved=r.c.configuration(); saved.current[0]=800; saved.current[1]=1200;
  saved.voltage[0]=5500; saved.voltage[1]=5700;
  CHECK(memory.startProfileSave(0,"SOURCE ONE",saved,0,true,1)==StorageResult::Ok);
  while(memory.saving()) memory.stepSave();
  auto review=r.c.previewProfile(memory,0,false,r.now); CHECK(review.storage==StorageResult::Ok && review.blocker==Issue::None);
  CHECK(r.c.loadProfile(memory,review,r.now)==Result::Ok);
  for(unsigned n=0;n<MemoryManager::recordSize+8;++n) { r.c.profileTick(memory); memory.stepSave(); }
  CHECK(r.c.profileLoad().state==ProfileLoadState::Complete && r.c.profileLoad().prepared);
  CHECK(r.c.participants()==3 && !r.c.enableControl().enabled()); // Default NO leaves choices alone.
  Configuration persisted; CHECK(memory.load(persisted)==StorageResult::Ok && persisted.current[0]==800 && persisted.operating.voltage[1]==5700);
  r.finish(); for(unsigned n=0;n<60;++n) r.step(250); review=r.c.previewProfile(memory,0,true,r.now);
  if(review.blocker!=Issue::None) std::cerr << "profile participation blocker=" << unsigned(review.blocker) << " phase=" << unsigned(r.c.unitControl(1).phase) << " zero=" << r.c.unitControl(1).zeroConfirmed << " reason=" << unsigned(r.c.availabilityReason(1,r.now)) << " now=" << r.now << "\n";
  CHECK(review.blocker==Issue::None);
  CHECK(r.c.loadProfile(memory,review,r.now)==Result::Ok);
  CHECK(r.c.participants()==3 && r.c.configuration().disabled==0); // Verified save gates participation.
  for(unsigned n=0;n<MemoryManager::recordSize+8;++n) { r.c.profileTick(memory); memory.stepSave(); }
  CHECK(memory.load(persisted)==StorageResult::Ok && persisted.disabled==2);
  Rig reboot(persisted,true,false); reboot.finish(); CHECK(reboot.c.participants()==1 && reboot.current[1]==0);
  CHECK(r.c.setAutoResume(true)==Result::Ok); r.finish();
  auto full=saved; CHECK(memory.startProfileSave(1,"BOTH",full,0,true,3)==StorageResult::Ok);
  while(memory.saving()) memory.stepSave();
  review=r.c.previewProfile(memory,1,true,r.now); CHECK(review.blocker==Issue::ParticipationHold);
  CHECK(r.c.loadProfile(memory,review,r.now)==Result::Blocked && r.c.participants()==1);
  CHECK(r.c.setAutoResume(false)==Result::Ok); r.finish(); for(unsigned n=0;n<60;++n) r.step(250); r.finish();
  review=r.c.previewProfile(memory,1,true,r.now); CHECK(review.blocker==Issue::None);
  CHECK(r.c.setVoltage(5600)==Result::Ok);
  CHECK(r.c.loadProfile(memory,review,r.now)==Result::Changed && r.c.participants()==1);
  review=r.c.previewProfile(memory,1,true,r.now);
  CHECK(r.c.loadProfile(memory,review,r.now)==Result::Ok && r.c.participants()==1);
  for(unsigned n=0;n<MemoryManager::recordSize+8;++n) { r.c.profileTick(memory); memory.stepSave(); }
  CHECK(memory.load(persisted)==StorageResult::Ok && persisted.disabled==0);
  // Newer OFF/config revisions invalidate an in-progress load's auto-save.
  r.finish(); review=r.c.previewProfile(memory,1,false,r.now);
  CHECK(r.c.loadProfile(memory,review,r.now)==Result::Ok);
  CHECK(r.c.setAutoResume(false)==Result::Ok); r.c.profileTick(memory);
  CHECK(r.c.profileLoad().state==ProfileLoadState::Failed && !memory.saving());
}
void profileUiAndSerial() {
  Rig r(installation(),false,false); r.finish(); Terminal t(r);
  CHECK(t.input("set voltage psu:1 55.1\n").find("OK")!=std::string::npos);
  CHECK(t.input("set current all 20\n").find("TOTAL PER GROUP")!=std::string::npos);
  CHECK(t.input("apply all\n").find("Prepared for next ON")!=std::string::npos);
  CHECK(t.input("profile save 1 BENCH ONE\n").find("default NO")!=std::string::npos);
  CHECK(t.input("profile inclusion yes\n").find("YES")!=std::string::npos);
  CHECK(t.input("confirm yes\n").find("EEPROM OK")!=std::string::npos);
  CHECK(t.input("profiles\n").find("BENCH ONE")!=std::string::npos);
  CHECK(t.input("profile load 1\n").find("store/restore=NO")!=std::string::npos);
  CHECK(t.input("confirm no\n").find("Cancelled")!=std::string::npos);
  UiRig u(r); u.hold(); CHECK(u.ui.page()==UiPage::ConfigMenu);
  UiFrame frame; UiView::compose(frame,u.ui,r.c,r.now); snapshot("settings-menu",frame);
  u.turn(4); u.click(); CHECK(u.ui.page()==UiPage::ProfileList && u.ui.savingProfile());
  UiView::compose(frame,u.ui,r.c,r.now); snapshot("profile-list",frame);
  u.click(); CHECK(u.ui.page()==UiPage::ProfileOptions && !u.ui.profileInclusion());
  UiView::compose(frame,u.ui,r.c,r.now); snapshot("profile-options",frame);
  u.ui.action({ActionKind::ProfileInclusion},++r.now); CHECK(u.ui.profileInclusion());
  const std::string originalName=u.ui.profileName();
  u.ui.action({ActionKind::ProfileName},++r.now);
  u.ui.action({ActionKind::Adjust,1},++r.now); u.ui.action({ActionKind::Back},++r.now);
  CHECK(originalName==u.ui.profileName()); // Cancel discards name edits only.
  CHECK(u.ui.profileInclusion());
  u.ui.action({ActionKind::ProfileName},++r.now);
  for (uint8_t i=0;i<profiles::nameLength;++i) {
    while (u.ui.profileName()[i]!=' ') u.ui.action({ActionKind::Adjust,1},++r.now);
    u.ui.action({ActionKind::NameCursor,1},++r.now);
  }
  u.ui.action({ActionKind::NameDone},++r.now);
  CHECK(std::string(u.ui.profileName())=="PROFILE 1"); // Blank names get a useful default.
  u.ui.action({ActionKind::ProfileName},++r.now); CHECK(u.ui.page()==UiPage::ProfileName);
  UiView::compose(frame,u.ui,r.c,r.now); snapshot("profile-name",frame);
  u.ui.action({ActionKind::Adjust,1},++r.now); u.ui.action({ActionKind::NameDone},++r.now);
  CHECK(std::string(u.ui.profileName())=="QROFILE 1"); // No padding leaks into the draft.
  u.ui.action({ActionKind::ProfileReview},++r.now); CHECK(u.ui.settingsSelection()==0);
  UiView::compose(frame,u.ui,r.c,r.now); snapshot("profile-save-review",frame);
  u.click(); CHECK(u.ui.page()==UiPage::ProfileOptions); // Cancel starts selected.
  u.ui.action({ActionKind::ProfileReview},++r.now); u.ui.action({ActionKind::ProfileConfirm},++r.now); u.finish();
  CHECK(u.ui.notice()==UiNotice::ProfileSaved);
  ProfileInfo info; CHECK(u.memory.profileInfo(0,info)==StorageResult::Ok && info.hasInclusion);
  u.click(); CHECK(u.ui.page()==UiPage::ProfileList);
  u.ui.action({ActionKind::Back},++r.now); u.ui.action({ActionKind::SettingsChoice,0},++r.now);
  CHECK(u.ui.page()==UiPage::Config && u.ui.configured(SettingFields::Voltage).minimum!=u.ui.configured(SettingFields::Voltage).maximum);
  UiView::compose(frame,u.ui,r.c,r.now); snapshot("settings-global-mixed",frame);
  u.ui.action({ActionKind::EditCurrent},++r.now); u.ui.action({ActionKind::Apply},++r.now);
  CHECK(u.ui.page()==UiPage::ScopeConfirm && u.ui.settingsSelection()==0);
  UiView::compose(frame,u.ui,r.c,r.now); snapshot("settings-global-review",frame);
  u.click(); CHECK(u.ui.page()==UiPage::Config); // Cancel changes no draft.
  u.ui.action({ActionKind::SettingsChoice,2},++r.now);
  UiView::compose(frame,u.ui,r.c,r.now); snapshot("settings-individual-list",frame);
  u.ui.action({ActionKind::SettingsItem,0},++r.now);
  CHECK(u.ui.scope().kind==ScopeKind::Unit);
  UiView::compose(frame,u.ui,r.c,r.now); snapshot("settings-individual",frame);
  u.ui.action({ActionKind::SettingsChoice,3},++r.now); u.ui.action({ActionKind::SettingsItem,0},++r.now);
  CHECK(!u.ui.profileInclusion()); u.ui.action({ActionKind::ProfileReview},++r.now);
  UiView::compose(frame,u.ui,r.c,r.now); snapshot("profile-load-review",frame);
  u.ui.action({ActionKind::ProfileConfirm},++r.now); u.finish();
  CHECK(u.ui.notice()==UiNotice::Prepared && !r.c.enableControl().enabled());
  CHECK(r.c.unbind(0)==Result::Ok);
  CHECK(t.input("profile load 1\n").find("Unmatched saved identity=")!=std::string::npos);
  const auto shown=t.input("profile show 1\n");
  CHECK(shown.find("Participation stored=YES")!=std::string::npos && shown.find("Included saved entries=1,2")!=std::string::npos);
  CHECK(t.input("group delete GROUP1\n").find("OK")!=std::string::npos);
  CHECK(t.input("count 1\n").find("OK")!=std::string::npos);
  CHECK(t.input("profile show 1\n").find("entry 2 id=")!=std::string::npos); // Inspect all saved IDs even after count changes.
}
FakeEeprom schemaFive(const Configuration& c,uint8_t capacity) {
  FakeEeprom bytes; auto* b=bytes.bytes.data(); const uint16_t size=116+10*capacity+(capacity+1)/2;
  std::memset(b,0,size); b[0]=0xa5; b[1]=5; put16(b+2,size); b[4]=7;
  std::memcpy(b+10,"R48C",4); b[14]=bool(c.operating.voltageMask); b[15]=capacity;
  for(uint8_t i=0;i<4;++i) b[16+i]=c.deploymentId>>(8*i);
  put16(b+20,c.voltage[0]); put16(b+22,c.offlineVoltage); put16(b+24,c.pollMs);
  b[26]=c.count; b[27]=uint8_t(c.autoResume)|(uint8_t(c.thermalStop)<<1);
  put16(b+28,c.acceptedAbsent); put16(b+30,c.operating.voltageMask); put16(b+32,c.operating.currentMask); put16(b+34,c.operating.voltage[0]);
  uint16_t at=36;
  for(uint8_t g=0;g<limits::maxGroups;++g) {
    for(uint8_t k=0;k<8;++k) {
      const char ch=c.groups[g].name[k];
      const uint8_t code=ch>='A' && ch<='Z' ? ch-'A'+1 : ch>='a' && ch<='z' ? ch-'a'+27 : ch>='0' && ch<='9' ? ch-'0'+53 : ch=='_' ? 63 : 0;
      const uint8_t bit=k*6; b[at+bit/8]|=code<<(bit%8); if(bit%8>2) b[at+bit/8+1]|=code>>(8-bit%8);
    }
    put16(b+at+6,groupCurrent(c,g)); put16(b+at+8,c.groups[g].offlineCurrent); at+=10;
  }
  for(uint8_t i=0;i<capacity;++i) { std::memcpy(b+at,c.units[i].identity.bytes,6); put16(b+at+6,c.units[i].ratedCurrent); put16(b+at+8,c.operating.current[i]); at+=10; }
  for(uint8_t i=0;i<capacity;++i) b[at+i/2]|=uint8_t(groupFor(c,i)+1)<<((i%2)*4);
  recordCrc(b,size); return bytes;
}
void scopedMigrationAndProtection() {
  for(uint8_t capacity : {8,9}) {
    auto before=authorized(false); before.acceptedAbsent=2; before.thermalStop=ThermalStop::Duration;
    auto bytes=schemaFive(before,capacity); MemoryManager memory(bytes); Configuration loaded;
    CHECK(memory.load(loaded)==StorageResult::Ok); checkSameProfile(loaded,before);
    CHECK(memory.save(loaded)==StorageResult::Ok && bytes.bytes[0]==0 && bytes.bytes[256]==0 && bytes.bytes[513]==6);
    CHECK(memory.load(loaded)==StorageResult::Ok); checkSameProfile(loaded,before);
  }
  // A new-schema build with a smaller compiled capacity must expand before an
  // automatic protection save computes its checksum and claims persistence.
  FakeEeprom bytes; MemoryManager memory(bytes); auto cfg=authorized(false);
  CHECK(memory.save(cfg)==StorageResult::Ok);
  auto* b=bytes.bytes.data()+512; b[14]=8; put16(b+2,230);
  std::memmove(b+226,b+242,4); recordCrc(b,230);
  Configuration loaded; CHECK(memory.load(loaded)==StorageResult::Ok);
  CHECK(memory.startProtectionSave(ThermalStop::Temperature)==StorageResult::Ok);
  while(memory.saving()) memory.stepSave();
  CHECK(memory.saveResult()==StorageResult::Ok && memory.persistedProtection(ThermalStop::Temperature));
  CHECK(memory.load(loaded)==StorageResult::Ok && loaded.thermalStop==ThermalStop::Temperature);
}
void profileEnabledGuards() {
  Rig r; r.c.setCurrent(0,2000); r.apply(); FakeEeprom bytes; MemoryManager memory(bytes);
  auto cfg=r.c.configuration(); cfg.voltage[0]=5500; cfg.voltage[1]=5700; cfg.current[0]=1000; cfg.current[1]=2000;
  CHECK(memory.startProfileSave(0,"ON TEST",cfg,0)==StorageResult::Ok); while(memory.saving()) memory.stepSave();
  auto review=r.c.previewProfile(memory,0,false,r.now); CHECK(review.blocker==Issue::None);
  CHECK(r.c.loadProfile(memory,review,r.now)==Result::Ok);
  CHECK(r.c.loadProfile(memory,review,r.now)==Result::Busy);
  for(unsigned i=0;i<2000 && r.c.profileLoad().state!=ProfileLoadState::Complete;++i) { r.step(20); r.c.profileTick(memory); memory.stepSave(); }
  CHECK(r.c.profileLoad().state==ProfileLoadState::Complete && !r.c.profileLoad().prepared);
  CHECK(r.c.configuration().operating.voltage[1]==5700 && r.c.currentSynchronized(1));
  Configuration loaded; CHECK(memory.load(loaded)==StorageResult::Ok && loaded.operating.current[1]==2000);
  // Changed bindings and expired reviews fail before configured targets mutate.
  r.finish(); review=r.c.previewProfile(memory,0,false,r.now);
  const auto before=r.c.configuration();
  CHECK(r.c.loadProfile(memory,review,r.now+limits::confirmationMs+1)==Result::Changed); checkSameProfile(r.c.configuration(),before);
  CHECK(r.c.bind(1,identity(2),5000,r.now)==Result::Ok);
  CHECK(r.c.loadProfile(memory,review,r.now)==Result::Changed);
  // A profile never clears a thermal stop, even when it prepares new targets.
  r.temperature=76; r.step();
  CHECK(!r.c.enableControl().enabled() && r.c.thermalStatus().stop!=ThermalStop::None);
  r.finish(); review=r.c.previewProfile(memory,0,false,r.now);
  CHECK(r.c.loadProfile(memory,review,r.now)==Result::Ok);
  CHECK(!r.c.enableControl().enabled() && r.c.thermalStatus().stop!=ThermalStop::None);
}
