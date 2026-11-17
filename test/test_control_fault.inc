void controlFaultEvidence() {
  Rig r(enableBootConfiguration(false),true,false); r.reportAc=true;
  r.c.configureStartup(true,true,r.now); r.finish();
  CHECK(!r.c.firstControlFault().slot);
  r.voltage=5230; r.reportedCurrent[4]=100; r.step();
  const auto& f=r.c.firstControlFault();
  CHECK(f.slot==5 && sameIdentity(f.identity,identity(5)) && f.address==5);
  CHECK(f.cause==FaultCause::ZeroHoldCurrent && f.state==CommandState::Changed && f.reg==protocol::OnlineCurrent);
  CHECK(f.phase==ControlPhase::Held && (f.flags&ControlFault::Zero) && !(f.flags&ControlFault::Enabled));
  CHECK(f.permit==PermitState::Allowed && r.c.startupStatus().permit==PermitState::Fault);
  CHECK(f.metrics&ControlFault::CurrentFresh); close(f.amps,1);
  CHECK(f.metrics&ControlFault::VoltageFresh); close(f.volts,52.3f,0.002f);
  CHECK(f.currentAge==0 && f.voltageAge==0);
  // The receive budget can expose the current before this poll's later status
  // and broadcast frames. Preserve their actual, independent ages.
  CHECK(f.broadcastAge<=250 && f.statusAge<=250);
  CHECK((f.metrics&ControlFault::Ack) && !f.ackRaw && f.ratedCurrent==5000);
  CHECK(f.authorizedCurrent==r.c.configuration().operating.current[4]);
  CHECK(f.route==Issue::None && f.demand==OutputDemand::Zero);
  std::array<uint8_t,sizeof(ControlFault)> original;
  std::memcpy(original.data(),&f,sizeof(f));
  r.reportedCurrent[4]=0; r.live=0; r.step(10000); r.c.reconcile();
  r.live=membersMask(r.c.count()); r.step(); r.step(); r.finish();
  r.reportedCurrent[2]=200; r.step();
  CHECK(std::memcmp(original.data(),&r.c.firstControlFault(),sizeof(f))==0);
  auto replaced=enableBootConfiguration(false); replaced.units[4].identity=identity(55);
  CHECK(r.c.configure(replaced)==Result::Ok);
  CHECK(std::memcmp(original.data(),&r.c.firstControlFault(),sizeof(f))==0);

  // Boundaries and rollover: an accepted zero plus exactly the threshold does
  // not fault. Retain the decoded sign as well as a near-zero boot timestamp.
  Rig edge(authorized(false),true,false); edge.finish();
  edge.reportedCurrent[0]=50; edge.step(); CHECK(!edge.c.firstControlFault().slot);
  edge.now=UINT32_MAX-5; edge.heartbeat(); edge.c.tick(edge.now);
  edge.step(10); edge.finish(); CHECK(!edge.c.firstControlFault().slot);
  auto* d=const_cast<Psu*>(edge.c.deviceForSlot(0,edge.now)); CHECK(d);
  edge.can.incoming.clear(); d->telemetry.values[protocol::OutputCurrent]=-0.75f;
  d->telemetry.updated[protocol::OutputCurrent]=edge.now;
  edge.c.tick(edge.now);
  CHECK(edge.c.firstControlFault().cause==FaultCause::ZeroHoldCurrent);
  close(edge.c.firstControlFault().amps,-0.75f);

  Rig rejected(authorized(false),true,false); rejected.rejectRegister=protocol::OnlineCurrent; rejected.finish();
  const auto& reject=rejected.c.firstControlFault();
  CHECK(reject.slot && reject.cause==FaultCause::Command && reject.state==CommandState::Rejected);
  CHECK((reject.flags&ControlFault::Command) && (reject.flags&ControlFault::Waiting));
  CHECK(reject.commandReg==protocol::OnlineCurrent && !reject.commandRaw && !(reject.metrics&ControlFault::Ack));
  Rig lostAck(authorized(false),true,false); lostAck.dropRegister=protocol::OnlineCurrent; lostAck.finish();
  CHECK(lostAck.c.firstControlFault().cause==FaultCause::Command);
  CHECK(lostAck.c.firstControlFault().state==CommandState::Timeout);
  CHECK((lostAck.c.firstControlFault().flags&ControlFault::Waiting) && !(lostAck.c.firstControlFault().metrics&ControlFault::Ack));
  Rig silent(authorized(false),true,false); silent.noCurrent=3; silent.finish();
  const auto& timeout=silent.c.firstControlFault();
  CHECK(timeout.slot && timeout.cause==FaultCause::ZeroObservation);
  CHECK(timeout.phase==ControlPhase::Stopping && timeout.state==CommandState::ObservationTimeout);
  CHECK((timeout.metrics&ControlFault::Ack) && !timeout.ackRaw && !(timeout.metrics&ControlFault::CurrentFresh));
  CHECK(timeout.currentAge>silent.c.staleMs());
  // Battery-connected startup investigation: zero was accepted, but each new
  // telemetry sample still reports current. Distinguish this from absent data.
  Rig flowing(authorized(false),true,false); flowing.reportedCurrent[0]=100; flowing.finish();
  const auto& flow=flowing.c.firstControlFault();
  CHECK(flow.slot==1 && flow.cause==FaultCause::ZeroObservation);
  CHECK((flow.metrics&ControlFault::CurrentFresh) && (flow.metrics&ControlFault::Ack) && !flow.ackRaw);
  CHECK(!(flow.flags&ControlFault::Zero)); close(flow.amps,1);
  auto draft=authorized(false); draft.operating.currentMask=0;
  Rig unapproved(draft,true,false); unapproved.finish();
  unapproved.reportedCurrent[0]=100; unapproved.step();
  CHECK(unapproved.c.firstControlFault().slot && unapproved.c.firstControlFault().authorizedCurrent==UINT16_MAX);
}
void controlFaultSerial() {
  Rig r(authorized(false),true,false); r.finish(); Terminal t(r);
  CHECK(t.input("diag fault\n").find("No control fault captured")!=std::string::npos);
  t.io.output.clear(); t.io.throttled=true; t.io.room=0;
  r.voltage=5230; r.reportedCurrent[0]=100; r.step();
  t.drain(100); CHECK(t.io.output.empty());
  for(unsigned n=0;n<2000;++n) { t.io.room=3; t.drain(1); }
  const auto& output=t.io.output;
  CHECK(output.find("FAULT SNAPSHOT first-since-boot")!=std::string::npos);
  CHECK(output.find("cause=current-exceeded-zero-hold")!=std::string::npos);
  CHECK(output.find("current-A=1.0000 age-ms=0 fresh=1")!=std::string::npos);
  CHECK(output.find("ack-raw=0 ack-limit-A=0.0000")!=std::string::npos);
  CHECK(output.find("voltage-V=52.300")!=std::string::npos || output.find("voltage-V=52.299")!=std::string::npos);
  CHECK(output.find("zero/standby/awake-before=1/0/1")!=std::string::npos);
  CHECK(output.find("truncated")==std::string::npos);
  CHECK(output.find("FAULT SNAPSHOT",output.find("FAULT SNAPSHOT")+1)==std::string::npos);
  t.io.throttled=false;
  CHECK(t.input("hello\n").find("FAULT SNAPSHOT")==std::string::npos);
  const auto retained=t.input("diag fault\n"); CHECK(retained.find("current-A=1.0000")!=std::string::npos);
  CHECK(t.input("alerts ack all\n").find("FAULT SNAPSHOT")==std::string::npos);
  CHECK(t.input("diag bus\n").find("output-overruns=0")!=std::string::npos);
  // Exercise maximum-width fields in the production bounded renderer. This is
  // presentation-only fixture injection; the normal capture above is real.
  auto& wide=const_cast<ControlFault&>(r.c.firstControlFault());
  wide.ms=wide.phaseMs=UINT32_MAX; wide.currentAge=wide.voltageAge=UINT32_MAX-1;
  wide.broadcastAge=wide.statusAge=UINT32_MAX-1; wide.status=UINT32_MAX;
  wide.amps=-123456.789f; wide.volts=123456.789f;
  wide.metrics=wide.flags=255; wide.commandReg=255; wide.commandRaw=wide.ackRaw=65535;
  wide.ratedCurrent=wide.authorizedCurrent=65535; wide.phase=ControlPhase::Starting;
  wide.cause=FaultCause::ZeroObservation; wide.state=CommandState::Changed;
  wide.permit=PermitState::Inhibited; wide.source=EnableSource::Protection;
  const auto before=r.can.sent.size();
  const auto wideOutput=t.input("diag fault\n");
  CHECK(wideOutput.find("truncated")==std::string::npos);
  CHECK(wideOutput.find("authorized-A=none")!=std::string::npos);
  CHECK(r.can.sent.size()==before); // Diagnostic requests cannot operate a PSU.
  CHECK(t.input("diag bus\n").find("output-overruns=0")!=std::string::npos);
}
