#include "config/ChargerConfig.h"
#include "config/ConsoleConfig.h"
#include "can/Mcp2515Transmit.h"
#include "psu/PsuController.h"
#include "storage/MemoryManager.h"
#include "ui/SerialConsole.h"
#include <array>
#include <cmath>
#include <deque>
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
void serialEditing() {
  FakeCan can; PsuController c(can); CHECK(c.begin());
  FakeEeprom bytes; MemoryManager memory(bytes); FakeSerial io; SerialConsole console(io, c, memory);
  console.begin(StorageResult::NoValidRecord); console.finishStartup();
  CHECK(io.output.find("Startup complete;") != std::string::npos);
  CHECK(io.output.substr(io.output.size() - 2) == "> ");
  auto input = [&](const std::string& text) {
    io.output.clear(); io.feed(text); console.tick(0);
  };
  input("set 1 current 4"); CHECK(io.output == "set 1 current 4");
  CHECK(c.unit(0).config().current == 100); // No execution before Enter.
  input("\b3"); CHECK(io.output == "\b \b3");
  input("\r"); CHECK(io.output == "\r\nOK\r\n> ");
  CHECK(c.unit(0).config().current == 300);
  input("\n"); CHECK(io.output.empty()); // CRLF split across ticks.
  input("\n"); CHECK(io.output == "\r\n> "); // Bare Enter is visible.
  input("echo off\r\n"); CHECK(io.output == "echo off\r\nOK\r\n> ");
  input("set\t1 current 4"); CHECK(io.output.empty());
  input("\x7f" "2\n"); CHECK(io.output == "\r\nOK\r\n> ");
  CHECK(c.unit(0).config().current == 200);
  input("echo on\n"); CHECK(io.output == "\r\nOK\r\n> ");
  input("\b\x7f"); CHECK(io.output.empty());
  input(std::string(40, 'x')); CHECK(io.available() == 8 && io.output.size() == 32);
  io.output.clear(); console.tick(0); CHECK(io.available() == 0 && io.output.size() == 8);
  input(std::string(40, 'x'));
  console.tick(0); input("\n"); CHECK(io.output.find("discarded") != std::string::npos);
  input("count 2\n"); CHECK(c.count() == 2); // Drained and ready after overflow.
}
void protocolAndConfig() {
  CHECK(protocol::request(0).id == 0x108040fe);
  CHECK(protocol::request(1).id == 0x108140fe);
  CHECK(protocol::request(127).id == 0x10ff40fe);
  CHECK(protocol::request(2, protocol::descriptionCommand).id == 0x1082d2fe);
  CHECK(!protocol::request(1).rtr);
  CHECK(protocol::setting(2, 3, 512).id == 0x108280fe);
  const auto command = protocol::setting(2, 3, 512);
  CHECK(command.data[0] == 1 && command.data[1] == 3 && command.data[6] == 2 && command.data[7] == 0);
  CHECK(protocol::encodeVoltage(5350) == 0xd600);
  CHECK(protocol::encodeCurrent(2500, 5000) == 512);
  CHECK(protocol::encodeCurrent(2500, 7500) == 341);
  CHECK(protocol::encodeCurrent(6000, 5000) == 1228);
  CHECK(protocol::encodeCurrent(100, 5000) == 20);
  CHECK(protocol::isReply(ack(command)));
  CHECK(!protocol::isReply(command));
  auto frame = data(1, 0x70, 100); frame.length = 7;
  CHECK(!protocol::isReply(frame)); frame.length = 8; frame.rtr = true;
  CHECK(!protocol::isReply(frame)); frame.rtr = false; frame.extended = false;
  CHECK(!protocol::isReply(frame));
  auto c = defaultConfiguration(); CHECK(validConfig(c));
  CHECK(c.count == 1 && !c.applyOnBoot && c.units[0].voltage == 5520);
  CHECK(!changeParameter(c.units[0], Parameter::Voltage, NAN));
  CHECK(!changeParameter(c.units[0], Parameter::Current, INFINITY));
  CHECK(!changeParameter(c.units[0], Parameter::Current, -1));
  CHECK(!changeParameter(c.units[0], Parameter::OfflineVoltage, 45));
  CHECK(!changeParameter(c.units[0], Parameter::Address, 0));
  CHECK(!changeParameter(c.units[0], Parameter::Address, 1.5));
  CHECK(!changeParameter(c.units[0], Parameter::Enabled, 2));
  CHECK(!changeParameter(c.units[0], Parameter::RatedCurrent, 0));
  CHECK(!changeParameter(c.units[0], Parameter::RatedCurrent, 0.01f));
  CHECK(changeParameter(c.units[0], Parameter::Voltage, 48.1f));
  CHECK(c.units[0].voltage == 4810);
}
void independentTelemetry() {
  FakeCan can; PsuController controller(can); CHECK(controller.begin()); CHECK(controller.setCount(2) == Result::Ok);
  CHECK(controller.modifySingle(1, Parameter::RatedCurrent, 75) == Result::Ok);
  can.incoming.push_back(data(1, 0x75, 51200));
  can.incoming.push_back(data(2, 0x75, 56320));
  can.incoming.push_back(data(1, 0x76, 512));
  can.incoming.push_back(data(2, 0x76, 512));
  can.incoming.push_back(data(2, 0x7f, uint32_t(-5120)));
  can.incoming.push_back(data(1, 0x81, 1024));
  can.incoming.push_back(data(1, 0x82, 2048));
  controller.tick(100);
  close(controller.unit(0).metric(protocol::OutputVoltage), 50);
  close(controller.unit(1).metric(protocol::OutputVoltage), 55);
  close(controller.unit(0).metric(protocol::CurrentCapacity), 25);
  close(controller.unit(1).metric(protocol::CurrentCapacity), 37.5f);
  close(controller.unit(1).metric(protocol::OutputTemperature), -5);
  close(controller.unit(0).metric(protocol::OutputCurrent), 1);
  close(controller.unit(0).metric(protocol::FilteredOutputCurrent), 2);
  CHECK(!controller.unit(0).hasMetric(protocol::InputVoltage));
  CHECK(!controller.unit(0).stale(100, 3000)); CHECK(controller.unit(0).stale(3200, 3000));
  CanFrame malformed = data(1, 0x75, 1024); malformed.length = 7;
  can.incoming.push_back(malformed);
  can.incoming.push_back(data(3, 0x75, 12345));
  can.incoming.push_back(protocol::setting(1, 0, 100));
  controller.tick(150); close(controller.unit(0).metric(protocol::OutputVoltage), 50);
  CHECK(controller.unknownFrames() == 3);
  CanFrame current = {}; current.id = 0x1002117e; current.extended = true; current.length = 8;
  current.data[1] = 1; current.data[6] = 0; current.data[7] = 200;
  can.incoming.push_back(current); controller.tick(200);
  close(controller.unit(1).telemetry().ampHours, 10 * .377f / 3600, .000001f);
  CHECK(controller.unit(0).telemetry().ampHours == 0);
  CHECK(controller.resetAmpHours(1, 2) == Result::Ok);
  CHECK(controller.unit(1).telemetry().ampHours == 0);
  CHECK(controller.modifySingle(1, Parameter::Address, 127) == Result::Ok);
  CHECK(!controller.unit(1).hasMetric(protocol::OutputVoltage));
  CHECK(controller.unit(1).stale(200, 3000));
}
void rangesAndAcknowledgements() {
  FakeCan can; PsuController c(can); CHECK(c.begin()); CHECK(c.setCount(3) == Result::Ok);
  CHECK(c.modifyRange(0, 3, Parameter::Voltage, 54) == Result::Ok);
  CHECK(c.modifySingle(1, Parameter::Voltage, 53) == Result::Ok);
  CHECK(c.unit(0).config().voltage == 5400 && c.unit(1).config().voltage == 5300);
  CHECK(c.modifyRange(0, 3, Parameter::Address, 2) == Result::Invalid);
  CHECK(c.unit(0).config().address == 1 && c.unit(1).config().address == 2);
  CHECK(c.modifyRange(1, 1, Parameter::Current, 3) == Result::Invalid);
  CHECK(c.modifyRange(0, 4, Parameter::Current, 3) == Result::Invalid);
  CHECK(c.modifySingle(1, Parameter::RatedCurrent, 10) == Result::Ok);
  CHECK(c.modifyRange(0, 3, Parameter::Current, 30) == Result::Invalid);
  CHECK(c.unit(0).config().current == 100); // Earlier valid target wasn't changed.
  CHECK(can.sent.empty());
  CHECK(c.modifySingle(2, Parameter::Enabled, 0) == Result::Ok);
  CHECK(c.applyRange(0, 3) == Result::Ok);
  CHECK(c.modifySingle(0, Parameter::Current, 5) == Result::Busy);
  CHECK(c.configure(defaultConfiguration()) == Result::Busy);
  CHECK(c.setCount(1) == Result::Busy);
  c.tick(250); CHECK(can.writes().size() == 1);
  CHECK(can.writes()[0].id == 0x108180fe);
  auto wrong = ack(can.writes()[0]); wrong.id = 0x1082807e;
  can.incoming.push_back(wrong); c.tick(251);
  CHECK(c.unit(0).commandStatus().state == CommandState::Waiting);
  wrong = ack(can.writes()[0]); ++wrong.data[7]; can.incoming.push_back(wrong); c.tick(252);
  CHECK(c.unit(0).commandStatus().state == CommandState::Waiting);
  can.incoming.push_back(ack(can.writes()[0])); c.tick(253);
  c.tick(500); CHECK(can.writes().size() == 2); CHECK(can.writes()[1].data[1] == 3);
  can.incoming.push_back(ack(can.writes()[1])); c.tick(501);
  CHECK(c.unit(0).commandStatus().state == CommandState::Success);
  c.tick(750); CHECK(can.writes().size() == 3 && can.writes()[2].id == 0x108280fe);
  can.incoming.push_back(ack(can.writes()[2], true)); c.tick(751);
  CHECK(!c.busy()); CHECK(c.unit(1).commandStatus().state == CommandState::Rejected);
  CHECK(c.unit(2).commandStatus().state == CommandState::Idle);
  CHECK(c.applySingle(2) == Result::Disabled);
  CHECK(c.applySingle(0) == Result::Ok); c.tick(1000); c.tick(1750);
  CHECK(!c.busy() && c.unit(0).commandStatus().state == CommandState::Timeout);
  can.sendOk = false; CHECK(c.applySingle(0) == Result::Ok); c.tick(2000);
  CHECK(!c.busy() && c.unit(0).commandStatus().state == CommandState::TransportError);
  CHECK(c.txFailures() > 0);
}
void persistenceCommandsAndRollover() {
  FakeCan can; PsuController c(can); CHECK(c.begin());
  CHECK(c.modifySingle(0, Parameter::Voltage, 52) == Result::Ok);
  CHECK(c.modifySingle(0, Parameter::OfflineVoltage, 54) == Result::Ok);
  CHECK(c.applySingle(0, ApplyMode::OnlineAndOffline) == Result::Ok);
  const uint8_t regs[] = {0, 3, 1, 4};
  for (uint8_t i = 0; i < 4; ++i) {
    c.tick((i + 1) * 250);
    const auto sent = can.writes(); CHECK(sent.size() == size_t(i + 1));
    CHECK(sent.back().data[1] == regs[i]);
    can.incoming.push_back(ack(sent.back())); c.tick((i + 1) * 250 + 1);
  }
  CHECK(!c.busy());
  CHECK(protocol::readBigEndian(can.writes()[0].data + 4) == protocol::encodeVoltage(5200));
  CHECK(protocol::readBigEndian(can.writes()[2].data + 4) == protocol::encodeVoltage(5400));
  CHECK(c.applySingle(0) == Result::Ok);
  c.tick(UINT32_MAX - 100); c.tick(649); // 750 ms across millis() rollover.
  CHECK(!c.busy() && c.unit(0).commandStatus().state == CommandState::Timeout);
  FakeCan failed; failed.initOk = false; PsuController offline(failed);
  CHECK(!offline.begin()); CHECK(offline.applySingle(0) == Result::TransportError);
  CHECK(offline.modifySingle(0, Parameter::Voltage, 50) == Result::Ok);
  offline.tick(10000); CHECK(failed.sent.empty());
}
void eepromJournal() {
  FakeEeprom bytes; MemoryManager memory(bytes);
  Configuration one = defaultConfiguration(), out = one;
  CHECK(memory.load(out) == StorageResult::NoValidRecord);
  CHECK(out.units[0].voltage == one.units[0].voltage);
  for (int cut = 0; cut <= MemoryManager::recordSize + 1; ++cut) {
    bytes.bytes.fill(0xff); bytes.stopAfter = cut; bytes.calls = 0;
    try { memory.save(one); } catch (const PowerCut&) {}
    bytes.stopAfter = -1;
    CHECK(memory.load(out) == (cut <= MemoryManager::recordSize
        ? StorageResult::NoValidRecord : StorageResult::Ok));
  }
  bytes.bytes.fill(0xff);
  one.count = PSU_MAX_UNITS; one.applyOnBoot = true; one.pollMs = 3000;
  for (uint8_t i = 0; i < PSU_MAX_UNITS; ++i) {
    one.units[i] = {uint8_t(10 + i), bool(i % 2), uint16_t(5200 + 10 * i),
                    uint16_t(100 + 20 * i), uint16_t(5500 + i),
                    uint16_t(200 + 30 * i), uint16_t(3000 + 100 * i)};
  }
  CHECK(memory.save(one) == StorageResult::Ok);
  CHECK(memory.load(out) == StorageResult::Ok);
  for (uint8_t i = 0; i < PSU_MAX_UNITS; ++i) {
    CHECK(out.units[i].address == one.units[i].address);
    CHECK(out.units[i].enabled == one.units[i].enabled);
    CHECK(out.units[i].voltage == one.units[i].voltage);
    CHECK(out.units[i].current == one.units[i].current);
    CHECK(out.units[i].offlineVoltage == one.units[i].offlineVoltage);
    CHECK(out.units[i].offlineCurrent == one.units[i].offlineCurrent);
    CHECK(out.units[i].ratedCurrent == one.units[i].ratedCurrent);
  }
  Configuration two = one; two.units[0].voltage = 5300;
  CHECK(memory.save(two) == StorageResult::Ok);
  CHECK(memory.load(out) == StorageResult::Ok && out.units[0].voltage == 5300);
  CHECK(out.count == PSU_MAX_UNITS && out.applyOnBoot && out.pollMs == 3000);
  CHECK(bytes.bytes[255] == 0xff && bytes.bytes[511] == 0xff);
  // Interrupt every update in the third save, including invalidation and final commit.
  const auto original = bytes.bytes;
  Configuration three = two; three.units[0].voltage = 5400;
  for (int cut = 0; cut <= MemoryManager::recordSize + 1; ++cut) {
    bytes.bytes = original; bytes.calls = 0; bytes.stopAfter = cut;
    try { memory.save(three); } catch (const PowerCut&) {}
    bytes.stopAfter = -1;
    CHECK(memory.load(out) == StorageResult::Ok);
    CHECK(out.units[0].voltage == (cut <= MemoryManager::recordSize ? 5300 : 5400));
  }
  bytes.bytes = original;
  bytes.bytes[256 + 30] ^= 0x10; // Newest corrupt: recover older complete record.
  CHECK(memory.load(out) == StorageResult::Ok && out.units[0].voltage == one.units[0].voltage);
  bytes.bytes[1] = 9;
  out.units[0].voltage = 5000;
  CHECK(memory.load(out) == StorageResult::NoValidRecord && out.units[0].voltage == 5000);
  bytes.bytes = original;
  // Simulate an EEPROM cell that refuses the changed low byte of voltage.
  bytes.ignoredAddress = 22;
  CHECK(memory.save(three) == StorageResult::WriteFailed);
  CHECK(memory.load(out) == StorageResult::Ok && out.units[0].voltage == 5300);
  bytes.ignoredAddress = -1;
  auto invalid = one; invalid.units[1].address = invalid.units[0].address;
  const auto before = bytes.bytes;
  CHECK(memory.save(invalid) == StorageResult::InvalidConfig && bytes.bytes == before);
  bytes.capacity = 511;
  CHECK(memory.load(out) == StorageResult::TooSmall);
  CHECK(memory.save(one) == StorageResult::TooSmall);
}
void serialWorkflow() {
  FakeCan can; PsuController c(can); CHECK(c.begin());
  FakeEeprom bytes; MemoryManager memory(bytes); FakeSerial io; SerialConsole console(io, c, memory);
  console.begin(StorageResult::NoValidRecord);
  uint32_t now = 0;
  auto command = [&](const std::string& text) {
    io.output.clear(); io.feed(text);
    do { c.tick(now); console.tick(now++); } while (io.available());
  };
  command("count 3\r\n"); CHECK(c.count() == 3);
  command("set 1-3 current 5\n"); CHECK(c.unit(2).config().current == 500);
  command("set 2 current 2.5\n"); CHECK(c.unit(1).config().current == 250);
  command("set all voltage 54.2\n"); CHECK(c.unit(0).config().voltage == 5420);
  const std::vector<std::string> invalid = {
    "set all current nan\n", "set all current inf\n", "set all current -1\n",
    "set 3-1 current 1\n", "set 0 current 1\n", "set 9999999999 current 1\n",
    "set 2 current 1oops\n", "set 2 enabled 2\n", "set all address 1\n",
    "set 1 current 1 extra\n", "count 256\n", "interval 0\n", "bootapply -1\n"};
  for (const auto& text : invalid) {
    command(text); CHECK(io.output.find("ERR") != std::string::npos);
    CHECK(c.unit(1).config().current == 250);
  }
  command(std::string(90, 'x') + "set all current 60\n");
  CHECK(io.output.find("discarded") != std::string::npos); CHECK(c.unit(0).config().current == 500);
  command("set 2 current 4\b3\n"); CHECK(c.unit(1).config().current == 300);
  command("bootapply 1\nsave\n"); CHECK(can.writes().empty());
  command("defaults\n"); CHECK(c.count() == 1 && !c.applyOnBoot());
  command("load\n"); CHECK(c.count() == 3 && c.applyOnBoot() && c.unit(1).config().current == 300);
  CHECK(can.writes().empty()); // load never silently writes hardware.
  command("set 1 current "); const auto polls = can.sent.size();
  c.tick(1000); console.tick(1000); CHECK(can.sent.size() > polls); // Partial input cannot stop CAN polling.
  command("2\n"); CHECK(c.unit(0).config().current == 200);
  command("apply 1-2\n"); CHECK(c.busy());
  command("defaults\n"); CHECK(c.count() == 3 && io.output.find("progress") != std::string::npos);
  command("help\n");
  for (int i = 0; i < 20; ++i) { c.tick(1100 + i); console.tick(1100 + i); }
  CHECK(io.output.find("Targets:") != std::string::npos);
  CHECK(io.output.find("save / load") != std::string::npos);
}
void serialSessionLifecycle() {
  FakeCan can; PsuController c(can); CHECK(c.begin());
  FakeEeprom bytes; MemoryManager memory(bytes); FakeSerial io;
  io.observing = false;
  SerialConsole terminal(io, c, memory);
  terminal.begin(StorageResult::NoValidRecord); terminal.finishStartup();
  // Boot with no terminal, then run through repeated polls without incoming input.
  for (uint32_t now = 0; now <= 5000; now += 100) { terminal.tick(now); c.tick(now); }
  CHECK(io.output.empty() && can.sent.size() >= 5);
  CHECK(can.writes().empty() && !c.applyOnBoot());
  uint32_t now = 5001;
  auto input = [&](const std::string& text) {
    io.output.clear(); io.feed(text);
    do { terminal.tick(now++); } while (io.available());
  };
  io.observing = true;
  input("hello\r\n");
  CHECK(io.output == "hello\r\nR4850 console ready; help for commands; Ctrl-X resets console\r\n> ");
  CHECK(can.writes().empty());
  input("set 1 voltage 54\n");
  const auto saved = bytes.bytes;
  input("raw on\nwatch on\n");
  input("apply 1"); // Fully formed but unsubmitted command abandoned by client.
  io.observing = false;
  now += console::inputIdleTimeoutMs;
  terminal.tick(now); c.tick(now);
  io.observing = true;
  input("\n"); CHECK(io.output.find("discarded") != std::string::npos);
  CHECK(!c.busy() && can.writes().empty());
  // Reset also works on a quick reconnect before the idle timeout.
  input("apply 1"); input("\x18\r\n");
  CHECK(!c.busy() && can.writes().empty());
  CHECK(io.output.find("Console reset;") != std::string::npos);
  CHECK(c.unit(0).config().voltage == 5400 && bytes.bytes == saved);
  io.output.clear(); can.incoming.push_back(data(1, 0x75, 55296)); c.tick(now);
  terminal.tick(now + 1000); CHECK(io.output.empty()); // raw/watch stopped.
  close(c.unit(0).metric(protocol::OutputVoltage), 54);
  // Ctrl-U recovers an invalid line; Ctrl-C recovers an overlong one.
  input(std::string("apply 1") + char(1)); input("\x15" "config 1\n");
  CHECK(!c.busy()); CHECK(io.output.find("PSU 1 addr=") != std::string::npos);
  input(std::string(100, 'x')); input("\x03");
  CHECK(io.output.find("Console reset;") != std::string::npos);
  input("set 1 current 3\n"); CHECK(c.unit(0).config().current == 300);
  // Console reset must not cancel or alter an already queued PSU job.
  input("apply 1\n"); CHECK(c.busy()); input("\x18"); CHECK(c.busy());
  io.observing = false;
  c.tick(now + 1000); CHECK(can.writes().size() == 1);
  can.incoming.push_back(ack(can.writes().back())); c.tick(now + 1001);
  c.tick(now + 1300); CHECK(can.writes().size() == 2);
  can.incoming.push_back(ack(can.writes().back())); c.tick(now + 1301);
  CHECK(!c.busy() && c.unit(0).commandStatus().state == CommandState::Success);
  io.observing = true;
  input("hello\n"); CHECK(io.output.find("R4850 console ready;") != std::string::npos);
  CHECK(c.unit(0).config().current == 300 && bytes.bytes == saved);
  // A genuine MCU reboot restores EEPROM/defaults, not an old console session.
  FakeCan rebootCan; PsuController rebootController(rebootCan); CHECK(rebootController.begin());
  FakeSerial rebootIo; SerialConsole reboot(rebootIo, rebootController, memory);
  reboot.begin(StorageResult::NoValidRecord); reboot.finishStartup();
  rebootIo.output.clear(); rebootIo.feed("\n"); reboot.tick(0);
  CHECK(rebootIo.output == "\r\n> "); CHECK(rebootCan.writes().empty());
}
void serialIdleAndBackgroundOutput() {
  for (uint32_t start : {0U, 0xfffffff0U}) {
    FakeCan can; PsuController c(can); CHECK(c.begin()); FakeEeprom bytes;
    MemoryManager memory(bytes); FakeSerial io; SerialConsole terminal(io, c, memory);
    terminal.begin(StorageResult::NoValidRecord); terminal.finishStartup();
    io.feed("apply 1"); terminal.tick(start);
    // New bytes are already waiting when the timeout is first serviced.
    io.feed("\r\n"); terminal.tick(start + console::inputIdleTimeoutMs);
    CHECK(!c.busy() && can.writes().empty());
    CHECK(io.output.find("Input expired;") != std::string::npos);
    io.output.clear(); terminal.tick(start + console::inputIdleTimeoutMs + 1);
    CHECK(io.output.empty()); // No repeated warnings or CRLF duplicate prompt.
    io.feed("count 2\n"); terminal.tick(start + console::inputIdleTimeoutMs + 2);
    CHECK(c.count() == 2);
    io.feed("count 3"); terminal.tick(start + console::inputIdleTimeoutMs + 3);
    io.feed("\n"); terminal.tick(start + 2 * console::inputIdleTimeoutMs + 2);
    CHECK(c.count() == 3); // A pause shorter than the timeout remains editable.
  }
  FakeCan can; PsuController c(can); CHECK(c.begin()); FakeEeprom bytes;
  MemoryManager memory(bytes); FakeSerial io; SerialConsole terminal(io, c, memory);
  terminal.begin(StorageResult::NoValidRecord); terminal.finishStartup();
  io.feed("raw on\n"); terminal.tick(0);
  io.feed("set 1 current 4"); terminal.tick(1); io.output.clear();
  can.incoming.push_back(data(1, 0x75, 55296)); c.tick(2);
  CHECK(io.output.find("\r\n1081407F ") == 0);
  CHECK(io.output.substr(io.output.size() - 17) == "> set 1 current 4");
  io.output.clear(); can.incoming.push_back(ack(protocol::setting(1, 3, 512))); c.tick(3);
  CHECK(io.output.find("\r\nACK 1 reg=3 accepted 25.00A\r\n") == 0);
  io.feed("\b3\n"); terminal.tick(4); CHECK(c.unit(0).config().current == 300);
  io.feed("hello\n"); terminal.tick(5); io.output.clear();
  can.incoming.push_back(data(1, 0x75, 55296)); c.tick(6); CHECK(io.output.empty());
  // Untrusted device description characters cannot move the terminal cursor.
  io.feed("describe 1\n"); terminal.tick(7); io.output.clear();
  auto description = protocol::request(1, protocol::descriptionCommand); description.id = 0x1081d27e;
  description.data[2] = 'A'; description.data[3] = 27; description.data[4] = '\r';
  description.data[5] = 0; description.data[6] = 'B'; description.data[7] = 127;
  can.incoming.push_back(description); c.tick(8);
  CHECK(io.output == "\r\nA??B?\r\n> ");
}
void descriptionAndAckOutput() {
  FakeCan can; PsuController c(can); CHECK(c.begin()); FakeEeprom bytes; MemoryManager m(bytes);
  FakeSerial io; SerialConsole console(io, c, m); console.begin(StorageResult::NoValidRecord);
  io.feed("describe 1\n"); console.tick(0);
  CHECK(can.sent.back().id == 0x1081d2fe);
  auto first = protocol::request(1, protocol::descriptionCommand); first.id = 0x1081d27f;
  const char* text = "Huawei"; for (int i = 0; i < 6; ++i) first.data[2 + i] = text[i];
  auto last = first; last.id = 0x1081d27e; text = "R4850!";
  for (int i = 0; i < 6; ++i) last.data[2 + i] = text[i];
  io.output.clear(); can.incoming.push_back(first); can.incoming.push_back(last); c.tick(1);
  CHECK(io.output == "\r\nHuaweiR4850!\r\n> ");
  can.incoming.push_back(ack(protocol::setting(1, 3, 512))); c.tick(2);
  CHECK(io.output.find("25.00A") != std::string::npos);
  can.incoming.push_back(ack(protocol::setting(1, 2, 60 * 1024), true)); c.tick(3);
  CHECK(io.output.find("rejected 60.00V") != std::string::npos);
}
}
int main() {
  try {
    boundedCanTransmit(); serialEditing(); protocolAndConfig(); independentTelemetry(); rangesAndAcknowledgements();
    persistenceCommandsAndRollover(); eepromJournal(); serialWorkflow(); serialSessionLifecycle(); serialIdleAndBackgroundOutput(); descriptionAndAckOutput();
    std::cout << "PASS: " << checks << " checks (including every EEPROM write interruption)\n";
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
