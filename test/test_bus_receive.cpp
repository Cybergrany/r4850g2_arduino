#include <CAN.h>
#include "can/Mcp2515Transport.h"
#include "protocol/HuaweiProtocol.h"
#include "diagnostics/RuntimeTiming.h"
#include <cassert>
#include <iostream>
TestSpi SPI; TestCan CAN;
uint32_t clockMs=0,clockUs=0;
uint32_t millis() { return clockMs; }
uint32_t micros() { return clockUs; }
void digitalWrite(uint8_t pin,uint8_t value) { assert(pin==10); SPI.select(value); }
void pinMode(uint8_t,uint8_t) {}
void attachInterrupt(uint8_t pin,void (*fn)(),int mode) { assert(pin==2 && mode==LOW); CAN.callback=fn; }
using namespace psu;
CanFrame refill={};
unsigned refills=0;
void refillAtBoundary() {
  if (!refills || (SPI.command!=0x90 && SPI.command!=0x94)) return;
  SPI.load(refill,SPI.command==0x90 ? 0 : 1); --refills;
}
void advanceClock() { clockUs+=2; }
int main() {
  Mcp2515Transport can; assert(can.begin()); CanFrame f;
  clockUs=UINT32_MAX-100; clockMs=65530; assert(!can.receive(f));
  auto valid=protocol::request(1); valid.id&=~0x80UL;
  for(unsigned n=0;n<15;++n) CAN.inject(valid);
  CanFrame negotiation={}; negotiation.id=0x1001107e; negotiation.length=8; negotiation.extended=true;
  negotiation.data[0]=0x30; negotiation.data[1]=1;
  CAN.inject(negotiation); // Count negotiation even though the full queue drops it.
  for(unsigned n=0;n<300;++n) CAN.inject(valid);
  auto invalid=valid; invalid.extended=false; CAN.inject(invalid);
  invalid=valid; invalid.length=7; CAN.inject(invalid);
  invalid=valid; invalid.rtr=true; CAN.inject(invalid);
  invalid=valid; invalid.length=15; CAN.inject(invalid); // Malformed DLC never reads past eight bytes.
  clockUs=100; clockMs=65540;
  for(unsigned n=0;n<15;++n) { assert(can.receive(f)); assert(f.id==valid.id); }
  assert(!can.receive(f));
  BusReceiveStats s; can.busReceiveStats(s);
  assert(s.received==320 && s.dropped==301 && s.invalid==4 && s.negotiationFrames==1);
  assert(s.highWater==15 && s.maxQueueAgeMs==10 && s.maxServiceGapUs==201);
  assert(can.droppedFrames()==255 && can.takeReceiveLoss() && !can.takeReceiveLoss());
  SPI.registers[0x2d]=0xa5; SPI.registers[0x1c]=23; SPI.registers[0x1d]=78;
  can.service(clockMs); can.busReceiveStats(s);
  assert(s.hardwareOverflows==1 && s.errorFlags==0x25 && s.txErrorCount==23 && s.rxErrorCount==78);
  assert(can.takeReceiveLoss() && !can.takeReceiveLoss());
  for(unsigned n=0;n<70;++n) { CAN.inject(negotiation); assert(can.receive(f)); assert(f.data[1]==1); }
  clockMs+=70000; clockUs+=70000000; can.receive(f); can.busReceiveStats(s);
  assert(s.negotiationFrames==71 && s.maxQueueAgeMs==UINT16_MAX && s.maxServiceGapUs==70000000);
  // Both hardware buffers filled and immediately refilled at CS release. One
  // ISR must read only its initial snapshot; neither buffer can starve the other.
  refill=valid; refills=2; SPI.load(valid,0); SPI.load(negotiation,1);
  SPI.boundary=refillAtBoundary; SPI.duringTransfer=advanceClock;
  const auto before=SPI.operations;
  const auto bursts=SPI.bursts.size();
  CAN.callback();
  assert(SPI.operations-before==3 && SPI.bursts.size()-bursts==2);
  assert(SPI.bursts[bursts]==0x90 && SPI.bursts[bursts+1]==0x94 && SPI.registers[0x2c]==3);
  assert(can.receivePending()==2 && can.receive(f) && f.id==valid.id);
  assert(can.receive(f) && f.id==negotiation.id);
  SPI.boundary=nullptr; CAN.callback(); assert(!(SPI.registers[0x2c]&3));
  assert(can.receive(f) && can.receive(f) && !can.receive(f));
  assert(can.receiveInterruptMaxUs()==62); // 3-byte flag read + two 14-byte bursts.
  for(auto size:SPI.lengths) assert(size<=14);
  SPI.duringTransfer=nullptr; assert(!can.receive(f));
  // A negative timing sample is diagnosed, not mistaken for a 71-minute gap.
  can.busReceiveStats(s); const auto maximum=s.maxServiceGapUs;
  clockUs-=500; assert(!can.receive(f)); can.busReceiveStats(s);
  assert(s.maxServiceGapUs==maximum && timing::diagnostics().invalidSamples==1);
  assert(timing::diagnostics().maxBackstepUs==500);
#if PSU_CAN_OBSERVER
  const auto beforeSend=SPI.operations; assert(!can.send(valid)); assert(SPI.operations==beforeSend);
  assert(can.transmitState()==TransmitState::Idle);
#endif
  std::cout<<"PASS: bounded production RX ISR, two-buffer refill, burst transactions, queue loss, invalid DLC, clock rollover/regression, observer isolation\n";
}
