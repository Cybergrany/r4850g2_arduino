#include "config/ChargerConfig.h"
#include "config/ConsoleConfig.h"
#include "config/Deployment.h"
#include "can/Mcp2515Transmit.h"
#include "psu/PsuController.h"
#include "storage/MemoryManager.h"
#include "ui/SerialConsole.h"
#include "ui/UiView.h"
#include <fstream>
#include <cstdlib>
#include <array>
#include <cmath>
#include <deque>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace psu;
namespace {
unsigned checks = 0;
#define CHECK(condition) do { ++checks; if (!(condition)) throw std::runtime_error( \
    std::string(__FILE__) + ":" + std::to_string(__LINE__) + " " #condition); } while (false)
void close(float a, float b, float tolerance = 0.001f) { CHECK(std::fabs(a - b) <= tolerance); }

struct FakeCan : CanTransport {
  bool initOk = true;
  bool sendOk = true;
  std::vector<CanFrame> sent;
  std::deque<CanFrame> incoming;
  bool begin() override { return initOk; }
  bool send(const CanFrame& frame) override { if (sendOk) sent.push_back(frame); return sendOk; }
  bool receive(CanFrame& frame) override {
    if (incoming.empty()) return false;
    frame = incoming.front(); incoming.pop_front(); return true;
  }
  std::vector<CanFrame> writes() const {
    std::vector<CanFrame> result;
    for (const auto& f : sent) if (protocol::command(f.id) == protocol::setCommand) result.push_back(f);
    return result;
  }
};
struct PowerCut {};
struct FakeEeprom : ByteStorage {
  std::array<uint8_t, 512> bytes;
  int stopAfter = -1;
  int calls = 0;
  uint16_t capacity = 512;
  int ignoredAddress = -1;
  FakeEeprom() { bytes.fill(0xff); }
  uint16_t length() const override { return capacity; }
  uint8_t read(uint16_t i) const override { CHECK(i < capacity && i < bytes.size()); return bytes[i]; }
  void update(uint16_t i, uint8_t v) override {
    CHECK(i < capacity && i < bytes.size());
    if (stopAfter >= 0 && calls++ == stopAfter) throw PowerCut();
    if (i != ignoredAddress) bytes[i] = v;
  }
};
struct FakeSerial : Stream {
  std::deque<char> input;
  std::string output;
  bool observing = true; // UART still transmits with no terminal to capture it.
  size_t write(uint8_t b) override { if (observing) output.push_back(char(b)); return 1; }
  int available() override { return input.size(); }
  int read() override { if (input.empty()) return -1; char c = input.front(); input.pop_front(); return uint8_t(c); }
  void feed(const std::string& s) { input.insert(input.end(), s.begin(), s.end()); }
};
CanFrame data(uint8_t address, uint8_t reg, uint32_t value) {
  CanFrame frame = protocol::request(address);
  frame.id = (frame.id & ~0x80UL) | 1;
  frame.data[0] = 1; frame.data[1] = reg;
  for (uint8_t i = 0; i < 4; ++i) frame.data[4 + i] = uint8_t(value >> (24 - i * 8));
  return frame;
}
CanFrame ack(CanFrame command, bool error = false) {
  command.id &= ~0x80UL;
  command.data[0] = error ? 0x21 : 1;
  return command;
}
// Simulate register state independently of the transmit algorithm, including a
// chip that never clears TXREQ even when asked to abort.
struct FakeRegisters {
  std::array<uint8_t, 128> bytes = {};
  enum Outcome { Success, ArbitrationThenSuccess, Stuck, Error, Aborted, NoCompletion } outcome = Success;
  uint32_t clock = 0;
  unsigned reads = 0, writes = 0, aborts = 0;
  bool requested = false;
  uint32_t now() { return clock++; }
  uint8_t read(uint8_t address) {
    CHECK(++reads < 200); // A regression must fail instead of hanging the suite.
    if (address == mcp2515::txControl && requested) {
      if (outcome == Success || outcome == ArbitrationThenSuccess) {
        bytes[address] = outcome == Success ? 0 : 0x20;
        bytes[mcp2515::interruptFlags] |= mcp2515::txComplete;
      } else if (outcome == Error) bytes[address] |= mcp2515::txError;
      else if (outcome == Aborted) bytes[address] = mcp2515::txAborted;
      else if (outcome == NoCompletion) bytes[address] = 0;
    }
    return bytes[address];
  }
  void write(uint8_t address, uint8_t value) {
    ++writes; bytes[address] = value;
    if (address == mcp2515::txControl) requested = true;
  }
  void modify(uint8_t address, uint8_t mask, uint8_t value) {
    if (address == mcp2515::txControl && mask == mcp2515::txRequest && value == 0) {
      ++aborts;
      if (outcome == Stuck) return;
    }
    bytes[address] = (bytes[address] & ~mask) | (value & mask);
  }
};
void boundedCanTransmit() {
  const auto frame = protocol::setting(2, 3, 512);
  FakeRegisters io;
  io.bytes[mcp2515::interruptFlags] = 0x07; // Stale TX completion + pending RX.
  CHECK(mcp2515::transmit(io, frame, 20));
  CHECK(io.bytes[0x31] == 0x84 && io.bytes[0x32] == 0x0a);
  CHECK(io.bytes[0x33] == 0x80 && io.bytes[0x34] == 0xfe);
  CHECK(io.bytes[0x35] == 8);
  for (unsigned i = 0; i < 8; ++i) CHECK(io.bytes[0x36 + i] == frame.data[i]);
  CHECK(io.bytes[mcp2515::interruptFlags] == 3); // Preserve RX flags.
  io = FakeRegisters(); io.outcome = FakeRegisters::ArbitrationThenSuccess;
  CHECK(mcp2515::transmit(io, frame, 20));
  for (const auto result : {FakeRegisters::Error, FakeRegisters::Aborted, FakeRegisters::NoCompletion}) {
    io = FakeRegisters(); io.outcome = result;
    io.bytes[mcp2515::interruptFlags] = mcp2515::txComplete;
    CHECK(!mcp2515::transmit(io, frame, 20)); // Stale flag cannot imply success.
  }
  for (const uint32_t start : {0U, 0xfffffff8U}) {
    io = FakeRegisters(); io.outcome = FakeRegisters::Stuck; io.clock = start;
    CHECK(!mcp2515::transmit(io, frame, 20));
    CHECK(uint32_t(io.clock - start) == 21 && io.aborts == 1);
    const auto written = io.writes;
    CHECK(!mcp2515::transmit(io, frame, 20));
    CHECK(io.writes == written && io.aborts == 2); // Busy buffer not overwritten.
  }
  for (unsigned variant = 0; variant < 4; ++variant) {
    auto invalid = frame;
    if (variant == 0) invalid.extended = false;
    if (variant == 1) invalid.rtr = true;
    if (variant == 2) invalid.length = 7;
    if (variant == 3) invalid.id = 0x20000000;
    io = FakeRegisters(); CHECK(!mcp2515::transmit(io, invalid, 20));
    CHECK(io.reads == 0 && io.writes == 0);
  }
}
Identity identity(uint8_t n) { return {{0x11, 0x22, 0x33, 0x44, 0x55, n}}; }
CanFrame info(uint8_t address, Identity id) {
  auto f = protocol::request(address, protocol::infoCommand); f.id &= ~0x80UL;
  f.data[1] = 2; std::memcpy(f.data + 2, id.bytes, 6); return f;
}
CanFrame broadcast(uint8_t address, bool ready = true, uint16_t raw = 20) {
  CanFrame f = {}; f.extended = true; f.length = 8;
  f.id = 0x1000117eUL | uint32_t(address) << 16;
  f.data[1] = 1; f.data[3] = ready ? 0 : 1;
  f.data[6] = raw >> 8; f.data[7] = raw; return f;
}
Configuration installation(uint8_t count = 2) {
  auto c = defaultConfiguration(); c.count = count;
  std::strcpy(c.groups[0].name, "GROUP1"); c.groups[0].members = membersMask(count);
  c.groups[0].current = 5500;
  for (uint8_t i = 0; i < count; ++i) c.units[i].identity = identity(i + 1);
  CHECK(validConfig(c)); return c;
}
// Drives actual controller state machines with independent replies/identity and
// telemetry. Each test can withhold ACKs, drop a member, or change an identity.
struct Rig {
  FakeCan can;
  PsuController c;
  uint32_t now = 100;
  uint8_t live;
  uint8_t addresses[PSU_MAX_UNITS] = {};
  bool answerIdentityQueries = true;
  size_t acknowledged = 0;
  explicit Rig(Configuration config = installation(), bool bootResume = false) : c(can), live(membersMask(config.count)) {
    for (uint8_t i = 0; i < config.count; ++i) addresses[i] = i + 1;
    CHECK(c.configure(config, bootResume) == Result::Ok); CHECK(c.begin());
    heartbeat(); c.tick(now);
    now += 200; heartbeat(); c.tick(now);
  }
  void heartbeat() {
    for (uint8_t i = 0; i < c.count(); ++i) if (live & (1U << i)) {
      can.incoming.push_back(info(addresses[i], identity(i + 1)));
      can.incoming.push_back(data(addresses[i], 0x75, 54 * 1024));
      can.incoming.push_back(broadcast(addresses[i]));
    }
  }
  void step(uint32_t ms = 250, bool acknowledge = true, bool refresh = true) {
    if (ms > 1000) {
      while (ms > 1000) { step(1000, acknowledge, refresh); ms -= 1000; }
      step(ms, acknowledge, refresh); return;
    }
    for (; acknowledged < can.sent.size(); ++acknowledged) {
      const auto& f = can.sent[acknowledged];
      if (acknowledge && protocol::command(f.id) == protocol::setCommand) can.incoming.push_back(ack(f));
    }
    now += ms;
    if (refresh) heartbeat();
    const size_t before = can.sent.size();
    c.tick(now);
    // INFO queries have their own immediate replies, independently of setting
    // ACKs. This also exercises the fresh identity guard before every write.
    if (answerIdentityQueries) {
      const size_t after = can.sent.size();
      for (size_t n = before; n < after; ++n) if (protocol::command(can.sent[n].id) == protocol::infoCommand)
        for (uint8_t i = 0; i < c.count(); ++i)
          if ((live & (1U << i)) && protocol::address(can.sent[n].id) == addresses[i])
            can.incoming.push_back(info(addresses[i], identity(i + 1)));
      c.tick(now);
    }
  }
  void finish() {
    for (uint8_t tries = 0; c.busy() && tries < 50; ++tries) step();
    CHECK(!c.busy());
  }
  void apply(Operation operation = Operation::All, int8_t group = -1) {
    const auto p = c.preview(operation, group, now);
    CHECK(p.blocker == Issue::None); CHECK(c.queue(p, false, now) == Result::Ok); finish();
    CHECK(c.report().failed == 0 && c.report().succeeded == p.recipients);
  }
};
void protocolAndConfiguration() {
  CHECK(protocol::request(1).id == 0x108140fe);
  CHECK(protocol::request(127).id == 0x10ff40fe);
  CHECK(protocol::request(1, protocol::infoCommand).id == 0x108150fe);
  CHECK(protocol::request(2, protocol::descriptionCommand).id == 0x1082d2fe);
  CHECK(!protocol::request(1).rtr);
  const auto command = protocol::setting(2, 3, 512);
  CHECK(command.id == 0x108280fe && command.data[6] == 2 && command.data[7] == 0);
  CHECK(protocol::encodeVoltage(5350) == 0xd600);
  CHECK(protocol::encodeCurrent(2500, 5000) == 512);
  CHECK(protocol::encodeCurrent(2500, 7500) == 341);
  CHECK(protocol::encodeCurrent(6000, 5000) == 1228);
  CHECK(protocol::isReply(ack(command)) && !protocol::isReply(command));
  auto invalid = data(1, 0x75, 1024); invalid.length = 7; CHECK(!protocol::isReply(invalid));
  invalid.length = 8; invalid.rtr = true; CHECK(!protocol::isReply(invalid));
  invalid.rtr = false; invalid.extended = false; CHECK(!protocol::isReply(invalid));
  auto c = defaultConfiguration(); CHECK(validConfig(c));
  CHECK(!c.autoResume && !c.operating.voltageAuthorized && !identified(c.units[0].identity));
  CHECK(!groupName("all") && !groupName("VOLtage") && !groupName("1group") && !groupName("TOOLONG99"));
  CHECK(groupName("Group_1") && sameName("Group_1", "GROUP_1"));
  GroupConfig g = {}; std::strcpy(g.name, "TEST"); g.members = 0x85; g.current = 5501;
  CHECK(allocation(g, 0) == 1834 && allocation(g, 2) == 1834 && allocation(g, 7) == 1833);
  CHECK(allocation(g, 1) == 0);
  Rig r;
  CHECK(r.c.groupIndex("group1") == 0);
  CHECK(r.c.setGroup("GROUP2", 2) == Result::Invalid); // Membership must be disjoint.
  CHECK(r.c.setCount(1) == Result::Invalid);
  CHECK(r.c.setCurrent(0, 12001) == Result::Invalid);
  CHECK(r.c.configuration().groups[0].current == 5500); // Atomic, never clamp.
  const auto check = r.c.checkCurrent(0, 12001);
  CHECK(check.issue == Issue::Capacity && check.slot == 0 && check.share == 6001 && check.maximum == 6000);
  CHECK(r.c.setVoltage(6000) == Result::Invalid && r.c.setVoltage(4700, true) == Result::Invalid);
  CHECK(r.c.setVoltage(5600) == Result::Ok);
  CHECK(r.can.writes().empty()); // All configuration edits remain staged.
  Rig retired(installation(4));
  CHECK(retired.c.setGroup("GROUP1", 3) == Result::Ok && retired.c.setCount(2) == Result::Ok);
  CHECK(retired.c.bind(0, identity(3), 5000, retired.now) == Result::Invalid);
  CHECK(retired.c.unbind(2) == Result::Ok); // Release the retained inactive binding explicitly.
  CHECK(retired.c.bind(0, identity(3), 5000, retired.now) == Result::Ok);
  CHECK(retired.c.report().units[0].state == CommandState::Idle && retired.can.writes().empty());
}
void discoveryAndTelemetry() {
  Rig r;
  CHECK(r.c.issue(0, r.now) == Issue::None && r.c.issue(1, r.now) == Issue::None);
  const auto* first = r.c.deviceForSlot(0, r.now);
  CHECK(first && first->verified(r.now));
  for (uint8_t m = 0; m < protocol::MetricCount; ++m) {
    const uint8_t registers[] = {0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x78, 0x7f, 0x80, 0x81, 0x82};
    r.can.incoming.push_back(data(1, registers[m], m == protocol::InputTemperature ? uint32_t(-5120) : 512));
  }
  r.can.incoming.push_back(data(1, 0x83, 0x10000000)); r.c.tick(r.now);
  float v = 0;
  CHECK(r.c.metric(0, protocol::CurrentCapacity, v, r.now)); close(v, 25);
  CHECK(r.c.metric(0, protocol::InputTemperature, v, r.now)); close(v, -5);
  CHECK(r.c.metric(0, protocol::Efficiency, v, r.now)); close(v, .5);
  CHECK(first->telemetry.alarmBits == 0x10000000 && r.c.issue(0, r.now) == Issue::None);
  CHECK(first->telemetry.ampHours > 0); r.c.resetAmpHours(); close(first->telemetry.ampHours, 0);
  r.can.incoming.push_back(broadcast(1, false)); r.c.tick(r.now);
  CHECK(r.c.issue(0, r.now) == Issue::NotReady);
  r.can.incoming.push_back(broadcast(1)); r.c.tick(r.now);
  // An unbound live unit is diagnostic-only: no setting writes.
  r.can.incoming.push_back(info(13, identity(13))); r.c.tick(r.now);
  r.now += 200; r.can.incoming.push_back(info(13, identity(13))); r.c.tick(r.now);
  CHECK(r.can.writes().empty());
  // Duplicate stable identity at two recently live addresses blocks routing.
  r.can.incoming.push_back(info(14, identity(1))); r.c.tick(r.now);
  CHECK(r.c.issue(0, r.now) == Issue::Conflict && !r.c.deviceForSlot(0, r.now));
  CHECK(r.c.preview(Operation::All, -1, r.now).blocker == Issue::Conflict);
  auto partial = r.c.preview(Operation::GroupCurrent, 0, r.now, true);
  CHECK(partial.issues[0] == Issue::Conflict);

  Discovery d;
  d.receive(info(1, identity(1)), 100); d.tick(100, 5000);
  CHECK(!d.address(1)->verified(100));
  d.receive(info(1, identity(1)), 150); CHECK(!d.address(1)->verified(150));
  d.receive(info(1, identity(1)), 300); CHECK(d.address(1)->verified(300));
  d.receive(info(1, Identity{}), 400); CHECK(!d.address(1)->verified(400));
  auto malformed = info(2, identity(2)); malformed.rtr = true; d.receive(malformed, 500);
  CHECK(d.address(2) == nullptr);
  d.tick(20000, 5000); CHECK(!d.address(1)->live);
  d.receive(info(7, identity(1)), 20000); d.tick(20000, 5000);
  d.receive(info(7, identity(1)), 20200); d.tick(20200, 5000);
  CHECK(d.identity(identity(1), 20200)->address == 7);
}
void orderedApplyAndDraftIsolation() {
  Rig r;
  CHECK(r.c.preview(Operation::GroupCurrent, 0, r.now).blocker == Issue::VoltageUnsynced);
  r.apply();
  const auto w = r.can.writes(); CHECK(w.size() == 4);
  CHECK(w[0].data[1] == protocol::OnlineVoltage && w[1].data[1] == protocol::OnlineVoltage);
  CHECK(w[2].data[1] == protocol::OnlineCurrent && w[3].data[1] == protocol::OnlineCurrent);
  CHECK(protocol::readBigEndian(w[2].data + 4) == protocol::encodeCurrent(2750, 5000));
  CHECK(r.c.configuration().operating.current[0] == 2750 && r.c.configuration().operating.currentMask == 3);
  CHECK(r.c.voltageSynchronized(0) && r.c.voltageSynchronized(1));
  CHECK(r.c.setVoltage(5650) == Result::Ok && r.c.setCurrent(0, 6000) == Result::Ok);
  const auto p = r.c.preview(Operation::GroupCurrent, 0, r.now);
  CHECK(p.voltage == 5400 && p.current[0] == 3000); // Unapplied voltage remains a draft.
  r.apply(Operation::GroupCurrent, 0);
  CHECK(r.can.writes().size() == 6 && r.can.writes()[4].data[1] == protocol::OnlineCurrent);
  CHECK(r.c.configuration().voltage == 5650 && r.c.configuration().operating.voltage == 5400);
  CHECK(r.c.setCurrent(0, 2000, true) == Result::Ok);
  r.apply(Operation::Offline);
  const auto offline = r.can.writes(); CHECK(offline.size() == 10);
  CHECK(offline[6].data[1] == protocol::OfflineVoltage && offline[8].data[1] == protocol::OfflineCurrent);
  CHECK(r.c.configuration().operating.current[0] == 3000); // Offline defaults never replace online authorization.
}
void missingMembersAndConfirmations() {
  Rig r; r.apply(); r.live = 1; r.step(6000);
  CHECK(r.c.issue(1, r.now) == Issue::Missing);
  const auto written = r.can.writes().size();
  auto p = r.c.preview(Operation::GroupCurrent, 0, r.now);
  CHECK(p.blocker == Issue::Missing && r.c.queue(p, false, r.now) == Result::Blocked);
  CHECK(r.can.writes().size() == written);
  p = r.c.preview(Operation::GroupCurrent, 0, r.now, true);
  CHECK(p.blocker == Issue::None && p.recipients == 1 && p.missing == 2);
  CHECK(p.current[0] == 2750 && p.current[1] == 2750);
  CHECK(r.c.queue(p, false, r.now) == Result::ConfirmRequired);
  CHECK(r.c.queue(p, true, r.now + limits::confirmationMs + 1) == Result::Changed);
  CHECK(r.c.setCurrent(0, 6000) == Result::Ok);
  CHECK(r.c.queue(p, true, r.now) == Result::Changed); // Stale confirmation cannot apply new drafts.
  p = r.c.preview(Operation::GroupCurrent, 0, r.now, true);
  CHECK(r.c.queue(p, true, r.now) == Result::Ok); r.finish();
  CHECK(r.c.report().succeeded == 1 && r.c.report().skipped == 2);
  CHECK(r.can.writes().size() == written + 1);
  CHECK(protocol::address(r.can.writes().back().id) == 1);
  CHECK(protocol::readBigEndian(r.can.writes().back().data + 4) == protocol::encodeCurrent(3000, 5000));
  CHECK(r.c.configuration().operating.current[1] == 2750); // Missing unit not silently reauthorized.
  p = r.c.preview(Operation::Voltage, -1, r.now, true);
  CHECK(p.blocker != Issue::None && r.c.queue(p, true, r.now) == Result::Blocked);
  r.live = 0; r.step(6000);
  CHECK(r.c.preview(Operation::GroupCurrent, 0, r.now, true).blocker == Issue::Missing);

  Rig returned; returned.apply(); returned.live = 1; returned.step(6000);
  p = returned.c.preview(Operation::GroupCurrent, 0, returned.now, true);
  returned.live = 3; returned.step(200); returned.step(200);
  CHECK(returned.c.queue(p, true, returned.now) != Result::Ok); // Topology changed; recovery may be busy.

  Rig blocked; blocked.apply();
  blocked.can.incoming.push_back(broadcast(2, false)); blocked.c.tick(blocked.now);
  p = blocked.c.preview(Operation::GroupCurrent, 0, blocked.now, true);
  CHECK(p.blocker == Issue::NotReady && blocked.c.queue(p, true, blocked.now) == Result::Blocked);
}
void acknowledgementsAndFailures() {
  // Losing an already-ACKed voltage recipient before the phase boundary must
  // block every current write. Its genuine return permits one recovery attempt.
  Rig phase;
  CHECK(phase.c.queue(phase.c.preview(Operation::All, -1, phase.now), false, phase.now) == Result::Ok);
  phase.step(); phase.step(); CHECK(phase.can.writes().size() == 2);
  phase.can.incoming.push_back(broadcast(1, false));
  phase.can.incoming.push_back(ack(phase.can.writes().back())); phase.c.tick(phase.now + 1);
  CHECK(!phase.c.busy() && phase.c.report().failed == 1 && phase.c.report().skipped == 2);
  CHECK(phase.c.report().units[1].state == CommandState::Incomplete && phase.can.writes().size() == 2);
  phase.acknowledged = phase.can.sent.size(); phase.step(); CHECK(phase.c.busy()); phase.finish();
  CHECK(phase.c.report().operation == Operation::Restore && phase.c.report().succeeded == 1);
  Rig r;
  CHECK(r.c.queue(r.c.preview(Operation::All, -1, r.now), false, r.now) == Result::Ok);
  r.step(250, false); CHECK(r.can.writes().size() == 1);
  const auto sent = r.can.writes().front();
  auto wrong = ack(sent); wrong.data[1] = protocol::OnlineCurrent; r.can.incoming.push_back(wrong);
  wrong = ack(sent); wrong.id ^= 3UL << 16; r.can.incoming.push_back(wrong);
  wrong = ack(sent); wrong.data[7] ^= 1; r.can.incoming.push_back(wrong);
  r.c.tick(r.now + 1); CHECK(r.c.report().units[0].state == CommandState::Waiting);
  r.can.incoming.push_back(ack(sent, true)); r.c.tick(r.now + 2);
  CHECK(r.c.report().units[0].state == CommandState::Rejected);
  r.acknowledged = r.can.sent.size(); r.finish();
  CHECK(r.c.report().failed == 1 && r.c.report().skipped == 2);
  CHECK(r.can.writes().size() == 2); // No current writes after a failed global voltage phase.
  r.step(1000); r.step(1000); CHECK(r.can.writes().size() == 2); // No repeated automatic fault clearing.
  CHECK(r.c.preview(Operation::GroupCurrent, 0, r.now).blocker == Issue::VoltageUnsynced);

  auto config = installation(); config.groups[0].members = 1;
  std::strcpy(config.groups[1].name, "GROUP2"); config.groups[1].members = 2;
  Rig groups(config); groups.apply();
  CHECK(groups.c.queue(groups.c.preview(Operation::GroupCurrent, 0, groups.now), false, groups.now) == Result::Ok);
  groups.step(250, false);
  groups.can.incoming.push_back(ack(groups.can.writes().back(), true)); groups.c.tick(groups.now + 1);
  groups.acknowledged = groups.can.sent.size(); groups.finish();
  groups.apply(Operation::GroupCurrent, 1);
  CHECK(groups.c.report().units[0].state == CommandState::Rejected); // Another group cannot erase the error.

  Rig timeout;
  CHECK(timeout.c.queue(timeout.c.preview(Operation::Voltage, -1, timeout.now), false, timeout.now) == Result::Ok);
  timeout.step(250, false); timeout.step(750, false);
  CHECK(timeout.c.report().units[0].state == CommandState::Timeout);
  timeout.finish(); CHECK(timeout.c.report().failed == 1 && timeout.c.report().succeeded == 2);

  Rig replaced;
  CHECK(replaced.c.queue(replaced.c.preview(Operation::All, -1, replaced.now), false, replaced.now) == Result::Ok);
  replaced.step(250, false);
  replaced.can.incoming.push_back(info(1, identity(99)));
  replaced.can.incoming.push_back(ack(replaced.can.writes().front())); replaced.c.tick(replaced.now + 1);
  CHECK(replaced.c.report().units[0].state == CommandState::Changed);
  CHECK(replaced.c.issue(0, replaced.now + 1) == Issue::Missing);

  Rig transport;
  CHECK(transport.c.queue(transport.c.preview(Operation::Voltage, -1, transport.now), false, transport.now) == Result::Ok);
  transport.can.sendOk = false; transport.step(); transport.step(); transport.step();
  CHECK(!transport.c.busy() && transport.c.report().failed == 3 && transport.c.txFailures() > 0);
}
void restorationAndDeployment() {
  Rig r; r.apply();
  CHECK(r.c.setCurrent(0, 2000) == Result::Ok && r.c.setVoltage(5800) == Result::Ok);
  const auto written = r.can.writes().size();
  r.live = 1; r.step(16000); CHECK(r.c.issue(1, r.now) == Issue::Missing);
  r.addresses[1] = 17; r.live = 3; r.step(250); // One identity sample cannot restore.
  CHECK(r.can.writes().size() == written);
  r.step(250); r.finish();
  const auto restored = r.can.writes(); CHECK(restored.size() == written + 2);
  CHECK(protocol::address(restored[written].id) == 17);
  CHECK(protocol::readBigEndian(restored[written].data + 4) == protocol::encodeVoltage(5400));
  CHECK(protocol::readBigEndian(restored[written + 1].data + 4) == protocol::encodeCurrent(2750, 5000));
  CHECK(r.c.configuration().groups[0].current == 2000 && r.c.configuration().voltage == 5800);
  auto saved = r.c.configuration();
  Rig noResume(saved, true); noResume.step(); CHECK(noResume.can.writes().empty());
  saved.autoResume = true;
  Rig reboot(saved, true); reboot.finish();
  const auto rebootWrites = reboot.can.writes(); CHECK(rebootWrites.size() == 4);
  CHECK(rebootWrites[0].data[1] == protocol::OnlineVoltage && rebootWrites[1].data[1] == protocol::OnlineVoltage);
  CHECK(rebootWrites[2].data[1] == protocol::OnlineCurrent);
  CHECK(protocol::readBigEndian(rebootWrites[2].data + 4) == protocol::encodeCurrent(2750, 5000));
  Rig loaded(saved, false); loaded.step(); CHECK(loaded.can.writes().empty());
  // Rebinding and saving cannot inherit the old slot's boot authorization,
  // even if all identities are currently present and verified.
  Rig rebound(saved); CHECK(rebound.c.bind(1, identity(2), 5000, rebound.now) == Result::Ok);
  CHECK(rebound.c.configuration().operating.voltageMask == 1 && rebound.c.configuration().operating.currentMask == 1);
  FakeEeprom reboundBytes; MemoryManager reboundMemory(reboundBytes); Configuration reboundSaved;
  CHECK(reboundMemory.save(rebound.c.configuration()) == StorageResult::Ok);
  CHECK(reboundMemory.load(reboundSaved) == StorageResult::Ok);
  Rig pending(reboundSaved, true); pending.step(); pending.step();
  CHECK(pending.c.issue(0, pending.now) == Issue::None && pending.c.issue(1, pending.now) == Issue::None);
  CHECK(pending.can.writes().empty());
  saved.deploymentId = deployment::id + 1;
  Rig foreign(saved, true); foreign.step();
  CHECK(foreign.can.writes().empty() && foreign.c.issue(0, foreign.now) == Issue::DeploymentMismatch);
  CHECK(foreign.c.queue(foreign.c.preview(Operation::All, -1, foreign.now), false, foreign.now) == Result::Blocked);

  // Reassignment preserves requested total but requires an explicit new apply.
  Rig groups; groups.apply(); CHECK(groups.c.setGroup("GROUP1", 1) == Result::Ok);
  CHECK(groups.c.configuration().operating.currentMask == 0);
  CHECK(groups.c.configuration().groups[0].current == 5500);
  CHECK(groups.c.setGroup("GROUP2", 2) == Result::Ok);
  groups.live = 0; groups.step(16000); groups.live = 3; groups.step(200); groups.step(200);
  CHECK(groups.can.writes().size() == 4); // No automatic redistribution after membership edits.
}
void schedulerAndBounds() {
  Rig full(installation(PSU_MAX_UNITS)); full.apply();
  CHECK(full.can.writes().size() == PSU_MAX_UNITS * 2);
  // Cached identity/telemetry alone cannot authorize a setting at a reused address.
  Rig guarded; guarded.answerIdentityQueries = false;
  CHECK(guarded.c.queue(guarded.c.preview(Operation::Voltage, -1, guarded.now), false, guarded.now) == Result::Ok);
  guarded.step(250, false, false);
  CHECK(guarded.c.report().units[0].state == CommandState::VerifyingIdentity && guarded.can.writes().empty());
  guarded.step(750, false, false);
  CHECK(guarded.c.report().units[0].state == CommandState::IdentityTimeout && guarded.can.writes().empty());
  Rig swapped; swapped.answerIdentityQueries = false;
  CHECK(swapped.c.queue(swapped.c.preview(Operation::Voltage, -1, swapped.now), false, swapped.now) == Result::Ok);
  swapped.step(250, false, false);
  swapped.can.incoming.push_back(info(1, identity(99))); swapped.c.tick(swapped.now + 1);
  CHECK(swapped.c.report().units[0].state == CommandState::Changed && swapped.can.writes().empty());
  Rig r; CHECK(r.c.setPollInterval(10000) == Result::Ok);
  for (unsigned i = 0; i < 120; ++i) r.step(100);
  unsigned infoRequests = 0, dataRequests = 0;
  for (const auto& f : r.can.sent) if (protocol::address(f.id) <= 2) {
    if (protocol::command(f.id) == protocol::infoCommand) ++infoRequests;
    if (protocol::command(f.id) == protocol::dataCommand) ++dataRequests;
  }
  CHECK(infoRequests >= 4 && dataRequests >= 2 && r.can.writes().empty());
  Discovery d;
  for (uint8_t a = 1; a <= Discovery::capacity + 1; ++a) d.receive(info(a, identity(a)), 200);
  CHECK(d.overflow() == 1);
  d.receive(info(127, identity(127)), 40000); CHECK(d.address(127)); // Stale capacity can be reused.
  // Unsigned freshness and command timing survive the millis() rollover.
  Psu unit = {}; unit.occupied = true; unit.lastSeen = 0xfffffff0U;
  CHECK(unit.fresh(20, 100) && !unit.fresh(200, 100));
}
void put16(uint8_t* p, uint16_t n) { p[0] = n; p[1] = n >> 8; }
void recordCrc(uint8_t* bytes, uint16_t size) {
  uint16_t crc = 0xffff;
  for (uint16_t i = 1; i < size; ++i) if (i != 8 && i != 9) {
    crc ^= uint16_t(bytes[i]) << 8;
    for (uint8_t n = 0; n < 8; ++n) crc = crc & 0x8000 ? uint16_t(crc << 1) ^ 0x1021 : uint16_t(crc << 1);
  }
  put16(bytes + 8, crc);
}
FakeEeprom legacyRecord(bool mixed = false) {
  FakeEeprom bytes;
  const uint16_t size = 20 + 12 * PSU_MAX_UNITS;
  auto* b = bytes.bytes.data(); std::memset(b, 0, size);
  b[0] = 0xa5; b[1] = 1; put16(b + 2, size); b[4] = 42;
  std::memcpy(b + 10, "R48C", 4); b[16] = 2; b[17] = 1; put16(b + 18, 1000);
  for (uint8_t i = 0; i < PSU_MAX_UNITS; ++i) {
    auto* unit = b + 20 + i * 12; unit[0] = i + 1; unit[1] = 1;
    put16(unit + 2, mixed && i == 1 ? 5500 : 5400); put16(unit + 4, 2750);
    put16(unit + 6, 5300); put16(unit + 8, 1000); put16(unit + 10, 5000);
  }
  recordCrc(b, size); return bytes;
}
void eepromAndMigration() {
  CHECK(MemoryManager::recordSize <= 256 && MemoryManager::budget == 512);
  FakeEeprom bytes; MemoryManager memory(bytes);
  auto one = installation(PSU_MAX_UNITS), out = defaultConfiguration();
  one.autoResume = true; one.operating.voltageAuthorized = true; one.operating.voltage = 5300;
  one.operating.currentMask = membersMask(PSU_MAX_UNITS);
  one.operating.voltageMask = membersMask(PSU_MAX_UNITS);
  one.groups[0] = {};
  for (uint8_t i = 0; i < PSU_MAX_UNITS; ++i) {
    one.groups[i].name[0] = 'G'; one.groups[i].name[1] = '1' + i;
    one.groups[i].members = 1U << i; one.groups[i].current = 2000 + i;
    one.groups[i].offlineCurrent = 1000 + i;
    one.operating.current[i] = 500 + i; one.units[i].ratedCurrent = 5000 + 100 * i;
  }
  CHECK(validConfig(one) && memory.load(out) == StorageResult::NoValidRecord);
  for (int cut = 0; cut <= MemoryManager::recordSize + 1; ++cut) {
    bytes.bytes.fill(0xff); bytes.stopAfter = cut; bytes.calls = 0;
    // Power loss destroys the in-RAM incremental-save state as well as execution.
    MemoryManager interrupted(bytes);
    try { interrupted.save(one); } catch (const PowerCut&) {}
    bytes.stopAfter = -1;
    CHECK(memory.load(out) == (cut <= MemoryManager::recordSize ? StorageResult::NoValidRecord : StorageResult::Ok));
  }
  bytes.bytes.fill(0xff); CHECK(memory.save(one) == StorageResult::Ok && memory.load(out) == StorageResult::Ok);
  CHECK(out.deploymentId == one.deploymentId && out.count == PSU_MAX_UNITS && out.autoResume);
  CHECK(out.operating.voltage == 5300 && out.operating.voltageAuthorized && out.operating.currentMask == one.operating.currentMask);
  CHECK(out.operating.voltageMask == one.operating.voltageMask);
  for (uint8_t i = 0; i < PSU_MAX_UNITS; ++i) {
    CHECK(sameIdentity(out.units[i].identity, one.units[i].identity));
    CHECK(out.units[i].ratedCurrent == one.units[i].ratedCurrent);
    CHECK(sameName(out.groups[i].name, one.groups[i].name) && out.groups[i].members == one.groups[i].members);
    CHECK(out.groups[i].current == one.groups[i].current && out.groups[i].offlineCurrent == one.groups[i].offlineCurrent);
    CHECK(out.operating.current[i] == one.operating.current[i]);
  }
  auto two = one; two.voltage = 5500; CHECK(memory.save(two) == StorageResult::Ok);
  CHECK(bytes.bytes[255] == 0xff && bytes.bytes[511] == 0xff);
  const auto original = bytes.bytes;
  auto three = two; three.voltage = 5600;
  for (int cut = 0; cut <= MemoryManager::recordSize + 1; ++cut) {
    bytes.bytes = original; bytes.calls = 0; bytes.stopAfter = cut;
    MemoryManager interrupted(bytes);
    try { interrupted.save(three); } catch (const PowerCut&) {}
    bytes.stopAfter = -1;
    CHECK(memory.load(out) == StorageResult::Ok);
    CHECK(out.voltage == (cut <= MemoryManager::recordSize ? 5500 : 5600));
  }
  bytes.bytes = original; bytes.bytes[256 + 30] ^= 1;
  CHECK(memory.load(out) == StorageResult::Ok && out.voltage == one.voltage);
  bytes.bytes = original; bytes.ignoredAddress = 20;
  CHECK(memory.save(three) == StorageResult::WriteFailed && memory.load(out) == StorageResult::Ok && out.voltage == 5500);
  bytes.ignoredAddress = -1; bytes.bytes = original;
  // A CRC-valid record belonging to another deployment remains inspectable.
  bytes.bytes[256 + 16] ^= 0x40;
  recordCrc(bytes.bytes.data() + 256, MemoryManager::recordSize);
  CHECK(memory.load(out) == StorageResult::WrongDeployment);
  CHECK(out.deploymentId != deployment::id && memory.save(out) == StorageResult::WrongDeployment);
  const auto untouched = bytes.bytes;
  auto bad = one; bad.units[1].identity = bad.units[0].identity;
  CHECK(memory.save(bad) == StorageResult::InvalidConfig && bytes.bytes == untouched);
  bytes.capacity = 511; CHECK(memory.save(one) == StorageResult::TooSmall && memory.load(out) == StorageResult::TooSmall);

  auto legacy = legacyRecord(); MemoryManager old(legacy);
  const auto preserved = legacy.bytes;
  CHECK(old.load(out) == StorageResult::Migrated);
  CHECK(out.voltage == 5400 && out.groups[0].current == 5500 && out.groups[0].offlineCurrent == 2000);
  CHECK(out.groups[0].members == 3 && !out.autoResume && !out.operating.voltageAuthorized);
  CHECK(!identified(out.units[0].identity) && legacy.bytes == preserved);
  LegacyConfiguration details; CHECK(old.legacy(details) && details.units[1].address == 2 && details.applyOnBoot);
  const auto migrated = out;
  for (int cut = 0; cut <= MemoryManager::recordSize + 1; ++cut) {
    legacy.bytes = preserved; legacy.calls = 0; legacy.stopAfter = cut;
    MemoryManager interrupted(legacy);
    try { interrupted.save(migrated); } catch (const PowerCut&) {}
    legacy.stopAfter = -1;
    CHECK(old.load(out) == (cut <= MemoryManager::recordSize ? StorageResult::Migrated : StorageResult::Ok));
  }
  legacy = legacyRecord(true); const auto mixed = legacy.bytes;
  out = installation(); out.voltage = 5700;
  CHECK(old.load(out) == StorageResult::LegacyNeedsReview && out.voltage == 5700 && legacy.bytes == mixed);
  CHECK(old.legacy(details) && details.units[1].voltage == 5500);
}
struct Terminal {
  Rig& rig;
  FakeEeprom bytes;
  MemoryManager memory;
  FakeSerial io;
  SerialConsole console;
  explicit Terminal(Rig& r) : rig(r), memory(bytes), console(io, r.c, memory) {
    console.begin(StorageResult::NoValidRecord); console.finishStartup();
  }
  std::string input(const std::string& text, bool drain = true) {
    io.output.clear(); io.feed(text);
    do { console.tick(rig.now); } while (io.available());
    if (drain) for (unsigned i = 0; i < 130; ++i) console.tick(rig.now);
    return io.output;
  }
};
void serialWorkflow() {
  Rig r; Terminal t(r);
  CHECK(t.input("help diagnostics\n").find("BUS / GROUP DIAGNOSTICS") != std::string::npos);
  CHECK(t.input("diag bus\n").find("responding addresses=2 verified identities=2 broadcasting=2") != std::string::npos);
  CHECK(t.input("diag group GROUP1\n").find("members=1,2 responding=1,2 ready=1,2 missing=-") != std::string::npos);
  CHECK(t.input("set voltage GROUP1 56\n").find("voltage is global") != std::string::npos);
  CHECK(t.input("set current GROUP1 55\n").find("STAGED total A=55.00") != std::string::npos);
  CHECK(t.input("preview all\n").find("share A=27.50") != std::string::npos && r.can.writes().empty());
  CHECK(t.input("apply all\n").find("QUEUED;") != std::string::npos); r.finish(); t.console.tick(r.now);
  CHECK(r.c.report().succeeded == 3);
  CHECK(t.input("set current GROUP1 20\nsave\n").find("EEPROM OK") != std::string::npos);
  Configuration stored; CHECK(t.memory.load(stored) == StorageResult::Ok);
  CHECK(stored.groups[0].current == 2000 && stored.operating.current[0] == 2750);
  CHECK(t.input("telemetry GROUP1\n").find("filtered-current A=N/A") != std::string::npos);
  CHECK(t.input("set current GROUP1 121\n").find("PSU 1 share A=60.50 exceeds configured maximum A=60.00") != std::string::npos);
  for (const auto& command : {"set current GROUP1 nan\n", "set current GROUP1 inf\n", "set current GROUP1 -2\n", "set current GROUP1 2junk\n", "group set BAD 1-99\n", "count 0\n"})
    CHECK(t.input(command).find("ERR") != std::string::npos);
  CHECK(r.c.configuration().groups[0].current == 2000);
  r.live = 1; r.step(6000);
  CHECK(t.input("apply GROUP1\n").find("missing; output state unknown") != std::string::npos);
  CHECK(t.input("apply GROUP1 partial\n").find("ARE YOU SURE?") != std::string::npos);
  CHECK(t.input("\x18" "confirm yes\n").find("no valid confirmation") != std::string::npos);
  CHECK(!r.c.busy());
  t.input("apply GROUP1 partial\n"); t.input("confirm yes\n"); CHECK(r.c.busy()); r.finish();
  CHECK(r.c.report().recipients == 1 && r.c.report().skipped == 2);

  // Commission fresh slots entirely through public serial commands.
  Rig fresh; Terminal setup(fresh); setup.input("defaults\ncount 2\n");
  setup.input("bind 1 112233445501 50\n"); setup.input("bind 2 112233445502 50\n");
  CHECK(identified(fresh.c.configuration().units[1].identity));
  CHECK(setup.input("group set SOURCE_A 1-2\n").find("OK") != std::string::npos);
  setup.input("set current SOURCE_A 55\nset voltage 54\n");
  CHECK(setup.input("apply all\n").find("QUEUED") != std::string::npos); fresh.finish();
  setup.input("autoresume on\nsave\n"); CHECK(setup.memory.load(stored) == StorageResult::Ok && stored.autoResume);
  CHECK(setup.input("bind 1 112233445502 50\n").find("different identity") != std::string::npos);
}
void serialLifecycleAndParity() {
  Rig r; Terminal t(r);
  CHECK(t.input("set current GROUP1 4", false) == "set current GROUP1 4");
  CHECK(r.c.configuration().groups[0].current == 5500);
  CHECK(t.input("\b3", false) == "\b \b3");
  CHECK(t.input("\r").find("OK") != std::string::npos && r.c.configuration().groups[0].current == 300);
  CHECK(t.input("\n").empty()); CHECK(t.input("\n") == "\r\n> ");
  t.input("echo off\r\n"); CHECK(t.input("set\tcurrent GROUP1 4", false).empty());
  t.input("\x7f" "2\n"); CHECK(r.c.configuration().groups[0].current == 200); t.input("echo on\n");
  t.input(std::string(100, 'x')); CHECK(t.input("\n").find("discarded") != std::string::npos);
  CHECK(t.input("hello\n").find("console ready") != std::string::npos);
  t.io.observing = false; t.input("apply all", false);
  r.step(console::inputIdleTimeoutMs); t.console.tick(r.now);
  t.io.observing = true; CHECK(t.input("\n").find("discarded") != std::string::npos && r.can.writes().empty());
  t.input("apply all", false); t.input("\x18\r\n"); CHECK(!r.c.busy());
  t.input("raw on\n"); t.input("set current GROUP1 4", false); t.io.output.clear();
  r.can.incoming.push_back(data(1, 0x75, 55296)); r.c.tick(r.now);
  CHECK(t.io.output.find("\r\n1081407F ") == 0);
  CHECK(t.io.output.find("> set current GROUP1 4") != std::string::npos);
  t.input("\x15" "hello\n");
  t.input("describe 1\n"); t.io.output.clear();
  auto first = protocol::request(1, protocol::descriptionCommand); first.id = 0x1081d27f;
  std::memcpy(first.data + 2, "Huawei", 6); auto last = first; last.id = 0x1081d27e;
  std::memcpy(last.data + 2, "R4850!", 6); r.can.incoming.push_back(first); r.can.incoming.push_back(last); r.c.tick(r.now);
  CHECK(t.io.output == "\r\nHuaweiR4850!\r\n> ");
  t.input("describe 1\n"); t.io.output.clear(); last.data[2] = 27; r.can.incoming.push_back(last); r.c.tick(r.now);
  CHECK(t.io.output.find('?') != std::string::npos && t.io.output.find(char(27)) == std::string::npos);
  t.io.output.clear(); r.can.incoming.push_back(ack(protocol::setting(1, 3, 512))); r.c.tick(r.now);
  CHECK(t.io.output.find("accepted raw=512 A=25.00") != std::string::npos);
  r.can.incoming.push_back(ack(protocol::setting(1, 2, 60 * 1024), true)); r.c.tick(r.now);
  CHECK(t.io.output.find("rejected raw=61440 V=60.00") != std::string::npos);
  // Console reset does not cancel a command already submitted to the backend.
  t.input("apply all\n"); CHECK(r.c.busy()); t.input("\x18"); CHECK(r.c.busy());
  t.io.observing = false; r.finish(); t.console.tick(r.now); t.io.observing = true;
  CHECK(t.input("hello\n").find("console ready") != std::string::npos);
  CHECK(r.c.report().succeeded == 3);
  // Timeouts are wrap-safe, and characters already waiting cannot complete an expired line.
  for (uint32_t start : {0U, 0xfffffff0U}) {
    Rig idle; Terminal terminal(idle); idle.now = start;
    terminal.input("apply all", false); idle.now += console::inputIdleTimeoutMs;
    CHECK(terminal.input("\r\n").find("Input expired") != std::string::npos && idle.can.writes().empty());
  }
}
struct UiRig {
  Rig& r;
  FakeEeprom bytes;
  MemoryManager memory;
  UiModel ui;
  explicit UiRig(Rig& rig) : r(rig), memory(bytes), ui(r.c, memory) { ui.tick(r.now); }
  void turn(int16_t n) { ui.input({n, false, false}, r.now); }
  void click() { ui.input({0, true, false}, r.now); }
  void hold() { ui.input({0, false, true}, r.now); }
  void tick(uint32_t ms = 1, bool refresh = true) { r.step(ms, true, refresh); ui.tick(r.now); }
  void finish() {
    for (unsigned n = 0; ui.working() && n < 2000; ++n) tick(10);
    CHECK(!ui.working());
  }
};
std::string rowText(const UiFrame& f, uint8_t row) {
  std::string text;
  for (uint8_t col = 0; col < ui::columns; ++col) text += f.cell(uint16_t(row) * ui::columns + col).character;
  return text;
}
void snapshot(const char* name, const UiFrame& frame);
void uiWorkflows() {
  Rig r; UiRig u(r);
  CHECK(u.ui.page() == UiPage::Groups && u.ui.group() == 0);
  u.click(); CHECK(u.ui.page() == UiPage::Units); u.turn(1); CHECK(u.ui.unit() == 1);
  u.click(); CHECK(u.ui.page() == UiPage::Detail); u.click(); CHECK(u.ui.unit() == 1);
  u.hold(); CHECK(u.ui.page() == UiPage::Groups); u.click(); CHECK(u.ui.unit() == 1);
  u.hold(); u.hold(); CHECK(u.ui.page() == UiPage::Config && u.ui.field() == UiField::Current);
  const auto old = r.c.configuration().groups[0].current;
  u.click(); u.turn(30000); CHECK(u.ui.editValue() == r.c.groupCurrentMaximum(0));
  CHECK(r.c.configuration().groups[0].current == old && r.can.writes().empty());
  u.turn(-30000); CHECK(u.ui.editValue() == 0);
  u.hold(); CHECK(r.c.configuration().groups[0].current == old && !u.ui.editing());
  // Current before global voltage commissioning produces a useful blocker.
  u.hold(); u.click(); u.click(); CHECK(u.ui.notice() == UiNotice::Blocked && u.ui.blocker() == Issue::VoltageUnsynced);
  CHECK(r.can.writes().empty());
  u.click(); u.turn(1); CHECK(u.ui.field() == UiField::Voltage);
  u.click(); u.turn(1); u.click(); CHECK(u.ui.notice() == UiNotice::Applying);
  u.finish(); CHECK(u.ui.notice() == UiNotice::Saved);
  Configuration saved; CHECK(u.memory.load(saved) == StorageResult::Ok);
  CHECK(saved.voltage == 5410 && saved.operating.voltage == 5410);
  CHECK(saved.operating.currentMask == 0); // Voltage edit cannot apply group drafts.
  for (const auto& frame : r.can.writes()) CHECK(frame.data[1] == protocol::OnlineVoltage);
  u.click(); u.turn(1); u.click(); u.turn(1); u.click();
  CHECK(u.ui.notice() == UiNotice::Applying); u.finish();
  CHECK(u.ui.notice() == UiNotice::Saved && u.memory.load(saved) == StorageResult::Ok);
  CHECK(saved.groups[0].current == old + ui::currentStep);
  CHECK(saved.operating.current[0] + saved.operating.current[1] == old + ui::currentStep);
  CHECK(r.c.currentSynchronized(0) && !pendingSettings(r.c, 3));
  Rig reboot(saved); CHECK(!reboot.c.currentSynchronized(0) && pendingSettings(reboot.c, 3));
  reboot.apply(Operation::Voltage); CHECK(pendingSettings(reboot.c, 3));
  reboot.apply(Operation::GroupCurrent, 0); CHECK(!pendingSettings(reboot.c, 3));
  // Serial/config changes invalidate a local draft before a confirming click.
  u.click(); u.click(); u.turn(3); const auto writes = r.can.writes().size();
  CHECK(r.c.setCurrent(0, 1000) == Result::Ok); u.ui.tick(r.now);
  CHECK(!u.ui.editing() && u.ui.notice() == UiNotice::Changed);
  u.click(); CHECK(r.can.writes().size() == writes && r.c.configuration().groups[0].current == 1000);
  // No groups is a valid commissioning screen; gestures cannot index -1.
  Rig empty; CHECK(empty.c.removeGroup(0) == Result::Ok); UiRig e(empty);
  e.click(); e.hold(); e.turn(300); CHECK(e.ui.group() == -1 && e.ui.page() == UiPage::Groups);
  UiFrame frame; UiView::compose(frame, e.ui, empty.c, empty.now);
  CHECK(rowText(frame, 3).find("No groups") != std::string::npos);
  Rig removed; UiRig deleted(removed); deleted.hold(); deleted.click();
  CHECK(removed.c.removeGroup(0) == Result::Ok); deleted.ui.tick(removed.now);
  CHECK(deleted.ui.notice() == UiNotice::Changed && !deleted.ui.editing());
  deleted.click(); CHECK(deleted.ui.page() == UiPage::Groups && removed.can.writes().empty());
}
void uiPartialAndFailures() {
  Rig r; r.apply(); UiRig u(r);
  r.live = 1; r.step(6000); u.ui.tick(r.now);
  u.hold(); u.click(); u.turn(1); u.click();
  CHECK(u.ui.page() == UiPage::Confirm && !u.ui.confirmingYes());
  UiFrame warning; UiView::compose(warning, u.ui, r.c, r.now); snapshot("confirmation", warning);
  CHECK(rowText(warning, 10).find("Missing PSU: 2") != std::string::npos);
  CHECK(rowText(warning, 12).find('~') == std::string::npos);
  const auto writes = r.can.writes().size(); u.click();
  CHECK(u.ui.notice() == UiNotice::Cancelled && r.can.writes().size() == writes);
  u.click(); u.click(); u.click(); CHECK(u.ui.page() == UiPage::Confirm);
  u.turn(1); CHECK(u.ui.confirmingYes()); u.click(); CHECK(u.ui.notice() == UiNotice::Applying);
  const auto share = allocation(r.c.configuration().groups[0], 0);
  u.finish(); CHECK(u.ui.notice() == UiNotice::PartialSaved);
  CHECK(r.c.report().recipients == 1 && r.c.report().skipped == 2);
  CHECK(r.c.configuration().operating.current[0] == share); // No redistribution.
  Configuration saved; CHECK(u.memory.load(saved) == StorageResult::Ok);
  // The confirmation expires; another click cannot issue its old write plan.
  u.click(); u.click(); u.click(); CHECK(u.ui.page() == UiPage::Confirm);
  const auto before = r.can.writes().size(); r.step(limits::confirmationMs + 1); u.click();
  CHECK(u.ui.notice() == UiNotice::Changed && r.can.writes().size() == before);
  // Global voltage has no missing-member override.
  u.turn(1); u.click(); u.click(); CHECK(u.ui.notice() == UiNotice::Blocked && u.ui.page() != UiPage::Confirm);
  CHECK(r.can.writes().size() == before);
  Rig unready; unready.apply(); UiRig blocked(unready);
  unready.can.incoming.push_back(broadcast(2, false)); unready.c.tick(unready.now);
  blocked.hold(); blocked.click(); blocked.click();
  CHECK(blocked.ui.notice() == UiNotice::Blocked && blocked.ui.blocker() == Issue::NotReady);
  // CAN failure never produces a success banner or an EEPROM record.
  Rig failure; failure.apply(); UiRig bad(failure); failure.can.sendOk = false;
  bad.hold(); bad.click(); bad.turn(1); bad.click(); bad.finish();
  CHECK(bad.ui.notice() == UiNotice::ApplyFailed && bad.memory.load(saved) == StorageResult::NoValidRecord);
  // EEPROM failure is distinguished from already-acknowledged live settings.
  Rig storage; storage.apply(); UiRig broken(storage); broken.bytes.ignoredAddress = 20;
  broken.hold(); broken.click(); broken.turn(1); broken.click(); broken.finish();
  CHECK(broken.ui.notice() == UiNotice::SaveFailed && storage.c.report().failed == 0);
  // An asynchronous save snapshots once, rejects overlapping saves, and writes
  // at most one byte per tick while the main loop remains available.
  FakeEeprom bytes; MemoryManager memory(bytes); auto original = installation();
  CHECK(memory.startSave(original) == StorageResult::Ok && memory.saving());
  CHECK(memory.save(original) == StorageResult::Busy);
  original.voltage = 5500;
  unsigned ticks = 0;
  while (memory.saving()) { const auto prior = bytes.bytes; memory.stepSave(); unsigned changed = 0;
    for (unsigned i = 0; i < prior.size(); ++i) changed += prior[i] != bytes.bytes[i];
    CHECK(changed <= 1 && ++ticks <= MemoryManager::recordSize + 2);
  }
  CHECK(memory.saveResult() == StorageResult::Ok && memory.load(saved) == StorageResult::Ok && saved.voltage == 5400);
  // A serial change during save must not be labelled as the saved configuration.
  Rig concurrent; concurrent.apply(); UiRig changed(concurrent);
  changed.hold(); changed.click(); changed.click();
  for (unsigned i = 0; changed.ui.notice() == UiNotice::Applying && i < 1000; ++i) changed.tick(10);
  CHECK(changed.ui.notice() == UiNotice::Saving);
  CHECK(concurrent.c.setCurrent(0, 1000) == Result::Ok); changed.finish();
  CHECK(changed.ui.notice() == UiNotice::SavedOlder);
  CHECK(changed.memory.load(saved) == StorageResult::Ok && saved.groups[0].current == 5500);
}
void presentationAndSessions() {
  auto cfg = installation(3); cfg.units[1].ratedCurrent = 1001;
  cfg.groups[0].current = 1000; Rig r(cfg);
  const auto maximum = r.c.groupCurrentMaximum(0);
  CHECK(r.c.checkCurrent(0, maximum).issue == Issue::None);
  CHECK(r.c.checkCurrent(0, maximum + 1).issue == Issue::Capacity);
  CHECK(maximum == 3604); // Equal shares and centiamp remainder, not sum of ratings.
  r.can.incoming.push_back(data(1, 0x81, 10 * 1024));
  r.can.incoming.push_back(data(2, 0x81, 20 * 1024));
  r.can.incoming.push_back(data(1, 0x7f, uint32_t(int32_t(-10 * 1024))));
  r.can.incoming.push_back(data(2, 0x7f, uint32_t(int32_t(-5 * 1024)))); r.c.tick(r.now);
  auto amps = summarize(r.c, 7, protocol::OutputCurrent, Aggregate::Sum, r.now);
  CHECK(amps.observed == 2 && amps.expected == 3 && amps.partial()); close(amps.value, 30);
  auto temp = summarize(r.c, 7, protocol::OutputTemperature, Aggregate::Maximum, r.now); close(temp.value, -5);
  r.step(6000); // Other telemetry keeps arriving, but current/temperature do not.
  amps = summarize(r.c, 7, protocol::OutputCurrent, Aggregate::Sum, r.now); CHECK(!amps.valid());
  float old; CHECK(r.c.metric(0, protocol::OutputCurrent, old, r.now)); close(old, 10); // Last-value API preserved.
  const auto accumulated = r.c.sessionAmpHours(0); CHECK(accumulated > 0);
  r.live &= ~1; r.step(31000); close(r.c.sessionAmpHours(0), accumulated);
  r.addresses[0] = 17; r.live |= 1; r.step(250); r.step(250);
  CHECK(r.c.sessionAmpHours(0) > accumulated && r.c.deviceForSlot(0, r.now)->address == 17);
  auto total = sessionTotal(r.c, 7); CHECK(total.observed == 3 && total.value >= accumulated);
  CHECK(r.c.setCurrent(0, 100) == Result::Ok); CHECK(r.c.sessionAmpHours(0) >= accumulated);
  auto replacement = r.c.configuration(); replacement.units[0].identity = identity(99);
  CHECK(r.c.configure(replacement) == Result::Ok); close(r.c.sessionAmpHours(0), 0); CHECK(!r.c.sessionObserved(0));
  r.c.resetAmpHours(); close(sessionTotal(r.c, 7).value, 0);
  // Per-field freshness remains correct across millis rollover.
  Rig wrap; wrap.now = 0xffffff00; wrap.heartbeat(); wrap.c.tick(wrap.now); wrap.now += 200; wrap.heartbeat(); wrap.c.tick(wrap.now);
  wrap.can.incoming.push_back(data(1, 0x81, 1024)); wrap.c.tick(wrap.now);
  CHECK(wrap.c.freshMetric(0, protocol::OutputCurrent, old, wrap.now + 100));
  CHECK(!wrap.c.freshMetric(0, protocol::OutputCurrent, old, wrap.now + 6000));
}
void snapshot(const char* name, const UiFrame& frame) {
  const char* path = std::getenv("PSU_UI_SNAPSHOTS"); if (!path) return;
  std::ofstream file(std::string(path) + "/" + name + ".txt"); CHECK(bool(file));
  for (uint8_t row = 0; row < ui::rows; ++row) file << rowText(frame, row) << '\n';
  for (uint16_t i = 0; i < UiFrame::cells; ++i) file << "0123456789abcdef"[frame.cell(i).style];
  file << '\n';
}
void uiLayouts() {
  char text[9]; formatValue(text, sizeof(text), 123456.7f, "W"); CHECK(std::strlen(text) <= 8 && std::strstr(text, "W"));
  formatValue(text, 7, 123456.7f, "W"); CHECK(std::strstr(text, "kW") && std::strlen(text) <= 6);
  formatValue(text, sizeof(text), -12.34f, "C", 2); CHECK(std::string(text) == "-12.34C");
  formatValue(text, sizeof(text), INFINITY, "W"); CHECK(std::string(text) == "--");
  UiFrame frame; frame.clear(); frame.text(0, 0, 8, "TOO_LONG_NAME"); CHECK(rowText(frame, 0).substr(0, 8) == "TOO_LON~");
  auto cfg = installation(8); for (auto& g : cfg.groups) g = {};
  for (uint8_t i = 0; i < 5; ++i) { std::snprintf(cfg.groups[i].name, sizeof(cfg.groups[i].name), "GROUP%u", unsigned(i + 1));
    cfg.groups[i].members = i < 3 ? 3U << (i * 2) : 1U << (i + 3);
    cfg.groups[i].current = i < 2 ? 2000 : i == 2 ? 0 : 500;
  }
  Rig r(cfg); r.apply(); UiRig u(r);
  for (uint8_t i = 0; i < 8; ++i) {
    const uint16_t amps = i < 2 ? 10 : i == 2 ? 10 : i < 6 ? 0 : 5;
    r.can.incoming.push_back(data(i + 1, 0x81, amps * 1024UL));
    r.can.incoming.push_back(data(i + 1, 0x73, amps * 54UL * 1024));
    r.can.incoming.push_back(data(i + 1, 0x7f, (i < 2 ? 60 : i < 4 ? 55 : 15) * 1024UL));
    r.can.incoming.push_back(data(i + 1, 0x80, 40 * 1024UL));
    r.can.incoming.push_back(data(i + 1, 0x78, 227 * 1024UL));
    r.can.incoming.push_back(data(i + 1, 0x71, 50 * 1024UL));
    r.can.incoming.push_back(data(i + 1, 0x72, amps ? 2560 : 0));
  }
  while (!r.can.incoming.empty()) r.c.tick(r.now);
  r.can.incoming.push_back(broadcast(4, false)); r.c.tick(r.now);
  u.turn(1); UiView::compose(frame, u.ui, r.c, r.now); snapshot("groups", frame);
  CHECK(rowText(frame, 0).find("40.0A") != std::string::npos && rowText(frame, 0).find("2160W") != std::string::npos);
  CHECK(rowText(frame, 2).find("GROUP1") != std::string::npos && rowText(frame, 2).find("GROUP3") != std::string::npos);
  CHECK(rowText(frame, 12).find("ERR 1/2") != std::string::npos);
  u.click(); u.turn(1); UiView::compose(frame, u.ui, r.c, r.now); snapshot("chargers", frame);
  CHECK(rowText(frame, 4).find("40C/55C") != std::string::npos && rowText(frame, 4).find("540W") != std::string::npos);
  CHECK(rowText(frame, 6).substr(0, 2) == "OK" && frame.cell(6 * ui::columns).style == Good);
  CHECK(rowText(frame, 12).find("NOT READY") != std::string::npos);
  u.click(); UiView::compose(frame, u.ui, r.c, r.now); snapshot("details", frame);
  CHECK(rowText(frame, 4).find("NOT READY") != std::string::npos);
  u.hold(); u.turn(2); CHECK(u.ui.groupRank() == 3);
  UiView::compose(frame, u.ui, r.c, r.now); snapshot("groups-page2", frame);
  CHECK(rowText(frame, 2).find("GROUP4") != std::string::npos && rowText(frame, 0).find("2/2") != std::string::npos);
  u.hold(); u.click();
  const uint32_t paintTime = r.now + ((r.now / ui::blinkMs) % 2 ? ui::blinkMs : 0);
  UiView::compose(frame, u.ui, r.c, paintTime); snapshot("config", frame);
  CHECK(rowText(frame, 3).find("ALL GROUPS") != std::string::npos);
  CHECK(rowText(frame, 4).find("54.00V") != std::string::npos && rowText(frame, 5).find("58.50V") != std::string::npos);
}
}
int main() {
  try {
    boundedCanTransmit(); protocolAndConfiguration(); discoveryAndTelemetry(); orderedApplyAndDraftIsolation();
    missingMembersAndConfirmations(); acknowledgementsAndFailures(); restorationAndDeployment(); schedulerAndBounds();
    eepromAndMigration(); serialWorkflow(); serialLifecycleAndParity();
    uiWorkflows(); uiPartialAndFailures(); presentationAndSessions(); uiLayouts();
    std::cout << "PASS: " << checks << " checks (identity/group/recovery/serial and every EEPROM write interruption)\n";
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
