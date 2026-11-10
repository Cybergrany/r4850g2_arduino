// Cache routing only; every caller still observes current telemetry, identity
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
