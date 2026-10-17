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
}
uint32_t millis() { return clockNow; }
uint32_t micros() { return clockNow*1000; }
TestHardwareSerial Serial;
namespace psu {
Ili9341Display::Ili9341Display() {}
bool Ili9341Display::begin() { available_=true; return true; }
bool Ili9341Display::canDim() const { return false; }
void Ili9341Display::dim(bool) {}
void Ili9341Display::Glyph::drawPixel(int16_t,int16_t,uint16_t) {}
void Ili9341Display::service(const UiFrame& frame) {
  appFrame=frame; uint8_t budget=255; uint16_t index; UiCell cell;
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
  Configuration after; CHECK(mem.load(after)==StorageResult::Ok); CHECK(after.voltage==cfg.voltage);
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
    CHECK(reboot.load(after)==StorageResult::Ok && after.voltage==cfg.voltage);
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
  void step(uint32_t ms=20) { clockNow+=ms; controller.tick(clockNow); memory.stepSave(); }
  void ready() { for(unsigned i=0;i<1600 && (!controller.voltageSynchronized(0) || controller.busy());++i) step();
    for(uint8_t i=0;i<PSU_MAX_UNITS;++i) CHECK(controller.issue(i,clockNow)==Issue::None && controller.voltageSynchronized(i)); }
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
  r.can.scenario(DebugScenario::Stale); for(unsigned i=0;i<350;++i) r.step(); CHECK(r.controller.issue(1,clockNow)==Issue::NoTelemetry);
  r.can.scenario(DebugScenario::Conflict); for(unsigned i=0;i<200;++i) r.step(); CHECK(r.controller.issue(0,clockNow)==Issue::Conflict);
}
void snapshot(const char* name,const UiFrame& frame) {
  const char* folder=std::getenv("PSU_UI_SNAPSHOTS"); if(!folder) return;
  std::ofstream f(std::string(folder)+"/"+name+".txt",std::ios::binary); CHECK(bool(f));
  for(uint8_t row=0;row<ui::rows;++row) { for(uint8_t col=0;col<ui::columns;++col) f<<frame.cell(uint16_t(row)*ui::columns+col).character; f<<'\n'; }
  for(uint16_t i=0;i<UiFrame::cells;++i) f<<"0123456789abcdef"[frame.cell(i).style];
  f<<'\n';
}
struct FakeDisplay : Display {
  UiFrame shown; uint8_t remaining=0; bool dimmed=false;
  bool begin() override { return true; }
  void invalidate() override { remaining=3; }
  bool pending() const override { return remaining!=0; }
  void service(const UiFrame& f) override { shown=f; if(remaining) --remaining; }
  bool canDim() const override { return true; }
  void dim(bool value) override { dimmed=value; }
};
void layoutAndLocalUi() {
  Rig r; r.ready(); UiModel model(r.controller,r.memory); model.tick(clockNow); UiFrame frame;
  UiView::compose(frame,model,r.controller,clockNow); snapshot("touch-groups",frame);
  UiAction a; CHECK(UiLayout::hit(model,50,80,a) && a.kind==ActionKind::OpenGroup);
  model.action(a,clockNow); UiView::compose(frame,model,r.controller,clockNow); snapshot("touch-chargers",frame);
  model.action({ActionKind::Next},clockNow); CHECK(model.unitRank()==2);
  model.action({ActionKind::Configure},clockNow); model.action({ActionKind::EditCurrent},clockNow);
  CHECK(model.page()==UiPage::Editor && model.editing()); const auto old=r.controller.configuration().groups[0].current;
  model.action({ActionKind::Adjust,32000},clockNow); CHECK(model.editValue()==model.maximum());
  UiView::compose(frame,model,r.controller,clockNow); snapshot("touch-editor",frame);
  CHECK(r.controller.configuration().groups[0].current==old);
  model.action({ActionKind::Cancel},clockNow); CHECK(!model.editing() && r.controller.configuration().groups[0].current==old);
  model.action({ActionKind::Tools},clockNow); UiView::compose(frame,model,r.controller,clockNow); snapshot("touch-tools",frame);
  model.action({ActionKind::Scenarios},clockNow); UiView::compose(frame,model,r.controller,clockNow); snapshot("touch-scenarios",frame);
  CalibrationStore cal(r.bytes,r.memory); CHECK(cal.start(knownCalibration())==StorageResult::Ok); while(cal.saving()) cal.step();
  FakeDisplay display; LocalUi ui(r.controller,r.memory,display,r.bytes); input={}; ui.begin();
  auto tick=[&]() { r.step(); ui.storageTick(); ui.tick(clockNow); };
  auto tap=[&](int16_t x,int16_t y) { input={int16_t(x*8),int16_t(y*8),1000,true}; for(int i=0;i<5;++i) tick(); input={}; for(int i=0;i<12;++i) tick(); };
  for(int i=0;i<10;++i) tick();
  tap(40,80); // group -> chargers
  tap(112,216); // config
  tap(80,128); // current -> editor
  CHECK(ui.editing());
  // A held opening touch must not flow into the +/- control of the editor.
  tap(280,100); // plus
  input={280*8,100*8,1000,true}; for(int i=0;i<35;++i) tick(); input={}; for(int i=0;i<5;++i) tick();
  CHECK(r.controller.configuration().groups[0].current==old);
  tap(50,216); // cancel
  CHECK(r.controller.configuration().groups[0].current==old);
  CHECK(!ui.editing());
  snapshot("touch-local",display.shown);
  tap(80,128); CHECK(ui.editing()); tap(280,100);
  // Wheel input during a captured Apply press consumes the touch gesture.
  input={230*8,216*8,1000,true}; tick(); wheel={1,false,false}; tick();
  input={}; for(int i=0;i<5;++i) tick(); CHECK(ui.editing() && !r.controller.busy());
  tap(230,216); CHECK(!ui.editing());
  for(int i=0;i<400;++i) tick();
  Configuration saved; CHECK(r.memory.load(saved)==StorageResult::Ok);
  CHECK(saved.groups[0].current==old+100);
  CHECK(r.controller.report().failed==0 && r.controller.report().succeeded==7);
  snapshot("touch-saved",display.shown);
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
  const auto before=r.controller.configuration().groups[0].current;
  snapshot("touch-diagnostics",display.shown);
  input={}; for(int i=0;i<5;++i) tick(); CHECK(r.controller.configuration().groups[0].current==before);
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
void applicationScenarios() {
  clockNow=100; input={}; wheel={}; Serial.input.clear(); Serial.output.clear();
  Application app; app.begin();
  auto tick=[&](unsigned n) { while(n--) { clockNow+=20; app.tick(); } };
  auto command=[&](const std::string& s) { Serial.output.clear(); Serial.input=s+"\n"; tick(20); };
  auto tap=[&](int16_t x,int16_t y) { input={int16_t(x*8),int16_t(y*8),1000,true}; tick(5); input={}; tick(12); };
  auto editCurrent=[&]() { tap(40,80); tap(112,216); tap(80,128); };
  tick(800); CHECK(Serial.output.find("SIM:")!=std::string::npos);
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
  command("touch debug off");
  for(uint8_t s=0;s<uint8_t(DebugScenario::Count);++s) {
    command(std::string("ui-debug scenario ")+scenarioName(DebugScenario(s)).data); tick(1900);
    command("ui-debug status"); CHECK(Serial.output.find("priming=0")!=std::string::npos);
    CHECK(Serial.output.find("ERR")==std::string::npos);
    CHECK(hasText(appFrame,"SIM"));
    if (DebugScenario(s)==DebugScenario::Empty) {
      CHECK(hasText(appFrame,"No groups configured")); wheel={0,false,true}; tick(5); CHECK(hasText(appFrame,"TOOLS"));
    } else if (DebugScenario(s)==DebugScenario::Missing) CHECK(hasText(appFrame,"ERR 1/3"));
    else if (DebugScenario(s)==DebugScenario::BusOffline) CHECK(hasText(appFrame,"ERR 3/3"));
    else if (DebugScenario(s)==DebugScenario::Extremes) CHECK(hasText(appFrame,"LONGGRP1"));
    CHECK(Serial.output.find("ERR console output truncated")==std::string::npos);
    switch (DebugScenario(s)) {
      case DebugScenario::Changed:
        editCurrent(); CHECK(hasText(appFrame,"CONFIG / DEVICES CHANGED")); snapshot("touch-changed",appFrame); break;
      case DebugScenario::Missing:
        editCurrent(); tap(230,216); CHECK(hasText(appFrame,"MISSING MEMBER - BLOCKED"));
        snapshot("touch-confirm",appFrame); tap(230,216); tick(600);
        CHECK(hasText(appFrame,"PARTIAL APPLY SAVED")); snapshot("touch-partial",appFrame); break;
      case DebugScenario::VoltageUnsynced:
        editCurrent(); tap(230,216); tick(100); CHECK(hasText(appFrame,"APPLY BLOCKED")); break;
      case DebugScenario::SaveChanged: case DebugScenario::SaveFailed:
      case DebugScenario::Reject: case DebugScenario::Timeout: case DebugScenario::Transport: case DebugScenario::Partial:
        editCurrent(); tap(280,100); tap(230,216); tick(600);
        if (DebugScenario(s)==DebugScenario::SaveChanged) {
          CHECK(hasText(appFrame,"EARLIER SNAPSHOT SAVED")); snapshot("touch-saved-older",appFrame);
        } else if (DebugScenario(s)==DebugScenario::SaveFailed) {
          CHECK(hasText(appFrame,"APPLIED / EEPROM FAILED")); snapshot("touch-save-failed",appFrame);
        } else {
          CHECK(hasText(appFrame,"APPLY FAILED - NOT SAVED")); snapshot("touch-apply-failed",appFrame);
        }
        break;
      default: break;
    }
  }
  command("ui-debug scenario healthy"); tick(1000);
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
}

}
int main() { try { wizardTests(); calibrationStorage(); gestures(); simulationTests(); layoutAndLocalUi(); localCalibrationAndDiagnostics(); applicationScenarios();
  std::cout<<"PASS touch/simulator: "<<checks<<" checks\n";
} catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; } }
