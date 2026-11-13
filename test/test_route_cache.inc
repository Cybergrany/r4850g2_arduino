// Cache routing/source validation only; callers still observe current telemetry, identity
// qualification, configuration, duplicate routes and lease expiry.
void routeCacheLifecycle() {
  for (const uint32_t start : {100U, 0xffffff00U}) {
    FakeCan bus; PsuController c(bus); auto cfg = installation(9);
    CHECK(c.configure(cfg) == Result::Ok && c.begin());
    uint32_t now = start;
    const auto deliver = [&](const CanFrame& f) { bus.incoming.push_back(f); c.tick(now); };
    CHECK(!c.deviceForSlot(8, now)); // Cache a miss, then receive in the same ms.
    deliver(info(2, identity(9)));
    CHECK(c.deviceForSlot(8, now) && c.deviceForSlot(8, now)->address == 2);
    CHECK(c.issue(8, now) == Issue::IdentityPending);
    now += 200; deliver(info(2, identity(9)));
    const auto* original = c.deviceForSlot(8, now);
    CHECK(original && original->verified(now));
    // No cached telemetry/protection decision: a new DATA sample is visible
    // through the cached route without an epoch/timestamp change.
    float value;
    deliver(data(2, 0x7f, 30 * 1024));
    CHECK(c.freshMetric(8, protocol::OutputTemperature, value, now) && value == 30);
    deliver(data(2, 0x7f, 76 * 1024));
    CHECK(c.configuration().thermalStop == ThermalStop::Temperature);
    CHECK(c.deviceForSlot(8, now) == original);
    // A second live address is never chosen arbitrarily, even within one ms.
    deliver(info(3, identity(9)));
    CHECK(!c.deviceForSlot(8, now) && c.issue(8, now) == Issue::Conflict);
    CHECK(c.requestPoll(0x100, now) == Result::Blocked);
    deliver(info(3, identity(99)));
    CHECK(c.deviceForSlot(8, now) == original);
    now += 200; deliver(info(3, identity(99)));
    CHECK(c.bind(0, identity(99), 5000, now) == Result::Ok);
    CHECK(c.deviceForSlot(0, now) && c.deviceForSlot(0, now)->address == 3);
    // Same-ms load/rebind clears both positive and missing cache entries.
    cfg = c.configuration(); cfg.units[0].identity = identity(9); cfg.units[8].identity = identity(99);
    CHECK(c.configure(cfg) == Result::Ok);
    CHECK(c.deviceForSlot(0, now)->address == 2 && c.deviceForSlot(8, now)->address == 3);
    CHECK(c.unbind(8) == Result::Ok && !c.deviceForSlot(8, now));
    CHECK(c.issue(8, now) == Issue::Unbound);
    // Queries can happen between ticks. Cached routing cannot extend the lease.
    CHECK(!c.deviceForSlot(0, now + limits::identityLeaseMs + 1));
    now += limits::identityLeaseMs + 1;
    deliver(info(37, identity(9)));
    CHECK(c.deviceForSlot(0, now) && c.deviceForSlot(0, now)->address == 37);
    CHECK(c.issue(0, now) == Issue::IdentityPending);
    CanFrame negotiation = {}; negotiation.extended = true; negotiation.length = 8;
    negotiation.id = 0x1000107e; negotiation.data[0] = 0x30; negotiation.data[1] = 1;
    deliver(negotiation);
    CHECK(!c.deviceForSlot(0, now) && c.issue(0, now) == Issue::IdentityPending);
    deliver(info(37, identity(9))); CHECK(!c.deviceForSlot(0, now));
    now += control::availabilitySettleMs; c.tick(now);
    deliver(info(42, identity(9)));
    CHECK(c.deviceForSlot(0, now) && c.deviceForSlot(0, now)->address == 42);
    CHECK(c.configuration().thermalStop == ThermalStop::Temperature && !c.enableControl().enabled());
    for (const auto& f : bus.writes())
      CHECK(f.data[1] != protocol::OnlineCurrent || !protocol::readBigEndian(f.data + 4));
  }
}

