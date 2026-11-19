// Exercise real discovery, zero/wake, sync and presentation with boot ENABLE_IN
// and negotiated address moves. Synthetic peers are not physical timing proof.
CanFrame negotiationFrame(uint8_t kind = 1) {
  CanFrame f = {}; f.extended = true; f.length = 8; f.id = 0x1000107e;
  f.data[0] = 0x30; f.data[1] = kind;
  std::memcpy(f.data + 2, identity(0).bytes, 6); return f;
}
bool positiveCurrent(const CanFrame& f) {
  return protocol::command(f.id) == protocol::setCommand && f.data[1] == protocol::OnlineCurrent &&
      protocol::readBigEndian(f.data + 4);
}
void addressQualification() {
  for (const uint32_t start : {100U, 0xfffff000U}) {
    Discovery d; uint32_t now = start;
    d.receive(info(1, identity(1)), now); d.tick(now, 5000);
    now += 200; d.receive(info(1, identity(1)), now); d.tick(now, 5000);
    CHECK(!d.settled() && d.settleRemaining(now) == control::addressStableMs);
    const auto stableAt = now + control::addressStableMs;
    // Ordinary INFO/telemetry must not continually restart qualification.
    for (unsigned n = 0; n < 19; ++n) {
      now += 250; d.receive(info(1, identity(1)), now); d.tick(now, 5000); CHECK(!d.settled());
    }
    d.receive(info(1, identity(1)), stableAt); d.tick(stableAt, 5000); CHECK(d.settled());
    now = stableAt + 1; d.receive(negotiationFrame(), now); d.tick(now, 5000);
    CHECK(!d.settled() && d.negotiating() && !d.identity(identity(1), now));
    now += control::negotiationQuietMs - 1; d.receive(negotiationFrame(2), now); d.tick(now, 5000);
    CHECK(d.quietRemaining(now) == control::negotiationQuietMs);
    d.receive(info(1, identity(1)), now); CHECK(!d.address(1)); // Old route cannot corroborate.
    now += control::negotiationQuietMs - 1; d.tick(now, 5000); CHECK(d.negotiating());
    ++now; d.tick(now, 5000); CHECK(!d.negotiating() && !d.settled());
    d.receive(info(2, identity(1)), now); d.tick(now, 5000);
    now += 200; d.receive(info(2, identity(1)), now); d.tick(now, 5000);
    CHECK(d.identity(identity(1), now)->verified(now) && !d.settled());
    // A late new route resets the stability window, even if it is unbound.
    now += 4000; d.receive(info(2, identity(1)), now); d.receive(info(1, identity(0)), now); d.tick(now, 5000);
    CHECK(d.settleRemaining(now) == control::addressStableMs);
  }
}
void startupQualification() {
  for (const uint8_t count : {uint8_t(2), uint8_t(9)}) for (const bool cutoff : {false, true}) {
    auto cfg = installation(count); cfg.autoResume = false;
    cfg.operating.voltageMask = cfg.operating.currentMask = membersMask(count);
    for (uint8_t i = 0; i < count; ++i) cfg.operating.voltage[i] = 5400, cfg.operating.current[i] = 300;
    Rig r(cfg, true, false, count == 2 && !cutoff ? 0xffffd000U : 100U, true);
    r.reportAc = true; r.temperature = 25;
    r.c.configureStartup(cutoff, true, r.now); r.c.reconcile(false);
    const auto boot = r.now; uint32_t released = cutoff ? 0 : boot;
    r.step(); enableBootSample(r, true);
    size_t checked = 0; uint32_t firstCurrent = 0;
    for (unsigned n = 0; n < 2400 && !firstCurrent; ++n) {
      r.step(25);
      if (cutoff && !released && r.c.chargeAllowed()) released = r.now;
      for (; checked < r.can.sent.size(); ++checked) if (positiveCurrent(r.can.sent[checked])) {
        CHECK(released && uint32_t(r.now - released) >= control::startupQualificationMs);
        CHECK(r.c.discovery().settled() && r.c.startQualified()); firstCurrent = r.now;
      }
      if (!r.c.startQualified()) CHECK(!r.c.psuActive(r.now));
    }
    CHECK(firstCurrent && !r.c.startupRemaining(r.now)); r.finish();
    CHECK(r.c.controlStatus(r.now).activity == Activity::Running);
    for (uint8_t i = 0; i < count; ++i) CHECK(r.current[i] > 290 && r.c.unitControl(i).fault == CommandState::Idle);
  }
  Rig r(authorized(), true, false); r.c.configureStartup(false, true, r.now);
  r.step(6000); CHECK(r.c.controlStatus(r.now).blocker == Issue::AddressSettling);
  UiRig u(r); UiFrame frame; UiView::compose(frame, u.ui, r.c, r.now);
  CHECK(rowText(frame, 1).find("ADDRESS SETTLING") != std::string::npos);
  snapshot("control-address-settling", frame);
  Terminal terminal(r); const auto text = terminal.input("diag startup\n");
  CHECK(text.find("startup-left-ms=") != std::string::npos && text.find("quiet-left-ms=") != std::string::npos);
  r.c.setAutoResume(false); r.step(20000); r.finish(); CHECK(!r.current[0] && !r.current[1]);
}
void negotiationDuringSync() {
  for (const uint8_t count : {uint8_t(2), uint8_t(9)}) for (unsigned stage = 0; stage < 5; ++stage) {
    Rig r(installation(count)); r.temperature = 25;
    CHECK(r.c.set({}, SettingFields::Current, 300) == Result::Ok);
    CHECK(r.c.queue(r.c.preview({}, SettingFields::Both, r.now), false, r.now) == Result::Ok);
    if (stage == 3) r.answerIdentityQueries = false; // Observe the pending verification before its reply.
    CanFrame oldWrite = {}; bool reached = stage == 0;
    for (unsigned n = 0; n < 1200 && !reached; ++n) {
      r.step(25);
      for (size_t i = r.acknowledged; i < r.can.sent.size(); ++i) if (positiveCurrent(r.can.sent[i])) {
        oldWrite = r.can.sent[i]; if (stage == 1) reached = true;
      }
      if (stage == 2 && r.c.syncStatus(r.now).phase == SyncPhase::Waiting) reached = true;
      if (stage == 3) for (uint8_t i = 0; i < count; ++i)
        if (r.c.report().units[i].state == CommandState::VerifyingIdentity) reached = true;
      if (stage == 4 && r.c.syncStatus(r.now).phase == SyncPhase::Releasing) reached = true;
    }
    CHECK(reached && r.c.busy());
    r.can.incoming.clear(); r.can.incoming.push_back(negotiationFrame()); r.step(25, false, false);
    r.answerIdentityQueries = true;
    CHECK(r.c.discovery().negotiating() && !r.c.busy() && !r.c.report().failed);
    const auto sent = r.can.sent.size();
    r.step(4000, false, false); r.can.incoming.push_back(negotiationFrame(2)); r.step(25, false, false);
    r.step(control::negotiationQuietMs - 1, false, false);
    CHECK(r.c.discovery().negotiating() && r.can.sent.size() == sent);
    r.step(1, false, false); CHECK(!r.c.discovery().negotiating());
    // A newly powered lower-ID unit pushes managed addresses up by one.
    for (uint8_t i = 0; i < count; ++i) r.addresses[i] = i + 2;
    if (oldWrite.length) r.can.incoming.push_back(ack(oldWrite)); // Stale old-route ACK is not proof.
    r.step(); r.step();
    for (uint8_t i = 0; i < count; ++i) CHECK(!r.c.currentSynchronized(i));
    r.finish();
    CHECK(!r.c.busy() && !r.c.report().failed && r.c.controlStatus(r.now).activity == Activity::Running);
    for (uint8_t i = 0; i < count; ++i) {
      CHECK(r.c.deviceForSlot(i, r.now)->address == i + 2 && r.current[i] > 290);
      CHECK(r.c.unitControl(i).fault == CommandState::Idle);
    }
  }
}
void negotiationFaultGuards() {
  Rig rejected; rejected.c.setCurrent(0, 1000); rejected.rejectRegister = protocol::OnlineCurrent;
  CHECK(rejected.c.queue(rejected.c.preview({}, SettingFields::Current, rejected.now), false, rejected.now) == Result::Ok);
  rejected.step(5000); CHECK(rejected.c.syncStatus(rejected.now).phase == SyncPhase::Blocked);
  const auto failed = rejected.c.report().failed;
  rejected.can.incoming.push_back(negotiationFrame()); rejected.step(); rejected.rejectRegister = 0xff;
  rejected.step(20000);
  CHECK(rejected.c.syncStatus(rejected.now).phase == SyncPhase::Blocked && (rejected.c.report().failed & failed) == failed);
  for (uint8_t i = 0; i < rejected.c.count(); ++i) if (failed & (1U << i))
    CHECK(rejected.c.unitControl(i).fault != CommandState::Idle);
  rejected.c.setAutoResume(false); rejected.c.reconcile(); rejected.finish();
  CHECK(!rejected.current[0] && !rejected.current[1]);
  Rig off; startSync(off); off.can.incoming.push_back(negotiationFrame()); off.step();
  off.c.setAutoResume(false); off.step(20000); off.finish();
  CHECK(!off.current[0] && !off.current[1] && !off.c.busy());
  // A genuine duplicate identity without announcements still blocks the job.
  Rig conflict; startSync(conflict); conflict.can.incoming.push_back(info(22, identity(1))); conflict.step();
  CHECK(conflict.c.syncStatus(conflict.now).phase == SyncPhase::Blocked);
  Rig missing; startSync(missing); missing.can.incoming.push_back(negotiationFrame()); missing.step();
  missing.live = 1; missing.step(20000);
  CHECK(!missing.current[0] && missing.c.requiredMembers() == 3 && !missing.c.acceptedAbsences());
  CHECK(missing.c.controlStatus(missing.now).blocker == Issue::Missing);
}
