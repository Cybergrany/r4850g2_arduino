#include "config_helpers.h"
#include "ui/LocalUi.h"
#include "ui/UiLayout.h"
#include "debug/Simulator.h"
#include "app/Application.h"
#include <iostream>
#include <stdexcept>
#include <cstring>
#include <cstdlib>
#include <fstream>
using namespace psu;
namespace {
unsigned checks=0;
#define CHECK(v) do { ++checks; if (!(v)) throw std::runtime_error(std::string(__FILE__)+":"+std::to_string(__LINE__)+" " #v); } while(0)
uint32_t clockNow=100;
TouchSample input={};
UiInput wheel={};
UiFrame appFrame;
uint8_t appDrawCells=255, appPaintDelay=0;
uint16_t appPaintCalls=0;
}
uint32_t millis() { return clockNow; }
uint32_t micros() { return clockNow*1000; }
TestHardwareSerial Serial;
namespace psu {
Ili9341Display::Ili9341Display() {}
bool Ili9341Display::begin() { available_=true; return true; }
bool Ili9341Display::canDim() const { return false; }
void Ili9341Display::dim(bool) {}
void Ili9341Display::invalidate(bool) { changes_.invalidate(); }
void Ili9341Display::service(const UiFrame& frame) {
  ++appPaintCalls; clockNow+=appPaintDelay;
  appFrame=frame; uint8_t budget=appDrawCells; uint16_t index; UiCell cell;
  while(changes_.next(frame,previous_,budget,index,cell)) previous_.cell(index,cell);
}
void Xpt2046Input::begin() {}
bool Xpt2046Input::read(TouchSample& s,uint32_t) { s=input; return true; }
void WheelInput::begin() {}
UiInput WheelInput::read() { auto e=wheel; wheel={}; return e; }
}
namespace {
TouchCalibration knownCalibration() { TouchCalibration c; c.valid=true; c.coefficient[0]=c.coefficient[4]=8192; return c; }
void wizardTests() {
  CalibrationWizard w; w.begin(0); CHECK(w.state()==CalibrationState::Collecting);
  for (uint8_t i=0;i<5;++i) {
    auto p=CalibrationWizard::target(i);
    // Initial contact cannot skip a target; each requires release and stable samples.
    for(uint8_t k=0;k<3;++k) w.sample({0,0,0,false},i*1000+k*20);
    for (uint8_t j=0;j<8;++j) w.sample({int16_t(p.x*8),int16_t(p.y*8),1000,true},i*1000+60+j*20);
    CHECK(w.targetIndex()==i+1);
    for (uint8_t j=0;j<8;++j) w.sample({int16_t(p.x*8),int16_t(p.y*8),1000,true},i*1000+200+j*20);
    CHECK(w.targetIndex()==i+1);
  }
  for(uint8_t k=0;k<3;++k) w.sample({0,0,0,false},5100+k*20);
  CHECK(w.state()==CalibrationState::Ready);
  TouchPoint p; CHECK(w.result().map(800,1200,p)); CHECK(p.x==100 && p.y==150);
  CHECK(!w.result().map(4095,4095,p)); CHECK(!w.result().map(-1,0,p));
  w.begin(0xfffffff0U); w.sample({0,0,0,false},uint32_t(0xfffffff0U+60001U)); CHECK(w.state()==CalibrationState::Failed);
  w.begin(0); w.sample({0,0,0,false},0);
  for (uint8_t i=0;i<40;++i) w.sample({int16_t(i%2 ? 2000 : 1000),1000,1000,true},i*20);
  CHECK(w.targetIndex()==0);
}
struct CutStorage : RamStorage {
  int remaining=-1; unsigned writes=0;
  void update(uint16_t a,uint8_t v) override { if (remaining==0) throw 1; if (remaining>0) --remaining; ++writes; RamStorage::update(a,v); }
};
void calibrationStorage() {
  CutStorage bytes; MemoryManager mem(bytes); CalibrationStore store(bytes,mem);
  auto cfg=debugConfiguration(); CHECK(mem.save(cfg)==StorageResult::Ok);
  const auto original=bytes;
  auto c=knownCalibration(); CHECK(store.start(c)==StorageResult::Ok);
  CHECK(mem.startSave(cfg)==StorageResult::Busy);
  while(store.saving()) { const auto before=bytes.writes; store.step(); CHECK(bytes.writes-before<=1); }
  CHECK(store.result()==StorageResult::Ok); TouchCalibration loaded; CHECK(store.load(loaded));
  Configuration after; CHECK(mem.load(after)==StorageResult::Ok); CHECK(after.voltage[0]==cfg.voltage[0]);
  for(uint16_t i=0;i<512;++i) if(i<216 || (i>=256 && i<472)) CHECK(bytes.read(i)==original.read(i));
  const auto baseline=bytes;
  c.coefficient[2]=65536*3;
  // Interrupt every byte of an update; the previous independently checksummed
  // calibration and charger record must remain readable.
  for(int cut=0;cut<=41;++cut) {
    bytes=baseline; bytes.remaining=cut;
    MemoryManager trialMem(bytes); CalibrationStore trial(bytes,trialMem);
    CHECK(trial.start(c)==StorageResult::Ok);
    try { while(trial.saving()) trial.step(); } catch(int) {}
    bytes.remaining=-1; MemoryManager reboot(bytes); CalibrationStore reader(bytes,reboot);
    TouchCalibration value; CHECK(reader.load(value)); CHECK(value.coefficient[2]==0 || value.coefficient[2]==3*65536);
    CHECK(reboot.load(after)==StorageResult::Ok && after.voltage[0]==cfg.voltage[0]);
  }
  bytes=baseline; MemoryManager brokenMem(bytes); CalibrationStore broken(bytes,brokenMem); bytes.failWrites=true;
  CHECK(broken.start(c)==StorageResult::Ok); while(broken.saving()) broken.step(); CHECK(broken.result()==StorageResult::WriteFailed);
}
void gestures() {
  TouchGesture g;
  auto release=[&](uint32_t now) { CHECK(!g.update(false,0,false,true,now)); CHECK(!g.update(false,0,false,true,now+20)); return g.update(false,0,false,true,now+40); };
  CHECK(!g.update(true,1,false,true,0)); CHECK(!release(20));
  CHECK(!g.update(true,1,false,true,100)); CHECK(release(200)==1);
  CHECK(!g.update(true,1,false,true,300)); CHECK(!g.update(true,2,false,true,320)); CHECK(!release(400));
  CHECK(!g.update(true,1,true,true,500)); CHECK(g.update(true,1,true,true,1000)==1); CHECK(g.update(true,1,true,true,1120)==1);
  CHECK(!release(1200));
  CHECK(!g.update(true,1,false,false,1300)); CHECK(!g.update(true,1,false,true,1400)); CHECK(!release(1500));
  CHECK(!g.update(true,1,false,true,1600));
  CHECK(!g.update(false,0,false,true,1700)); // One noisy up sample must not apply.
  CHECK(!g.update(true,1,false,true,1720)); CHECK(release(1800)==1);
  CHECK(!g.update(true,1,false,true,1900)); CHECK(!release(1920)); // Nor a single down sample.
}

struct Rig {
  SimulatedCan can; PsuController controller; RamStorage bytes; MemoryManager memory;
  Rig():controller(can),memory(bytes) { can.reset(); CHECK(controller.configure(debugConfiguration(),true)==Result::Ok); CHECK(controller.begin()); }
  void step(uint32_t ms=20) { clockNow+=ms; controller.tick(clockNow); controller.serviceProtection(memory,clockNow); memory.stepSave(); controller.profileTick(memory); controller.serviceProtection(memory,clockNow); }
  void ready() {
    for(unsigned i=0;i<4000 && (controller.report().succeeded!=membersMask(controller.count()) || controller.busy() ||
        controller.controlStatus(clockNow).activity!=Activity::Running);++i) step();
    CHECK(controller.controlStatus(clockNow).activity==Activity::Running);
    controller.alerts().acknowledgeAll();
    for(uint8_t i=0;i<PSU_MAX_UNITS;++i) CHECK(controller.issue(i,clockNow)==Issue::None && controller.voltageSynchronized(i));
  }
};
void simulationTests() {
  Rig r; r.ready();
  auto p=r.controller.preview(Operation::GroupCurrent,0,clockNow); CHECK(p.blocker==Issue::None);
  CHECK(r.controller.setCurrent(0,5500)==Result::Ok); p=r.controller.preview(Operation::GroupCurrent,0,clockNow);
  CHECK(r.controller.queue(p,false,clockNow)==Result::Ok);
  for(unsigned i=0;i<1000 && r.controller.busy();++i) r.step();
  CHECK(r.controller.report().succeeded==7 && !r.controller.report().failed);
  for(auto scenario:{DebugScenario::Reject,DebugScenario::Timeout,DebugScenario::IdentityTimeout,DebugScenario::Transport,DebugScenario::Partial}) {
    r.can.scenario(scenario); p=r.controller.preview(Operation::GroupCurrent,0,clockNow); CHECK(p.blocker==Issue::None);
    CHECK(r.controller.queue(p,false,clockNow)==Result::Ok);
    for(unsigned i=0;i<1000 && r.controller.busy();++i) r.step();
    CHECK(!r.controller.busy() && r.controller.report().failed);
  }
  r.can.scenario(DebugScenario::Missing); for(unsigned i=0;i<850;++i) r.step();
  p=r.controller.preview(Operation::GroupCurrent,0,clockNow,true); CHECK(p.missing==2 && p.blocker==Issue::None);
  r.can.scenario(DebugScenario::Healthy); for(unsigned i=0;i<200;++i) r.step(); CHECK(r.controller.issue(1,clockNow)==Issue::None);
  r.can.scenario(DebugScenario::Stale); for(unsigned i=0;i<350;++i) r.step(); CHECK(r.controller.issue(1,clockNow)==Issue::TemperatureSensor);
  r.can.scenario(DebugScenario::Conflict); for(unsigned i=0;i<200;++i) r.step(); CHECK(r.controller.issue(0,clockNow)==Issue::Conflict);
}
void snapshot(const char* name,const UiFrame& frame) {
  unsigned focused=0;
  for (uint8_t i=0;i<frame.outlineCount();++i) { const auto o=frame.outline(i);
    CHECK(o.valid() && o.row()+o.height()<=ui::rows && o.column()+o.width()<=ui::columns);
    focused += o.focused();
  }
  CHECK(focused<=1);

  const char* folder=std::getenv("PSU_UI_SNAPSHOTS"); if(!folder) return;
  std::ofstream f(std::string(folder)+"/"+name+".txt",std::ios::binary); CHECK(bool(f));
  for(uint8_t row=0;row<ui::rows;++row) { for(uint8_t col=0;col<ui::columns;++col) f<<frame.cell(uint16_t(row)*ui::columns+col).character; f<<'\n'; }
  for(uint16_t i=0;i<UiFrame::cells;++i) f<<"0123456789abcdef"[frame.cell(i).style];
  f<<"\n[";
  for(uint8_t i=0;i<frame.outlineCount();++i) { const auto o=frame.outline(i);
    if(i) f<<',';
    f<<'['<<unsigned(o.row())<<','<<unsigned(o.column())<<','<<unsigned(o.width())<<','<<unsigned(o.height())<<','<<o.focused()<<']';
  }
  f<<"]\n";
}
struct FakeDisplay : Display {
  UiFrame shown; uint8_t remaining=0; bool dimmed=false;
  DisplayStats timers;
  bool begin() override { return true; }
  void invalidate(bool = false) override { remaining=3; }
  bool pending() const override { return remaining!=0; }
  void service(const UiFrame& f) override { shown=f; if(remaining) --remaining; }
  bool canDim() const override { return true; }
  void dim(bool value) override { dimmed=value; }
  DisplayStats statistics() const override { return timers; }
  void resetStatistics() override { timers={}; }
};
void drawingPriority() {
  Rig r; r.ready(); r.controller.alerts().acknowledgeAll();
  CalibrationStore cal(r.bytes,r.memory); CHECK(cal.start(knownCalibration())==StorageResult::Ok); while(cal.saving()) cal.step();
  FakeDisplay display; LocalUi ui(r.controller,r.memory,display,r.bytes); input={}; wheel={}; ui.begin();
  ui.tick(clockNow); CHECK(ui.priorityDrawing());
  ui.paint(); ui.paint(); CHECK(!display.pending() && !ui.priorityDrawing());
  clockNow+=ui::refreshMs; ui.tick(clockNow);
  CHECK(display.remaining==2 && !ui.priorityDrawing()); // Metrics retain one slice.
  clockNow+=ui::refreshMs; ui.tick(clockNow);
  CHECK(display.remaining==1); // A slow telemetry pass cannot keep restarting.
  ui.paint(); wheel={1,false,false}; ui.tick(++clockNow);
  CHECK(ui.priorityDrawing()); ui.paint(); ui.paint(); CHECK(!ui.priorityDrawing());

  // Exercise the actual Application loop with deliberately incomplete drawing.
  // One regular slice plus the fixed ceiling, or the elapsed-time limit.
  clockNow=0xfffffff0U; Serial.input.clear(); Serial.output.clear();
  appDrawCells=1; appPaintCalls=0;
  Application app; app.begin(); app.tick(); CHECK(appPaintCalls==1+ui::priorityRenderSlices);
  appPaintCalls=0; appPaintDelay=1; app.tick();
  CHECK(appPaintCalls==1+ui::priorityRenderUs/1000);
  appDrawCells=255; appPaintDelay=0;
}
void layoutAndLocalUi() {
  Rig r; r.ready(); UiModel model(r.controller,r.memory); model.tick(clockNow); UiFrame frame;
  UiView::compose(frame,model,r.controller,clockNow); snapshot("touch-groups",frame);
  UiAction a; CHECK(UiLayout::hit(model,50,80,a) && a.kind==ActionKind::OpenGroup);
  model.action(a,clockNow); UiView::compose(frame,model,r.controller,clockNow); snapshot("touch-chargers",frame);
  model.action({ActionKind::Next},clockNow); CHECK(model.unitRank()==2);
  model.action({ActionKind::Configure},clockNow); model.action({ActionKind::EditCurrent},clockNow);
  CHECK(model.page()==UiPage::Editor && model.editing()); const auto old=groupCurrent(r.controller.configuration(), 0);
  model.action({ActionKind::Adjust,32000},clockNow); CHECK(model.editValue()==model.maximum());
  UiView::compose(frame,model,r.controller,clockNow); snapshot("touch-editor",frame);
  CHECK(groupCurrent(r.controller.configuration(), 0)==old);
  model.action({ActionKind::Cancel},clockNow); CHECK(!model.editing() && groupCurrent(r.controller.configuration(), 0)==old);
  model.action({ActionKind::Tools},clockNow); UiView::compose(frame,model,r.controller,clockNow); snapshot("touch-tools",frame);
  model.action({ActionKind::Scenarios},clockNow); UiView::compose(frame,model,r.controller,clockNow); snapshot("touch-scenarios",frame);
  CalibrationStore cal(r.bytes,r.memory); CHECK(cal.start(knownCalibration())==StorageResult::Ok); while(cal.saving()) cal.step();
  FakeDisplay display; LocalUi ui(r.controller,r.memory,display,r.bytes); input={}; ui.begin();
  auto tick=[&]() { r.step(); ui.storageTick(); ui.tick(clockNow); };
  auto tap=[&](int16_t x,int16_t y) { input={int16_t(x*8),int16_t(y*8),1000,true}; for(int i=0;i<5;++i) tick(); input={}; for(int i=0;i<12;++i) tick(); };
  for(int i=0;i<10;++i) tick();
  tap(40,80); // group -> chargers
  tap(112,216); // config menu
  tap(80,80); // configure group
  tap(40,48); // selected group
  tap(80,128); // current -> editor
  CHECK(ui.editing());
  // A held opening touch must not flow into the +/- control of the editor.
  tap(280,100); // plus
  input={280*8,100*8,1000,true}; for(int i=0;i<35;++i) tick(); input={}; for(int i=0;i<5;++i) tick();
  CHECK(groupCurrent(r.controller.configuration(), 0)==old);
  tap(50,216); // cancel
  CHECK(groupCurrent(r.controller.configuration(), 0)==old);
  CHECK(!ui.editing());
  snapshot("touch-local",display.shown);
  tap(80,128); CHECK(ui.editing()); tap(280,100);
  // Wheel input during a captured Apply press consumes the touch gesture.
  input={230*8,216*8,1000,true}; tick(); wheel={1,false,false}; tick();
  input={}; for(int i=0;i<5;++i) tick(); CHECK(ui.editing() && !r.controller.busy());
  tap(230,216); CHECK(!ui.editing());
  for(int i=0;i<400;++i) tick();
  Configuration saved; CHECK(r.memory.load(saved)==StorageResult::Ok);
  CHECK(groupCurrent(saved, 0)==old+100);
  CHECK(r.controller.report().failed==0 && r.controller.report().succeeded==7);
  snapshot("touch-saved",display.shown);
  // Actual touch hit boxes: whole-source acceptance, verified save and rejoin.
  r.can.sourcePower(0x18,false); for(int i=0;i<1200;++i) tick();
  r.controller.alerts().acknowledgeAll(); for(int i=0;i<12;++i) tick();
  wheel={0,false,true}; tick(); for(int i=0;i<5;++i) tick();
  tap(160,216); tap(50,48); tap(150,64); // Groups -> Tools -> overview -> GROUP2.
  snapshot("touch-availability-group",display.shown);
  tap(150,144); // Exclude group review.
  snapshot("touch-availability-confirm",display.shown);
  tap(240,216); // Remember defaults checked; confirm verified save.
  for(int i=0;i<350;++i) tick();
  CHECK(r.controller.configuration().disabled==0x18 && r.controller.excluded()==0x18);
  snapshot("touch-availability-saved",display.shown);
  r.can.sourcePower(0x18,true); for(int i=0;i<1200;++i) tick();
  CHECK(r.controller.unitControl(3).phase==ControlPhase::Held && r.controller.excluded()==0x18);
  r.controller.alerts().acknowledgeAll(); for(int i=0;i<12;++i) tick();
  tap(100,216); tap(100,216); // Result -> group -> overview.
  snapshot("touch-availability-return",display.shown);
  tap(150,176); tap(150,48); // Controller -> Stop.
  for(int i=0;i<1200;++i) tick();
  CHECK(!r.controller.enableControl().enabled());
  tap(100,216); tap(150,64); // Overview -> GROUP2.
  tap(150,112); tap(240,216); // Include, reviewed + remembered.
  for(int i=0;i<400;++i) tick();
  CHECK(r.controller.excluded()==0);
  tap(100,216); tap(100,216); tap(150,176); tap(60,80); // Controller -> Enable review.
  CHECK(!r.controller.enableControl().enabled()); tap(240,216);
  for(int i=0;i<400;++i) tick();
  CHECK(r.controller.currentSynchronized(3));
}
struct Capture : Print {
  std::string value;
  size_t write(uint8_t b) override { value.push_back(char(b)); return 1; }
};
void localCalibrationAndDiagnostics() {
  Rig r; r.ready(); FakeDisplay display; LocalUi ui(r.controller,r.memory,display,r.bytes);
  input={}; ui.begin();
  auto tick=[&]() { r.step(); ui.storageTick(); ui.tick(clockNow); };
  for(int i=0;i<5;++i) tick();
  snapshot("touch-calibration",display.shown);
  CHECK(display.shown.outlineCount()==1 && display.shown.outline(0).focused());
  for(uint8_t i=0;i<5;++i) {
    const auto p=CalibrationWizard::target(i);
    input={int16_t(p.x*8),int16_t(p.y*8),1000,true};
    for(int j=0;j<12;++j) tick();
    input={}; for(int j=0;j<5;++j) tick();
  }
  for(int i=0;i<80;++i) tick();
  CalibrationStore cal(r.bytes,r.memory); TouchCalibration mapped; CHECK(cal.load(mapped));
  TouchPoint point; CHECK(mapped.map(800,800,point) && point.x==100 && point.y==100);
  snapshot("touch-calibration-saved",display.shown);
  Capture out; char cmd[]="touch", debug[]="debug", on[]="on"; char* argv[]={cmd,debug,on};
  CHECK(ui.command(3,argv,out,clockNow)); for(int i=0;i<5;++i) tick();
  input={40*8,145*8,1000,true}; for(int i=0;i<30;++i) tick();
  const auto before=groupCurrent(r.controller.configuration(), 0);
  snapshot("touch-diagnostics",display.shown);
  input={}; for(int i=0;i<5;++i) tick(); CHECK(groupCurrent(r.controller.configuration(), 0)==before);
  char status[]="status"; char* stat[]={cmd,status}; out.value.clear(); CHECK(ui.command(2,stat,out,clockNow));
  CHECK(out.value.find("calibrated=1")!=std::string::npos && out.value.size()<256);
  // A local apply continues to its save/result even if calibration is requested.
  UiModel model(r.controller,r.memory); model.tick(clockNow); model.action({ActionKind::Configure},clockNow);
  model.action({ActionKind::EditCurrent},clockNow); model.action({ActionKind::Adjust,1},clockNow); model.action({ActionKind::Apply},clockNow);
  CHECK(model.working()); model.action({ActionKind::Tools},clockNow); model.action({ActionKind::Calibrate},clockNow); CHECK(model.working());
  for(int i=0;i<1000 && model.working();++i) { r.step(); model.tick(clockNow); }
  CHECK(model.notice()==UiNotice::Saved);
}

bool hasText(const UiFrame& f,const char* text) {
  std::string all;
  for(uint16_t i=0;i<UiFrame::cells;++i) all+=f.cell(i).character;
  return all.find(text)!=std::string::npos;
}
void scopedProfileTouchWorkflow() {
  Rig r; r.ready(); CHECK(r.controller.setAutoResume(false)==Result::Ok);
  for (unsigned i=0;i<1200;++i) r.step();
  r.controller.alerts().acknowledgeAll();
  CalibrationStore cal(r.bytes,r.memory); CHECK(cal.start(knownCalibration())==StorageResult::Ok); while(cal.saving()) cal.step();
  FakeDisplay display; LocalUi ui(r.controller,r.memory,display,r.bytes); input={}; wheel={}; ui.begin();
  auto tick=[&](unsigned n) { while(n--) { r.step(); ui.storageTick(); ui.tick(clockNow); } };
  auto tap=[&](int16_t x,int16_t y) { input={int16_t(x*8),int16_t(y*8),1000,true}; tick(5); input={}; tick(14); };
  tick(20); tap(40,80); tap(112,216); // Chargers -> config menu.
  CHECK(hasText(display.shown,"Configure individual"));
  tap(100,176); tap(40,48); // Save Config -> slot 1.
  CHECK(hasText(display.shown,"No (targets only)"));
  tap(150,96); CHECK(hasText(display.shown,"[X] Yes"));
  tap(150,144); tap(240,160); // Edit name, increment first character.
  CHECK(hasText(display.shown,"QROFILE 1"));
  tap(50,216); CHECK(hasText(display.shown,"PROFILE 1")); // Cancel restores it.
  tap(150,144); tap(240,160); tap(230,216); tap(230,216); // Name Done, review.
  CHECK(hasText(display.shown,"Store participation: YES"));
  snapshot("touch-profile-save-review",display.shown);
  tap(230,216); CHECK(r.memory.saving());
  // An alert arriving during the named save cannot replace its completion owner.
  r.controller.alerts().set(AlertManager::Receive,AlertCode::ReceiveLoss,clockNow);
  ui.tick(clockNow); tick(8); CHECK(hasText(display.shown,"Acknowledge"));
  tap(75,216); tick(200);
  CHECK(hasText(display.shown,"PROFILE SAVED"));
  ProfileInfo info; CHECK(r.memory.profileInfo(0,info)==StorageResult::Ok && info.hasInclusion && !std::strcmp(info.name,"QROFILE 1"));
  tap(75,216); tap(150,216); // Result -> slots -> menu.
  tap(100,144); tap(40,48); // Load Config -> slot 1.
  CHECK(hasText(display.shown,"No (targets only)")); // Restore remains opt-in.
  tap(230,216); snapshot("touch-profile-load-review",display.shown);
  CHECK(r.controller.set({ScopeKind::Unit,0},SettingFields::Voltage,5500)==Result::Ok);
  tap(230,216); tick(10); CHECK(hasText(display.shown,"CHANGED")); // Serial edit invalidated the review.
  CHECK(!r.memory.saving() && r.controller.configuration().voltage[0]==5500);
  tap(75,216); tap(40,48); tap(230,216); tap(230,216); tick(350);
  CHECK(!r.controller.enableControl().enabled());
  CHECK(r.controller.configuration().voltage[0]==5400);
  Configuration saved; CHECK(r.memory.load(saved)==StorageResult::Ok && saved.voltage[0]==5400);
}
void thermalTouchWorkflow() {
  Rig r; r.ready(); CHECK(r.memory.save(r.controller.configuration())==StorageResult::Ok);
  CalibrationStore cal(r.bytes,r.memory); CHECK(cal.start(knownCalibration())==StorageResult::Ok); while(cal.saving()) cal.step();
  FakeDisplay display; LocalUi ui(r.controller,r.memory,display,r.bytes); input={}; ui.begin();
  auto tick=[&](unsigned n) { while(n--) { r.step(); ui.storageTick(); ui.tick(clockNow); } };
  auto tap=[&](int16_t x,int16_t y) { input={int16_t(x*8),int16_t(y*8),1000,true}; tick(5); input={}; tick(14); };
  tick(20); tap(40,80); tap(112,216); tap(80,80); tap(40,48); tap(80,128); CHECK(ui.editing());
  input={230*8,216*8,1000,true}; tick(1); // Captured Apply must not become Acknowledge.
  r.can.temperatures(30,70); tick(130); input={}; tick(15);
  CHECK(ui.editing() && r.controller.thermalStatus().percent==30);
  CHECK(!r.controller.alerts().at(AlertManager::Thermal).acknowledged && hasText(display.shown,"Acknowledge"));
  snapshot("touch-alert-temperature",display.shown);
  tap(75,216); CHECK(ui.editing() && r.controller.alerts().at(AlertManager::Thermal).acknowledged);
  CHECK(r.controller.alerts().error());
  r.can.temperatures(76,30); tick(350);
  CHECK(ui.editing() && !r.controller.enableControl().enabled());
  Configuration saved; CHECK(r.memory.load(saved)==StorageResult::Ok && saved.thermalStop==ThermalStop::Temperature);
  snapshot("touch-alert-shutdown",display.shown);
  tap(150,180); CHECK(!r.controller.enableControl().enabled()); // Resume is separate and still hot.
  tap(75,216); CHECK(ui.editing());
  r.can.temperatures(30,30); tick(200);
  CHECK(r.controller.setAutoResume(true)==Result::Ok && !r.controller.enableControl().enabled());
  tick(500); CHECK(r.controller.enableControl().enabled() && r.controller.configuration().thermalStop==ThermalStop::None);
  // Explicit list remains open for acknowledged active incidents.
  UiModel model(r.controller,r.memory); model.tick(clockNow);
  r.controller.alerts().set(AlertManager::Receive,AlertCode::ReceiveLoss,clockNow);
  r.controller.alerts().acknowledgeAll(); model.action({ActionKind::Alerts},clockNow++);
  model.action({ActionKind::OpenAlert,0},clockNow++); model.notifications(clockNow++);
  CHECK(model.page()==UiPage::AlertDetail);
  model.dismissAlert(); model.action({ActionKind::Alerts},clockNow++);
  UiFrame frame; UiView::compose(frame,model,r.controller,clockNow); snapshot("touch-alert-list",frame);
  // Real calibration writer excludes protection until its own commit completes.
  CHECK(cal.start(knownCalibration())==StorageResult::Ok);
  r.can.temperatures(76,30); for(unsigned i=0;i<100;++i) r.step();
  CHECK(r.controller.configuration().thermalStop!=ThermalStop::None && !r.controller.thermalStatus().persisted);
  while(cal.saving()) cal.step();
  tick(400);
  CHECK(r.memory.load(saved)==StorageResult::Ok && saved.thermalStop!=ThermalStop::None);
}

void touchRecoveryWorkflow() {
  Rig r; r.ready(); CHECK(r.controller.setAutoResume(false)==Result::Ok);
  for(unsigned i=0;i<600;++i) r.step();
  CHECK(r.memory.save(r.controller.configuration())==StorageResult::Ok);
  CalibrationStore calibration(r.bytes,r.memory); CHECK(calibration.start(knownCalibration())==StorageResult::Ok);
  while(calibration.saving()) calibration.step();
  r.can.sourcePower(7,false); for(unsigned i=0;i<1000;++i) r.step();
  r.controller.alerts().acknowledgeAll();
  FakeDisplay display; LocalUi ui(r.controller,r.memory,display,r.bytes); input={}; ui.begin();
  auto tick=[&](unsigned n) { while(n--) { r.step(); ui.storageTick(); ui.tick(clockNow); } };
  auto tap=[&](int16_t x,int16_t y) { input={int16_t(x*8),int16_t(y*8),1000,true}; tick(5); input={}; tick(15); };
  tick(20); r.controller.alerts().reset(AlertManager::Startup);
  r.controller.alerts().set(AlertManager::Startup,AlertCode::Missing,clockNow); tick(30);
  CHECK(hasText(display.shown,"Acknowledge") && hasText(display.shown,"Config"));
  snapshot("touch-recovery-missing",display.shown);
  const auto id=r.controller.alerts().at(AlertManager::Startup).id;
  tap(240,216); CHECK(hasText(display.shown,"PSU PARTICIPATION"));
  CHECK(!r.controller.alerts().at(AlertManager::Startup).acknowledged && !r.controller.acceptedAbsences());
  snapshot("touch-recovery-availability",display.shown);
  tap(150,216); CHECK(hasText(display.shown,"Acknowledge")); // Overview Back returns to the incident.
  CHECK(r.controller.alerts().at(AlertManager::Startup).id==id);
  tap(60,216); CHECK(r.controller.alerts().at(AlertManager::Startup).acknowledged);
  // A failed journal write has an independent reviewed-save recovery path.
  r.controller.storageStatus(StorageResult::WriteFailed); tick(1600);
  CHECK(hasText(display.shown,"Review save")); tap(240,216);
  CHECK(hasText(display.shown,"REVIEW RAM BEFORE SAVE")); tap(200,216);
  CHECK(hasText(display.shown,"REVIEW RECOVERY"));
  snapshot("touch-recovery-save-confirm",display.shown);
  tap(60,216); CHECK(!r.memory.saving() && hasText(display.shown,"REVIEW RAM BEFORE SAVE"));
  tap(200,216); tap(240,216); tick(400);
  CHECK(hasText(display.shown,"Saved and verified") && !r.controller.enableControl().enabled());
  snapshot("touch-recovery-saved",display.shown);
  // The UI's diag extension is bounded and separates event/telemetry timing.
  // Serial status and resetting counters do not change navigation.
  struct Capture: Print { std::string value; size_t write(uint8_t b) override { value+=char(b); return 1; } } out;
  char diag[]="diag", page[]="ui"; char* args[]={diag,page};
  display.timers.renderMaxUs=display.timers.transactionMaxUs=UINT32_MAX;
  display.timers.sceneMs=display.timers.sceneMaxMs=display.timers.changeMs=display.timers.changeMaxMs=UINT32_MAX;
  CHECK(ui.command(2,args,out,clockNow));
  CHECK(out.value.find("draw-Hz=8000000")!=std::string::npos && out.value.find("scene-ms(last/max)")!=std::string::npos);
  CHECK(out.value.find("change-ms=")!=std::string::npos);
  CHECK(out.value.size()+std::strlen("phase-us control/storage/serial/ui=4294967295/4294967295/4294967295/4294967295\r\n> ")<=console::outputReserveBytes);
  char reset[]="reset"; char* resetArgs[]={diag,page,reset};
  out.value.clear(); CHECK(ui.command(3,resetArgs,out,clockNow));
  CHECK(out.value.find("pending drawing continues")!=std::string::npos && hasText(display.shown,"Saved and verified"));
  CHECK(!display.timers.renderMaxUs && !display.timers.changeMs);
}

void ninthTouchSelection() {
  Rig r; r.ready(); UiModel model(r.controller,r.memory); model.tick(clockNow);
  model.action({ActionKind::OpenGroup,3},clockNow);
  CHECK(model.group()==3 && model.unitCount()==2);
  UiAction action;
  CHECK(UiLayout::hit(model,40,128,action) && action.kind==ActionKind::OpenUnit && action.value==1);
  model.action(action,clockNow); CHECK(model.unit()==8 && model.page()==UiPage::Detail);
  UiFrame frame; UiView::compose(frame,model,r.controller,clockNow); snapshot("touch-ninth-details",frame);
  r.can.sourcePower(0x100,false); r.can.batteryPower(0x100,false);
  for(unsigned i=0;i<900;++i) r.step();
  model.action({ActionKind::Startup},clockNow); model.action({ActionKind::AvailabilityGroup,3},clockNow); model.action({ActionKind::AvailabilityIndividuals},clockNow);
  CHECK(model.availabilitySelected()==0);
  CHECK(UiLayout::hit(model,40,72,action) && action.kind==ActionKind::AvailabilityToggle && action.value==1);
  model.action(action,clockNow); CHECK(model.availabilitySelected()==0x100);
  model.action(action,clockNow); CHECK(model.availabilitySelected()==0);
  model.action(action,clockNow); CHECK(model.availabilitySelected()==0x100);
  CHECK(UiLayout::hit(model,240,216,action) && action.kind==ActionKind::AvailabilityReview);
  model.action(action,clockNow);
  CHECK(model.availabilityPlan().selected==0x180 && model.availabilityPlan().blocker==Issue::None);
  CHECK(!r.controller.acceptedAbsences()); // Opening a review is not consent.
  UiView::compose(frame,model,r.controller,clockNow); snapshot("touch-ninth-absence-review",frame);
  CHECK(UiLayout::hit(model,240,216,action) && action.kind==ActionKind::AvailabilityConfirm);
  model.action(action,clockNow);
  CHECK(!r.controller.acceptedAbsences());
  for(unsigned i=0;i<500;++i) { r.step(); model.tick(clockNow); }
  CHECK(r.controller.acceptedAbsences()==0x100 && r.controller.participants()==0xff);
  // Four-row touch viewport: Down/Up scroll one row, not a page; checkbox
  // hit targets follow the visible logical slot and Cancel preserves live intent.
  auto all=r.controller.configuration();
  for(auto& g:all.groups) g={};
  std::strcpy(all.groups[0].name,"NINE"); all.groups[0].members=0x1ff;
  CHECK(r.controller.configure(all,false)==Result::Ok);
  r.can.sourcePower(0x1ff,true); r.can.batteryPower(0x1ff,true);
  for(unsigned i=0;i<2000;++i) r.step();
  model.resetNavigation(); model.tick(clockNow); model.action({ActionKind::Startup},clockNow);
  model.action({ActionKind::AvailabilityGroup,0},clockNow); model.action({ActionKind::AvailabilityIndividuals},clockNow);
  CHECK(UiLayout::hit(model,270,176,action) && action.kind==ActionKind::AvailabilityScroll);
  model.action(action,clockNow); CHECK(model.availabilityTop()==1);
  for(unsigned i=0;i<10;++i) model.action(action,clockNow);
  CHECK(model.availabilityTop()==5);
  CHECK(UiLayout::hit(model,40,144,action) && action.kind==ActionKind::AvailabilityToggle && action.value==8);
  model.action(action,clockNow); CHECK(model.availabilitySelected()==0 && r.controller.excluded()==0x100);
  UiView::compose(frame,model,r.controller,clockNow); snapshot("touch-availability-scroll-nine",frame);
  CHECK(UiLayout::hit(model,40,176,action) && action.kind==ActionKind::AvailabilityScroll);
  model.action(action,clockNow); CHECK(model.availabilityTop()==4);
  for(unsigned i=0;i<10;++i) model.action(action,clockNow);
  CHECK(model.availabilityTop()==0);
  CHECK(UiLayout::hit(model,40,216,action) && action.kind==ActionKind::Back);
  model.action(action,clockNow); CHECK(model.page()==UiPage::Availability && r.controller.excluded()==0x100);
}

void applicationScenarios() {
  clockNow=100; input={}; wheel={}; Serial.input.clear(); Serial.output.clear();
  Application app; app.begin();
  auto tick=[&](unsigned n) { while(n--) { clockNow+=20; app.tick(); } };
  auto command=[&](const std::string& s) { Serial.output.clear(); Serial.input=s+"\n"; tick(20); };
  auto tap=[&](int16_t x,int16_t y) { input={int16_t(x*8),int16_t(y*8),1000,true}; tick(5); input={}; tick(12); };
  auto editCurrent=[&]() {
    // Fault scenarios may raise another unit's incident between taps. Dismiss
    // it explicitly before each fresh navigation gesture; acknowledgement must
    // preserve the page rather than consume the next tap as navigation.
    command("alerts ack all"); tap(40,80); command("alerts ack all");
    tap(112,216); command("alerts ack all");
    CHECK(hasText(appFrame,"Configure group"));
    tap(80,80); command("alerts ack all"); tap(40,48); command("alerts ack all");
    CHECK(hasText(appFrame,"GROUP VOLTAGE"));
    tap(80,128); command("alerts ack all");
  };
  tick(1600); CHECK(Serial.output.find("SIM:")!=std::string::npos);
  CHECK(Serial.output.find("CAN ready")==std::string::npos);
  command("ui-debug status"); CHECK(Serial.output.find("priming=0")!=std::string::npos);

  // Complete the real startup wizard through Application with fake raw samples.
  command("touch calibrate"); tick(5);
  for(uint8_t i=0;i<5;++i) {
    const auto p=CalibrationWizard::target(i); input={int16_t(p.x*8),int16_t(p.y*8),1000,true}; tick(12); input={}; tick(5);
  }
  tick(100); CHECK(hasText(appFrame,"Calibration saved"));
  command("touch status"); CHECK(Serial.output.find("calibrated=1")!=std::string::npos);
  command("touch calibration export"); CHECK(Serial.output.find("affine Q16.16=")!=std::string::npos);
  command("alerts ack all"); command("touch debug off");
  for(uint8_t s=0;s<uint8_t(DebugScenario::Count);++s) {
    command(std::string("ui-debug scenario ")+scenarioName(DebugScenario(s)).data); tick(1900);
    command("alerts ack all");
    command("ui-debug status"); CHECK(Serial.output.find("priming=0")!=std::string::npos);

    if (Serial.output.find("ERR ")!=std::string::npos) throw std::runtime_error(std::string("Scenario ")+scenarioName(DebugScenario(s)).data+": "+Serial.output);
    CHECK(hasText(appFrame,"SIM"));
    if (DebugScenario(s)==DebugScenario::Empty) {
      CHECK(hasText(appFrame,"No groups configured")); wheel={0,false,true}; tick(5); CHECK(hasText(appFrame,"CONFIGURATION"));
    } else if (DebugScenario(s)==DebugScenario::Missing) CHECK(hasText(appFrame,"ERR 1/3"));
    else if (DebugScenario(s)==DebugScenario::BusOffline) CHECK(hasText(appFrame,"ERR 3/3"));
    else if (DebugScenario(s)==DebugScenario::Extremes) CHECK(hasText(appFrame,"LONGGRP1"));
    CHECK(Serial.output.find("ERR console output truncated")==std::string::npos);
    switch (DebugScenario(s)) {
      case DebugScenario::ZeroHold:
        CHECK(hasText(appFrame,"ZERO A: SERIAL OFF")); snapshot("touch-zero-hold",appFrame); break;
      case DebugScenario::StopUnconfirmed:
        CHECK(hasText(appFrame,"AC CUTOFF")); snapshot("touch-stop-unconfirmed",appFrame); break;
      case DebugScenario::FanReject: case DebugScenario::WakeCurrent: case DebugScenario::ZeroReject:
        CHECK(hasText(appFrame,"START BLOCKED") || hasText(appFrame,"AC CUTOFF")); snapshot("touch-control-fault",appFrame); break;
      case DebugScenario::Changed:
        editCurrent(); CHECK(hasText(appFrame,"CONFIG / DEVICES CHANGED")); snapshot("touch-changed",appFrame); break;
      case DebugScenario::Missing:
        editCurrent(); tap(230,216); CHECK(hasText(appFrame,"MISSING MEMBER - BLOCKED"));
        snapshot("touch-confirm",appFrame); tap(230,216); tick(600);
        CHECK(hasText(appFrame,"PARTIAL APPLY SAVED")); snapshot("touch-partial",appFrame); break;
      case DebugScenario::VoltageUnsynced:
        editCurrent(); tap(230,216); tick(600); CHECK(hasText(appFrame,"PREPARED FOR NEXT ON")); break;
      case DebugScenario::SaveChanged: case DebugScenario::SaveFailed:
      case DebugScenario::Reject: case DebugScenario::Timeout: case DebugScenario::Transport: case DebugScenario::Partial:
        editCurrent(); tap(280,100); tap(230,216); tick(600); command("alerts ack all");
        if (DebugScenario(s)==DebugScenario::SaveChanged) {
          CHECK(hasText(appFrame,"EARLIER SNAPSHOT SAVED")); snapshot("touch-saved-older",appFrame);
        } else if (DebugScenario(s)==DebugScenario::SaveFailed) {
          CHECK(hasText(appFrame,"APPLIED / EEPROM FAILED")); snapshot("touch-save-failed",appFrame);
        } else {
          // A background volatile refresh can discover this rejection before
          // the editor submits its write. Both paths must leave EEPROM alone.
          if (!hasText(appFrame,"APPLY FAILED - NOT SAVED") && !hasText(appFrame,"APPLY BLOCKED")) {
            std::string frame;
            for (unsigned i=0;i<UiFrame::cells;++i) { frame += appFrame.cell(i).character; if ((i+1)%ui::columns==0) frame += '\n'; }
            throw std::runtime_error(std::string("Apply result for ")+scenarioName(DebugScenario(s)).data+":\n"+frame);
          }
          snapshot("touch-apply-failed",appFrame);
        }
        break;
      default: break;
    }
  }
  command("ui-debug scenario healthy"); tick(1600);
  command("set current GROUP1 35"); command("apply GROUP1"); tick(500);
  command("save"); tick(300);
  if (Serial.output.find("SIM RAM: EEPROM OK")==std::string::npos) throw std::runtime_error("Save console output: "+Serial.output);
  command("set current GROUP1 40"); command("ui-debug restart"); tick(1000);
  command("config"); tick(100); CHECK(Serial.output.find("total A=35.00")!=std::string::npos);
  command("ui-debug reset"); tick(1000); command("config"); tick(100);
  CHECK(Serial.output.find("total A=30.00")!=std::string::npos);
  // Saved configurations need not authorize every fixture unit. Restart must
  // respect that policy rather than waiting forever for a full-fixture restore.
  command("autoresume off"); command("save"); tick(300); command("ui-debug restart"); tick(800);
  command("ui-debug status"); CHECK(Serial.output.find("priming=0")!=std::string::npos);
  command("ui-debug enable-input on"); tick(1000); command("diag control"); tick(150);
  CHECK(Serial.output.find("enable=ON source=ENABLE_IN")!=std::string::npos);
  command("autoresume off"); tick(1000); command("diag control"); tick(150);
  CHECK(Serial.output.find("enable=OFF source=autoresume/serial")!=std::string::npos && hasText(appFrame,"ZERO A: SERIAL OFF"));
  command("fan all auto"); tick(300); command("diag psu 1"); tick(100);
  CHECK(Serial.output.find("fan requested=AUTO acknowledged=AUTO")!=std::string::npos && Serial.output.find("override=AUTO;")!=std::string::npos);
  command("fan GROUP1 max"); tick(300); command("diag psu 1"); tick(100);
  CHECK(Serial.output.find("fan requested=MAX acknowledged=MAX")!=std::string::npos && Serial.output.find("override=MAX;")!=std::string::npos);
  command("fan all policy"); tick(100);
  command("ui-debug enable-input off"); tick(10); command("ui-debug enable-input on"); tick(1000); command("diag control"); tick(150);
  CHECK(Serial.output.find("enable=ON source=ENABLE_IN")!=std::string::npos);
  // Save latest excluded-member intent, restart with that group present, and
  // require explicit rejoin before any positive-current restore to those slots.
  command("ui-debug reset"); tick(1600);
  command("ui-debug source GROUP2 off"); tick(1000);
  command("diag startup"); tick(150); CHECK(Serial.output.find("WAIT AC")!=std::string::npos);
  command("startup accept GROUP2 remember"); tick(100);
  CHECK(Serial.output.find("selected=4,5")!=std::string::npos);
  command("confirm yes"); tick(350);
  CHECK(Serial.output.find("SAVED AND VERIFIED")!=std::string::npos);
  command("set current GROUP2 26"); command("apply GROUP2"); tick(100);
  command("save"); tick(350); command("ui-debug restart"); tick(1600);
  command("diag startup"); tick(150);
  CHECK(Serial.output.find("excluded=4,5")!=std::string::npos);
  command("diag psu 4"); tick(100);
  CHECK(Serial.output.find("authorized A=13.00")!=std::string::npos && Serial.output.find("held-at-zero-awake")!=std::string::npos);
  command("autoresume off"); tick(1200);
  command("startup include GROUP2 remember"); tick(100); CHECK(Serial.output.find("ARE YOU SURE?")!=std::string::npos);
  command("confirm yes"); tick(600); command("autoresume on"); tick(600);
  command("diag psu 4"); tick(100); CHECK(Serial.output.find("current-acked=1")!=std::string::npos);
  command("startup stop"); tick(10); command("autoresume on"); tick(200);
  command("diag startup"); tick(150); CHECK(Serial.output.find("LOW fault latched")!=std::string::npos);
  CHECK(Serial.output.find("ERR console output truncated")==std::string::npos);
  // Exercise diagnostics through the real Application/console hook, including
  // the phase counters that LocalUi alone cannot report. Reset is read-only
  // with respect to the existing latched cutoff and enable decision.
  command("diag ui");
  CHECK(Serial.output.find("scene-ms(last/max)=")!=std::string::npos);
  CHECK(Serial.output.find("change-ms=")!=std::string::npos);
  CHECK(Serial.output.find("phase-us control/storage/serial/ui=")!=std::string::npos);
  CHECK(Serial.output.find("ERR console output truncated")==std::string::npos);
  command("diag ui reset"); CHECK(Serial.output.find("UI timing maxima reset")!=std::string::npos);
  command("diag startup"); tick(150); CHECK(Serial.output.find("LOW fault latched")!=std::string::npos);
}

}
int main() { try { wizardTests(); calibrationStorage(); gestures(); simulationTests(); drawingPriority(); layoutAndLocalUi(); localCalibrationAndDiagnostics(); scopedProfileTouchWorkflow(); thermalTouchWorkflow(); touchRecoveryWorkflow(); ninthTouchSelection(); applicationScenarios();
  std::cout<<"PASS touch/simulator: "<<checks<<" checks\n";
} catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; } }