void drawingReceiveService() {
  // An expired source can become fresh again within the same timestamp without
  // changing identity/epoch. Its cache must refresh before processing ACKs.
  FakeCan bus; PsuController c(bus); CHECK(c.configure(installation(1)) == Result::Ok && c.begin());
  for (uint32_t now : {100U, 300U}) { bus.incoming.push_back(info(1, identity(1))); c.tick(now); }
  float value;
  const uint32_t now = 300 + c.staleMs() + 1;
  CHECK(!c.freshMetric(0, protocol::InputTemperature, value, now));
  bus.incoming.push_back(data(1, 0x80, 25 * 1024));
  const auto before = bus.sent.size();
  CHECK(c.serviceReceive(now) == 1 && bus.sent.size() == before);
  CHECK(c.freshMetric(0, protocol::InputTemperature, value, now) && value == 25);
  CHECK(!c.freshMetric(0, protocol::OutputTemperature, value, now));
  bus.incoming.push_back(data(1, 0x80, 26 * 1024));
  CHECK(c.serviceReceive(300 + limits::identityLeaseMs + 1) == 1);
  CHECK(!c.freshMetric(0, protocol::InputTemperature, value, 300 + limits::identityLeaseMs + 1));

  auto cfg = installation(1); draftGroupCurrent(cfg, 0, 2000);
  Rig r(cfg); r.temperature = 30; r.apply(); r.step(1000);
  CHECK(r.c.setCurrent(0, 3000) == Result::Ok);
  CHECK(r.c.queue(r.c.preview(Operation::GroupCurrent, 0, r.now), false, r.now) == Result::Ok);
  for (uint16_t n=0; n<200 && r.c.report().units[0].state!=CommandState::Waiting; ++n) r.step(10, false);
  CHECK(r.c.report().units[0].state == CommandState::Waiting);
  const auto request = r.can.writes().back(); CHECK(request.data[1] == protocol::OnlineCurrent);
  r.can.incoming.clear();
  r.can.incoming.push_back(ack(request)); r.can.incoming.push_back(data(1, 0x80, 71 * 1024));
  const auto sends = r.can.sent.size();
  CHECK(r.c.serviceReceive(++r.now) == 2 && r.can.sent.size() == sends);
  CHECK(r.c.report().succeeded == 1 && r.c.report().units[0].state == CommandState::Success);
  CHECK(r.c.thermalStatus().level == ThermalLevel::Error && r.c.effectiveCurrent(0) == 900);
  CHECK(!r.c.currentSynchronized(0)); // Old higher ACK cannot confirm the reduction.
  for (uint8_t n=0; n<5; ++n) r.can.incoming.push_back(data(1, 0x7f, 76 * 1024));
  CHECK(r.c.serviceReceive(++r.now) == 4 && r.can.incoming.size() == 1);
  CHECK(!r.c.enableControl().enabled() && r.c.configuration().thermalStop == ThermalStop::Temperature);
  CHECK(r.can.sent.size() == sends); // The normal scheduler retains write ownership.
  CHECK(r.c.serviceReceive(r.now) == 1 && r.c.serviceReceive(r.now) == 0);
  r.step(3000); CHECK(r.current[0] == 0);
}

void exactGroupRange() {
  // Check the per-PSU bound against validation,
  // including sparse memberships and every position for the limiting PSU.
  for (uint8_t n=1; n<=PSU_MAX_UNITS; ++n) {
    auto cfg = installation(PSU_MAX_UNITS); cfg.groups[0].members=0; draftGroupCurrent(cfg, 0, 0);
    for (uint8_t k=0; k<n; ++k) cfg.groups[0].members |= 1U << ((2*k)%PSU_MAX_UNITS);
    for (uint8_t slot=0; slot<PSU_MAX_UNITS; ++slot) if (cfg.groups[0].members & (1U<<slot)) {
      auto mixed=cfg; mixed.units[slot].ratedCurrent=1001;
      FakeCan can; PsuController c(can); CHECK(c.configure(mixed)==Result::Ok);
      const auto maximum=c.groupCurrentMaximum(0);
      CHECK(maximum && c.checkCurrent(0,maximum).issue==Issue::None);
      CHECK(c.checkCurrent(0,maximum+1).issue==Issue::Capacity);
    }
  }
}

void idleSerialStatusCadence() {
  Rig r(installation(1), false, false); r.finish(); Terminal t(r);
  CHECK(t.input("alerts every 0\n").find("every 0 s") != std::string::npos); t.drain();
  // Direct command replies stay immediate. Unsolicited status waits at most
  // the presentation interval, regardless of how many idle loops run.
  r.now += console::statusIntervalMs; t.drain(); t.io.output.clear();
  CHECK(r.c.setAutoResume(true) == Result::Ok);
  t.drain(1000); CHECK(t.io.output.find("enable=ON") == std::string::npos);
  r.now += console::statusIntervalMs - 1; t.drain();
  CHECK(t.io.output.find("enable=ON") == std::string::npos);
  ++r.now; t.drain(); CHECK(t.io.output.find("enable=ON") != std::string::npos);
  CHECK(t.input("autoresume off\n").find("OK") != std::string::npos && !r.c.enableControl().enabled());
  // Presentation cadence must also continue across the millis rollover.
  r.now = 0xfffffff0U; t.drain(); t.io.output.clear();
  CHECK(r.c.setAutoResume(true) == Result::Ok);
  r.now += console::statusIntervalMs; t.drain();
  CHECK(t.io.output.find("enable=ON") != std::string::npos);
}
