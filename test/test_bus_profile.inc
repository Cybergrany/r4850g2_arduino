#if PSU_BUS_PROFILE
void busProfiling() {
  Rig r(installation(1),false,false); r.temperature=20; r.finish();
  Terminal t(r);
  const auto original=r.c.configuration();
  const auto before=r.can.sent.size();
  CHECK(t.input("busprofile off\n").find("OK")!=std::string::npos);
  CHECK(!r.c.busProfile().streaming() && r.can.sent.size()==before);
  const auto counted=r.c.busProfile().dataRx;
  r.step(250); CHECK(r.c.busProfile().dataRx>counted);
  CHECK(t.input("busprofile on\n").find("OK")!=std::string::npos);
  BusEventRecord event;
  while(r.c.busProfile().pop(event)) {}
  CHECK(t.input("busprofile mark 12\n").find("marker addr=0 value=12")!=std::string::npos);
  CHECK(t.input("diag profile\n").find("BUS S,")!=std::string::npos);
  for(unsigned n=0;n<20;++n) { r.step(250); t.drain(50); }
  CHECK(t.io.output.find("BUS U,")!=std::string::npos);
  CHECK(t.io.output.find("output truncated")==std::string::npos);
  for(const auto prefix : {"BUS U,", "BUS S,"}) {
    size_t at=0;
    while((at=t.io.output.find(prefix,at))!=std::string::npos) {
      const auto end=t.io.output.find('\r',at);
      CHECK(end!=std::string::npos && end-at+6<=busprofile::outputReserve);
      const auto line=t.io.output.substr(at,end-at);
      unsigned commas=0; for(char ch:line) if(ch==',') ++commas;
      CHECK(commas==(prefix[4]=='U' ? 23U : 26U)); at=end;
    }
  }
  CHECK(sameIdentity(original.units[0].identity,r.c.configuration().units[0].identity));
  CHECK(original.voltage[0]==r.c.configuration().voltage[0] && original.current[0]==r.c.configuration().current[0]);
  const auto rx=r.c.busProfile().dataRx;
  t.input("hello\n"); CHECK(r.c.busProfile().dataRx==rx && r.c.busProfile().streaming());

  // All application requests respect the existing negotiation quiet interval;
  // counting continues so the capture can verify silence rather than hide TX.
  r.c.scan();
  CanFrame negotiation={}; negotiation.id=0x1001107e; negotiation.extended=true; negotiation.length=8;
  negotiation.data[0]=0x30; negotiation.data[1]=1;
  std::memcpy(negotiation.data+2,identity(1).bytes,6);
  r.can.incoming.push_back(negotiation);
  const auto txBefore=r.can.sent.size(), writesBefore=r.can.writes().size();
  const auto infoBefore=r.c.busProfile().infoTx, negBefore=r.c.busProfile().duringNegotiation;
  for(unsigned n=0;n<10;++n) { r.step(100); t.drain(50); }
  CHECK(r.c.discovery().negotiating());
  CHECK(r.can.writes().size()==writesBefore);
  CHECK(r.can.sent.size()==txBefore && r.c.busProfile().infoTx==infoBefore);
  CHECK(r.c.busProfile().duringNegotiation==negBefore);
  CHECK(r.c.busProfile().negotiationRx==1);
  CHECK(t.io.output.find("negotiation addr=1")!=std::string::npos);
  r.step(control::availabilitySettleMs+1000);
  CHECK(!r.c.discovery().negotiating() && r.c.busProfile().infoTx>infoBefore);
  // Event storms and slow UARTs do not suppress command processing once the
  // ordinary bounded reply buffer drains. Alert/status output still gets turns.
  for(unsigned n=0;n<2000;++n) {
    r.c.busProfile().note(BusEvent::Marker,0,n,0,r.now);
    t.drain(1);
  }
  CHECK(t.input("autoresume off\n").find("OK")!=std::string::npos);
  CHECK(!r.c.enableControl().enabled());
  CHECK(t.io.output.find("output truncated")==std::string::npos);
  // Every textual event variant fits a reserved console row, including the
  // longest existing issue/recovery reason. Desktop formatting cannot conceal it.
  FakeSerial sink;
  for(unsigned issue=0;issue<=uint8_t(Issue::ParticipationHold);++issue) {
    sink.output.clear(); printBusEvent(sink,{UINT32_MAX,UINT32_MAX,uint16_t(issue<<8),BusEvent::Route,9});
    CHECK(sink.output.size()+4<=busprofile::outputReserve);
  }
}
#endif
