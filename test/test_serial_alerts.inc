size_t broadcasts(const std::string& text) {
  size_t count=0, at=0;
  while ((at=text.find("ALERTS uptime-ms=",at))!=std::string::npos) { ++count; ++at; }
  return count;
}
void serialAlertCadence() {
  Rig r; Terminal t(r);
  for (uint8_t i=0;i<AlertManager::Count;++i) r.c.alerts().reset(i);
  CHECK(t.input("alerts every\n").find("every 5 s")!=std::string::npos);
  CHECK(t.input("alerts every 2\n").find("every 2 s")!=std::string::npos);
  r.c.alerts().set(0,AlertCode::Missing,r.now);
  r.c.alerts().set(1,AlertCode::Command,r.now);
  const auto sample=[&](uint32_t elapsed) {
    t.io.output.clear(); r.now+=elapsed; t.drain(); return t.io.output;
  };
  CHECK(!broadcasts(sample(1999)));
  auto text=sample(1); CHECK(broadcasts(text)==1);
  CHECK(text.find("active WARNING=1 ERROR=1")!=std::string::npos);
  CHECK(text.find("ERROR PSU 2 GROUP1")<text.find("WARNING PSU 1 GROUP1"));
  CHECK(text.find("Setting/fan command failed; Restore blocked.")!=std::string::npos);
  CHECK(!r.c.alerts().at(0).acknowledged && !r.c.alerts().at(1).acknowledged);
  t.input("alerts ack all\n");
  CHECK(!broadcasts(sample(1999)));
  text=sample(1); CHECK(broadcasts(text)==1 && text.find("active acknowledged:")!=std::string::npos);
  CHECK(r.c.alerts().warning() && r.c.alerts().error());
  r.c.alerts().set(0,AlertCode::None,r.now);
  text=sample(2000); CHECK(text.find("active WARNING=0 ERROR=1")!=std::string::npos);
  CHECK(text.find("WARNING PSU 1 GROUP1 cleared acknowledged:")!=std::string::npos);
  CHECK(sample(2000).find("WARNING PSU 1")==std::string::npos);
  // A transient is still reported from the retained unacknowledged incident.
  r.c.alerts().set(AlertManager::Receive,AlertCode::ReceiveLoss,r.now);
  r.c.alerts().set(AlertManager::Receive,AlertCode::None,r.now);
  text=sample(2000); CHECK(text.find("cleared unacknowledged: Recent CAN receive loss")!=std::string::npos);
  // A different new alert must not repeat previously reported cleared history.
  r.c.alerts().set(AlertManager::LocalUi,AlertCode::Display,r.now);
  text=sample(2000); CHECK(text.find("Display unavailable")!=std::string::npos);
  CHECK(text.find("Recent CAN receive loss")==std::string::npos);
  r.c.alerts().set(1,AlertCode::None,r.now);
  r.c.alerts().set(AlertManager::LocalUi,AlertCode::None,r.now);
  text=sample(2000); CHECK(broadcasts(text)==1 && text.find("(all clear)")!=std::string::npos);
  CHECK(!broadcasts(sample(2000)));
  CHECK(t.input("alerts\n").find("Recent CAN receive loss")!=std::string::npos);
  for (const auto bad : {"-1", "1.5", "3601", "abc"}) {
    CHECK(t.input(std::string("alerts every ")+bad+"\n").find("ERR")!=std::string::npos);
    CHECK(t.input("alerts every\n").find("every 2 s")!=std::string::npos);
  }
  t.input("alerts every 0\n"); r.c.alerts().set(0,AlertCode::Missing,r.now);
  CHECK(!broadcasts(sample(10000)) && r.c.alerts().warning());
  t.input("alerts every 1\n"); t.input("alerts ack all\n");
  t.input("\x18"); t.input("hello\n");
  CHECK(t.input("alerts every\n").find("every 1 s")!=std::string::npos);
  text=sample(1000); CHECK(broadcasts(text)==1 && text.find("active acknowledged:")!=std::string::npos);
  // No asynchronous summary interrupts a partial command or a requested report.
  t.input("set current GROUP1 ",false); CHECK(!broadcasts(sample(1000)));
  text=t.input("1\n"); CHECK(groupCurrent(r.c.configuration(), 0)==100);
  CHECK(text.find("OK")<text.find("ALERTS uptime-ms="));
  r.now+=1000; text=t.input("diag psu 1\n");
  CHECK(text.find("authorized/effective/acknowledged A=")<text.find("ALERTS uptime-ms="));
  // Expired or discarded input is not an indefinitely active editor.
  t.input("abandoned",false); text=sample(console::inputIdleTimeoutMs);
  CHECK(text.find("Input expired")!=std::string::npos && broadcasts(text)==1);
  t.input("\x18"); t.input(std::string(100,'x'),false);
  text=sample(1000); CHECK(broadcasts(text)==1 && text.find("truncated")==std::string::npos);
  t.input("\x18");
  // The interval survives rollover and console-only reconnects.
  r.now=UINT32_MAX-999; t.input("alerts every 2\n");
  CHECK(!broadcasts(sample(1999)) && broadcasts(sample(1))==1);
}
void serialAlertStorm() {
  Rig r(installation(8)); Terminal t(r);
  t.input("alerts every 1\n"); t.io.output.clear();
  t.io.throttled=true; t.io.room=0;
  t.io.feed("set current GROUP1 12\n");
  const auto tick=[&](uint16_t n, uint8_t room) {
    for (uint16_t i=0;i<n;++i) {
      r.step(1); r.c.setAutoResume(i&1); // Continually changing status must not starve summaries.
      for (uint8_t s=0;s<AlertManager::Count;++s)
        r.c.alerts().set(s,(i+s)&1 ? AlertCode::Command : AlertCode::Missing,r.now);
      t.io.room=room; const auto written=t.io.written; t.drain(1);
      CHECK(t.io.written-written<=room && r.can.incoming.empty());
    }
  };
  tick(3000,0); CHECK(t.io.output.empty() && !t.io.available());
  tick(5000,3); CHECK(groupCurrent(r.c.configuration(), 0)==1200);
  CHECK(broadcasts(t.io.output)>0 && broadcasts(t.io.output)<=5);
  CHECK(t.io.output.find("ALERTS changed")==std::string::npos);
  CHECK(t.io.output.find("PSU 8 GROUP1")!=std::string::npos);
  CHECK(t.io.output.find("truncated")==std::string::npos);
  t.io.throttled=false;
  CHECK(t.input("alerts every 0\n").find("every 0 s")!=std::string::npos);
  CHECK(t.input("diag bus\n").find("output-overruns=0")!=std::string::npos);
}
