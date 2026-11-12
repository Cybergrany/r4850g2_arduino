#include "observer/BusObserver.h"
#include "diagnostics/BusProfileText.h"
#include "protocol/HuaweiProtocol.h"
#include "config/ControlConfig.h"
#include <cassert>
#include <deque>
#include <iostream>
using namespace psu;
struct SerialSink : Stream {
  std::string input, output;
  int capacity=64;
  int available() override { return input.size(); }
  int availableForWrite() override { return capacity; }
  int read() override { const auto c=input.front(); input.erase(0,1); return c; }
  size_t write(uint8_t c) override { assert(capacity); output+=char(c); return 1; }
};
struct Can : CanTransport {
  bool good=true; unsigned serviceCalls=0;
  std::deque<CanFrame> input;
  bool begin() override { return good; }
  bool send(const CanFrame&) override { assert(false && "Observer sent a CAN command"); return false; }
  void service(uint32_t) override { assert(good); ++serviceCalls; }
  bool receive(CanFrame& f) override {
    assert(good); if(input.empty()) return false; f=input.front(); input.pop_front(); return true;
  }
};
void commands(BusObserver& o,SerialSink& s,const std::string& text,uint32_t now) {
  s.input+=text;
  do { o.tick(now); } while(!s.input.empty());
  for(unsigned i=0;i<50;++i) o.tick(now);
}
CanFrame negotiation(uint8_t n=1) {
  CanFrame f={}; f.id=0x1001107e; f.length=8; f.extended=true;
  f.data[0]=0x30; f.data[1]=n; f.data[2]=0x6c; f.data[3]=0x68; f.data[6]=0x18; f.data[7]=0x55; return f;
}
void records() {
  BusProfile p; BusEventRecord e;
  static_assert(sizeof(BusEventRecord)==12,"Compact fixed event record");
  for(unsigned round=0;round<5;++round) {
    for(unsigned i=0;i<busprofile::eventSlots;++i) p.note(BusEvent::Marker,0,i,0,i);
    p.note(BusEvent::Marker,0,99,0,99);
    assert(p.lost()==round+1);
    for(unsigned i=0;i<busprofile::eventSlots;++i) { assert(p.pop(e)); assert(e.value==i); }
    assert(!p.pop(e));
  }
  p.stream(false); p.requested(protocol::request(1),0,true,1); p.completed(false,1);
  assert(p.dataTx==1 && p.duringNegotiation==1 && p.failed==1 && !p.streaming());
  assert(p.pop(e) && e.kind==BusEvent::TxFailure);
  auto fan=protocol::toggle(1,protocol::FanMode,true); fan.id &= ~0x80UL;
  p.received(fan,42); assert(p.pop(e) && e.value==1 && e.detail==0x0134);
  fan.data[0]=0x21; p.received(fan,43); assert(p.pop(e) && e.detail==0x2134);
  p.infoTx=UINT16_MAX; p.requested(protocol::request(1,protocol::infoCommand),0,false,1); assert(p.infoTx==0);
  p.summaryPrinted(UINT32_MAX-500); assert(!p.summaryDue(498)); assert(p.summaryDue(499));
  SerialSink sink;
  p.negotiation(negotiation(2),44); assert(p.pop(e)); printBusEvent(sink,e);
  assert(sink.output.find("id=6C6800001855 type=30-02")!=std::string::npos);
  BusReceiveStats stats; stats.received=stats.dropped=stats.invalid=stats.maxServiceGapUs=UINT32_MAX;
  stats.maxQueueAgeMs=stats.hardwareOverflows=stats.negotiationFrames=UINT16_MAX; stats.highWater=255;
  stats.errorFlags=stats.txErrorCount=stats.rxErrorCount=255;
  p.infoTx=p.dataTx=p.describeTx=p.settingTx=p.sent=p.failed=p.duringNegotiation=UINT16_MAX;
  p.negotiationRx=p.infoRx=p.dataRx=p.broadcastRx=p.ackRx=p.otherRx=UINT16_MAX;
  for(unsigned i=0;i<70000;++i) p.note(BusEvent::Marker,0,0,0,0);
  assert(p.lost()==UINT16_MAX); sink.output.clear(); printBusSummary(sink,p,stats,UINT32_MAX);
  assert(sink.output.size()+4<=busprofile::outputReserve);
  for(uint8_t kind=0;kind<=uint8_t(BusEvent::AcReview);++kind) {
    sink.output.clear(); printBusEvent(sink,{UINT32_MAX,UINT32_MAX,UINT16_MAX,BusEvent(kind),255});
    assert(sink.output.size()+4<=busprofile::outputReserve);
  }
}
void observer() {
  for(uint32_t start : {0U,UINT32_MAX-10000}) {
    Can can; SerialSink serial; BusObserver o(can,serial); o.begin(start);
    commands(o,serial,"hello\r\n",start); assert(o.ready() && !o.allowAc());
    commands(o,serial,"confirm yes\n",start); assert(!o.allowAc());
    commands(o,serial,"ac on\r\n",start); assert(!o.allowAc());
    assert(serial.output.find("cannot hold zero current")!=std::string::npos);
    commands(o,serial,"confirm yes\n",start+15000); assert(!o.allowAc());
    commands(o,serial,"ac on\nconfirm yes\n",start+16000); assert(o.allowAc());
    serial.capacity=0; // Never wait for a connected terminal to process OFF.
    for(unsigned n=0;n<500;++n) { can.input.push_back(negotiation()); o.tick(start+17000+n); }
    assert(o.profile().negotiationRx==500 && o.profile().lost()>0);
    commands(o,serial,"ac off\n",start+18000); assert(!o.allowAc());
    commands(o,serial,"ac on\nac off\nconfirm yes\n",start+18001); assert(!o.allowAc());
    commands(o,serial,"ac on\n"+std::string(60,'X')+"\nconfirm yes\n",start+18002); assert(!o.allowAc());
    commands(o,serial,"ac on\n\x18" "confirm yes\n",start+18003); assert(!o.allowAc());
    serial.capacity=64; commands(o,serial,"profile off\n",start+19000);
    commands(o,serial,"ac on\nconfirm ",start+20000);
    commands(o,serial,"yes\n",start+51000); assert(!o.allowAc());
    commands(o,serial,"ac on\nconfirm yes\n",start+52000); assert(o.allowAc());
    commands(o,serial,"hello\n",start+53000); assert(o.allowAc()); // Reconnect is not a power action.
    commands(o,serial,"ac off\n",start+54000); assert(!o.allowAc());
    for(unsigned n=1;n<=13;++n) { auto f=protocol::request(n); f.id&=~0x80UL; can.input.push_back(f); }
    commands(o,serial,"status\n",start+55000);
    assert(serial.output.find("table-loss=1")!=std::string::npos);
    assert(o.profile().sent==0 && o.profile().settingTx==0 && o.profile().infoTx==0);
  }
  Can bad; bad.good=false; SerialSink serial; BusObserver o(bad,serial); o.begin(0);
  commands(o,serial,"ac on\nconfirm yes\n",1); assert(!o.ready() && !o.allowAc() && !bad.serviceCalls);
}
int main() { records(); observer(); std::cout<<"PASS: bounded profile records, rollover, observer AC guards, backpressure and zero CAN sends\n"; }
