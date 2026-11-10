// Execute production control, composition and rendering on an emulated Mega.
// Compare one-slice scheduling with bounded priority drawing; no physical I/O.
#include <Arduino.h>
#include "debug/Simulator.h"
#include "ui/UiModel.h"
#include "ui/UiView.h"
#include "ui/Ili9341Display.h"
#include <stdlib.h>
void operator delete(void* p) { free(p); }
void operator delete(void* p, unsigned int) { free(p); }
using namespace psu;
volatile uint8_t benchPhase=0;
SimulatedCan bus; PsuController controller(bus); RamStorage bytes;
MemoryManager memory(bytes); UiModel model(controller,memory);
UiFrame frame; Ili9341Display display;
uint32_t offset;
void paint(bool priority) {
  while (display.pending()) {
    controller.tick(millis()+offset); display.service(frame);
    const auto started=micros();
    for(uint8_t n=0;priority && display.pending() && n<ui::priorityRenderSlices && uint32_t(micros()-started)<ui::priorityRenderUs;++n) {
      controller.serviceReceive(millis()+offset); display.service(frame);
    }
  }
}
void compose() { UiView::compose(frame,model,controller,millis()+offset); display.invalidate(true); }
int main() {
  init(); bus.reset(); bus.temperatures(30,25);
  auto config=debugConfiguration(); config.autoResume=false;
  controller.configure(config,false); controller.begin();
  uint32_t now=100; benchPhase=1;
  for(uint16_t i=0;i<1500;++i) { now+=50; controller.tick(now); }
  offset=now-millis(); controller.alerts().acknowledgeAll();
  for(uint8_t kind=0;kind<4;++kind) for(uint8_t priority=0;priority<2;++priority) {
    display.begin(); model.resetNavigation(); model.tick(millis()+offset);
    if(kind==1) model.input({0,true,false},millis()+offset);
    if(kind==2) model.action({ActionKind::Tools},millis()+offset);
    compose(); paint(true);
    benchPhase=2+kind*2+priority;
    if(kind==3) model.input({0,true,false},millis()+offset);
    else model.input({1,false,false},millis()+offset);
    compose(); paint(priority);
    benchPhase=1;
  }
  benchPhase=15; for(;;) {}
}
