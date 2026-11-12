#include <CAN.h>
#include "can/Mcp2515Transport.h"
#include "protocol/HuaweiProtocol.h"
#include <cassert>
#include <iostream>
TestSpi SPI; TestCan CAN;
uint32_t clockMs=0,clockUs=0;
uint32_t millis() { return clockMs; }
uint32_t micros() { return clockUs; }
void digitalWrite(uint8_t,uint8_t) {}
void pinMode(uint8_t,uint8_t) {}
using namespace psu;
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
  clockUs=100; clockMs=65540;
  for(unsigned n=0;n<15;++n) { assert(can.receive(f)); assert(f.id==valid.id); }
  assert(!can.receive(f));
  BusReceiveStats s; can.busReceiveStats(s);
  assert(s.received==319 && s.dropped==301 && s.invalid==3 && s.negotiationFrames==1);
  assert(s.highWater==15 && s.maxQueueAgeMs==10 && s.maxServiceGapUs==201);
  assert(can.droppedFrames()==255 && can.takeReceiveLoss() && !can.takeReceiveLoss());
  SPI.registers[0x2d]=0xa5; SPI.registers[0x1c]=23; SPI.registers[0x1d]=78;
  can.service(clockMs); can.busReceiveStats(s);
  assert(s.hardwareOverflows==1 && s.errorFlags==0x25 && s.txErrorCount==23 && s.rxErrorCount==78);
  assert(can.takeReceiveLoss() && !can.takeReceiveLoss());
  for(unsigned n=0;n<70;++n) { CAN.inject(negotiation); assert(can.receive(f)); assert(f.data[1]==1); }
  clockMs+=70000; clockUs+=70000000; can.receive(f); can.busReceiveStats(s);
  assert(s.negotiationFrames==71 && s.maxQueueAgeMs==UINT16_MAX && s.maxServiceGapUs==70000000);
#if PSU_CAN_OBSERVER
  const auto before=SPI.operations; assert(!can.send(valid)); assert(SPI.operations==before);
  assert(can.transmitState()==TransmitState::Idle);
#endif
  std::cout<<"PASS: production RX ISR counters, queue loss, invalid frames, timestamp rollover and observer transmit isolation\n";
}
