// Drive the real scheduler, identity checks, ACK handling and console together.
void serialSoak(Rig& r, Terminal& t, uint32_t ms) {
  for (uint32_t elapsed=0; elapsed<ms; elapsed+=25) { r.step(25); t.drain(10); }
}
void serialQuietRefresh() {
  Rig r(installation(9),false,false); r.temperature=20; r.finish(); r.step(40000);
  Terminal t(r); t.input("raw off\n"); t.io.output.clear();
  const auto before=r.can.writes().size();
  // A closed/backpressured console must not accumulate benign refresh traces.
  t.io.throttled=true; t.io.room=0; serialSoak(r,t,60000);
  const auto writes=r.can.writes(); CHECK(writes.size()>=before+72);
  for(size_t i=before;i<writes.size();++i) {
    CHECK(writes[i].data[1]==protocol::OnlineVoltage || writes[i].data[1]==protocol::OnlineCurrent);
    if(writes[i].data[1]==protocol::OnlineCurrent) CHECK(!protocol::readBigEndian(writes[i].data+4));
  }
  for(uint8_t i=0;i<r.c.count();++i) CHECK(r.c.unitControl(i).phase==ControlPhase::Held && r.current[i]==0);
  t.io.throttled=false; t.drain();
  CHECK(t.io.output.find("ACK PSU")==std::string::npos);
  CHECK(t.input("diag bus\n").find("trace-drops=0")!=std::string::npos);
  // Operator writes/results remain visible, including normal positive current.
  t.input("set current all 9\n"); t.input("autoresume on\n");
  CHECK(t.input("apply all\n").find("QUEUED")!=std::string::npos);
  serialSoak(r,t,20000);
  CHECK(t.io.output.find("ACK PSU 1 reg=0 accepted")!=std::string::npos);
  CHECK(t.io.output.find("ACK PSU 9 reg=3 accepted")!=std::string::npos);
  CHECK(t.io.output.find("RESULT accepted slots=1,2,3,4,5,6,7,8,9")!=std::string::npos);
  t.io.output.clear(); const auto activeBefore=r.can.writes().size(); serialSoak(r,t,20000);
  CHECK(r.can.writes().size()>activeBefore && t.io.output.find("ACK PSU")==std::string::npos);
  // Thermal corrections share the control worker but must not be quieted.
  r.temperature=65; t.io.output.clear(); serialSoak(r,t,8000);
  CHECK(r.c.thermalStatus().level==ThermalLevel::Warning);
  CHECK(t.io.output.find("reg=3 accepted raw=")!=std::string::npos);
  CHECK(t.io.output.find("fan=MAX")!=std::string::npos);
  CHECK(t.io.output.find("WARNING")!=std::string::npos);
}
void serialRefreshTraceAndFaults() {
  Rig r(installation(1),false,false); r.temperature=20; r.finish(); r.step(40000); Terminal t(r);
  t.input("raw on\n"); t.io.output.clear(); serialSoak(r,t,15000);
  CHECK(t.io.output.find("ACK PSU 1 reg=0 accepted")!=std::string::npos);
  CHECK(t.io.output.find("ACK PSU 1 reg=3 accepted")!=std::string::npos);
  t.input("raw off\n"); t.io.output.clear();
  // Unmatched success and unknown/rejected replies remain diagnostic evidence.
  auto unmatched=ack(protocol::setting(1,protocol::OnlineCurrent,1234));
  r.can.incoming.push_back(unmatched); r.c.tick(r.now); t.drain();
  CHECK(t.io.output.find("accepted raw=1234")!=std::string::npos);
  unmatched.data[0]=0x77; t.io.output.clear(); r.can.incoming.push_back(unmatched); r.c.tick(r.now); t.drain();
  CHECK(t.io.output.find("unknown-status raw=1234")!=std::string::npos);
  // Manual fan acceptance is not background refresh traffic.
  t.input("fan all max\n"); serialSoak(r,t,2000); CHECK(t.io.output.find("fan=MAX")!=std::string::npos);
  t.input("fan all auto\n"); serialSoak(r,t,2000); CHECK(t.io.output.find("fan=AUTO")!=std::string::npos);
  r.rejectRegister=protocol::OnlineVoltage; t.io.output.clear(); serialSoak(r,t,15000);
  CHECK(r.c.unitControl(0).fault==CommandState::Rejected);
  CHECK(t.io.output.find("reg=0 rejected raw=")!=std::string::npos);
  CHECK(t.io.output.find("ERROR")!=std::string::npos);
  // Missing ACKs still reach the normal timeout/fault/alert path.
  Rig timeout(installation(1),false,false); timeout.temperature=20; timeout.finish(); timeout.step(40000); Terminal tt(timeout);
  timeout.dropRegister=protocol::OnlineVoltage; tt.io.output.clear(); serialSoak(timeout,tt,20000);
  CHECK(timeout.c.unitControl(0).fault==CommandState::Timeout);
  CHECK(tt.io.output.find("ERROR")!=std::string::npos);
  CHECK(tt.input("diag control\n").find("timeout")!=std::string::npos);
}
