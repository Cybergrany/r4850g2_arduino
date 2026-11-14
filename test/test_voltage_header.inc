void voltageHeader() {
  UiFrame frame;
  const auto paint = [&](Rig& r, const char* name = nullptr) {
    UiRig u(r); UiView::compose(frame, u.ui, r.c, r.now);
    if (name) snapshot(name, frame);
    const auto row = rowText(frame, 0);
    CHECK(row.size() == 26 && row.find('~') == std::string::npos);
    CHECK(row[16] == ' ' && row[22] == ' ' && row.substr(23) == "1/1");
    return row;
  };
  const auto drain = [](Rig& r) { while (!r.can.incoming.empty()) r.c.serviceReceive(r.now); };
  const auto volts = [&](Rig& r, uint8_t slot, uint32_t raw) {
    r.can.incoming.push_back(data(r.addresses[slot], 0x75, raw)); drain(r);
  };

  // Actual nine-unit bench readings: one common authorized 54 V target.
  Rig bench(installation(9));
  const uint16_t measured[] = {5404,5409,5407,5406,5416,5410,5408,5411,5408};
  for (uint8_t i = 0; i < 9; ++i) volts(bench, i, uint32_t(measured[i])*1024/100);
  CHECK(paint(bench, "header-voltage-bench").substr(0,11) == "54.0/54.1V ");
  CHECK(frame.cell(0).style == Text);
  CHECK(bench.c.setVoltage(5700) == Result::Ok);
  CHECK(paint(bench).substr(0,11) == "54.0/54.1V "); // Draft does not replace authorized target.
  for (uint8_t i = 0; i < 9; ++i) volts(bench, i, 53*1024UL);
  CHECK(paint(bench).substr(0,11) == "54.0/53.0V "); // Agreement is around the measured mean.

  Rig edges;
  volts(edges, 0, 54*1024-256); volts(edges, 1, 54*1024+256);
  CHECK(paint(edges).substr(0,11) == "54.0/54.0V "); // Inclusive +/-0.25 V.
  volts(edges, 1, 54*1024+257);
  CHECK(paint(edges, "header-voltage-spread").substr(0,11) == "54.0/MIXV  ");
  CHECK(frame.cell(0).style == Warning);
  volts(edges, 0, 54*1024); volts(edges, 1, 54*1024);
  CHECK(paint(edges).substr(0,11) == "54.0/54.0V ");
  Rig skewed(installation(3));
  volts(skewed, 2, 54*1024+512);
  CHECK(paint(skewed).substr(0,11) == "54.0/MIXV  "); // Half-volt span alone cannot prove agreement.

  auto cfg = installation(); cfg.operating.voltageMask = 3;
  cfg.operating.voltage[0] = 5400; cfg.operating.voltage[1] = 5401;
  Rig mixed(cfg, false, false); mixed.finish();
  CHECK(paint(mixed, "header-voltage-targets").substr(0,11) == "MIX/54.0V  "); // Exact target comparison.
  volts(mixed, 1, 56*1024);
  CHECK(paint(mixed).substr(0,11) == "MIX/MIXV   ");
  cfg.operating.voltageMask = 1;
  Rig partialTarget(cfg, false, false); partialTarget.finish();
  CHECK(paint(partialTarget).substr(0,11) == "54.0/54.0V*");
  cfg.operating.voltageMask = 0;
  Rig noTarget(cfg, false, false); noTarget.finish();
  CHECK(paint(noTarget).substr(0,11) == "--/54.0V   ");

  Rig stale;
  stale.noVoltage = 2; stale.step(stale.c.staleMs()+1000);
  CHECK(paint(stale, "header-voltage-partial").substr(0,11) == "54.0/54.0V*");
  CHECK(frame.cell(0).style == Warning);
  stale.noVoltage = 3; stale.step(stale.c.staleMs()+1000);
  CHECK(paint(stale).substr(0,11) == "54.0/--V   ");
  noTarget.noVoltage = 3; noTarget.step(noTarget.c.staleMs()+1000);
  CHECK(paint(noTarget, "header-voltage-unknown").substr(0,11) == "--/--V     ");

  // Compact A/W fields still fit maximum nine-unit totals, with partial markers.
  for (uint8_t i = 0; i < 9; ++i) {
    bench.can.incoming.push_back(data(i+1, 0x81, 60*1024UL));
    bench.can.incoming.push_back(data(i+1, 0x73, 3510*1024UL));
  }
  drain(bench);
  auto row = paint(bench, "header-voltage-full-load");
  CHECK(row.substr(11,5) == " 540A" && row.substr(17,5) == " 32kW");
  // Keep identities fresh but let one unit's voltage/current/power expire.
  bench.noVoltage = bench.noCurrent = 0x100;
  bench.step(bench.c.staleMs()+1000);
  for (uint8_t i = 0; i < 8; ++i) {
    bench.can.incoming.push_back(data(i+1, 0x81, 60*1024UL));
    bench.can.incoming.push_back(data(i+1, 0x73, 3510*1024UL));
  }
  drain(bench); row = paint(bench, "header-voltage-partial-load");
  CHECK(row[10] == '*' && row.substr(11,5) == "480A*" && row.substr(17,5) == "28kW*");

  Rig wrap(installation(), false, true, UINT32_MAX-10000);
  wrap.step(20000); CHECK(wrap.now < 20000);
  CHECK(paint(wrap).substr(0,11) == "54.0/54.0V ");
}
