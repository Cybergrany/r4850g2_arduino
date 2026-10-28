#include "../util/FlashString.h"
#include "Simulator.h"
#if PSU_UI_DEBUG
#include <string.h>
#include <stdio.h>
namespace psu {
namespace {
Identity simIdentity(uint8_t slot) { return {{0x53, 0x49, 0x4d, 0, 0, uint8_t(slot + 1)}}; }
void bigEndian(uint8_t* p, uint32_t n) { for (uint8_t i = 0; i < 4; ++i) p[i] = uint8_t(n >> (24 - i * 8)); }
}
FlashText scenarioName(DebugScenario s) {
  switch (s) {
    case DebugScenario::Healthy: return UI_TEXT("healthy");
    case DebugScenario::Empty: return UI_TEXT("empty");
    case DebugScenario::Missing: return UI_TEXT("missing");
    case DebugScenario::Stale: return UI_TEXT("stale");
    case DebugScenario::Unbound: return UI_TEXT("unbound");
    case DebugScenario::Conflict: return UI_TEXT("conflict");
    case DebugScenario::Replacement: return UI_TEXT("replacement");
    case DebugScenario::NotReady: return UI_TEXT("not-ready");
    case DebugScenario::VoltageUnsynced: return UI_TEXT("voltage-unsynced");
    case DebugScenario::Reject: return UI_TEXT("reject");
    case DebugScenario::Timeout: return UI_TEXT("timeout");
    case DebugScenario::SaveChanged: return UI_TEXT("save-changed");
    case DebugScenario::BusOffline: return UI_TEXT("bus-offline");
    case DebugScenario::IdentityTimeout: return UI_TEXT("identity-timeout");
    case DebugScenario::Unassigned: return UI_TEXT("unassigned");
    case DebugScenario::IdentityPending: return UI_TEXT("identity-pending");
    case DebugScenario::Transport: return UI_TEXT("transport");
    case DebugScenario::Partial: return UI_TEXT("partial-failure");
    case DebugScenario::SaveFailed: return UI_TEXT("save-failed");
    case DebugScenario::Changed: return UI_TEXT("changed");
    case DebugScenario::Extremes: return UI_TEXT("extremes");
    case DebugScenario::WrongDeployment: return UI_TEXT("wrong-deployment");
    case DebugScenario::ZeroHold: return UI_TEXT("zero-hold");
    case DebugScenario::Hot: return UI_TEXT("fan-hot");
    case DebugScenario::Cool: return UI_TEXT("fan-cool");
    case DebugScenario::SensorFault: return UI_TEXT("fan-sensor-fault");
    case DebugScenario::FanReject: return UI_TEXT("fan-reject");
    case DebugScenario::ZeroReject: return UI_TEXT("zero-reject");
    case DebugScenario::StopUnconfirmed: return UI_TEXT("stop-unconfirmed");
    case DebugScenario::WakeCurrent: return UI_TEXT("wake-current");
    case DebugScenario::BatteryCan: return UI_TEXT("battery-can-no-ac");
    case DebugScenario::ColdStart: return UI_TEXT("cold-start");
    case DebugScenario::SlowStartup: return UI_TEXT("slow-startup");
    case DebugScenario::SourceAbsent: return UI_TEXT("source-absent");
    case DebugScenario::SourceReturn: return UI_TEXT("source-return");
    case DebugScenario::PartialReturn: return UI_TEXT("partial-return");
    case DebugScenario::UnstableSource: return UI_TEXT("source-unstable");
    default: return UI_TEXT("unknown");
  }
}
Configuration debugConfiguration() {
  auto c = defaultConfiguration(); c.count = PSU_MAX_UNITS; c.autoResume = true;
  memset(c.groups, 0, sizeof(c.groups));
  for (uint8_t i = 0; i < c.count; ++i) {
    const uint8_t g = i < 3 ? 0 : i < 5 ? 1 : i < 7 ? 2 : 3;
    PSU_SNPRINTF(c.groups[g].name, sizeof(c.groups[g].name), "GROUP%u", unsigned(g + 1));
    c.groups[g].members |= 1U << i; c.groups[g].current += 1000;
    c.units[i].identity = simIdentity(i); c.units[i].ratedCurrent = 5000;
    c.operating.current[i] = 1000;
  }
  c.operating.voltage = c.voltage; c.operating.voltageAuthorized = true;
  c.operating.voltageMask = c.operating.currentMask = membersMask(c.count);
  return c;
}
void RamStorage::clear() { memset(bytes_, 0xff, sizeof(bytes_)); failWrites = false; }
void SimulatedCan::reset() {
  for (uint8_t i = 0; i < PSU_MAX_UNITS; ++i) { voltage_[i] = 5400; current_[i] = 1000; }
  pending_ = cycling_ = false; slot_ = field_ = budget_ = 0; lastCycle_ = 0;
  standby_ = fanMax_ = wakeSeen_ = 0;
  scenario_ = DebugScenario::Healthy;
  sources_ = batteries_ = 0xff; permit_ = true; now_ = scenarioSince_ = changedSource_ = 0;
  memset(readyAt_, 0, sizeof(readyAt_));
}
void SimulatedCan::sourcePower(uint8_t mask, bool on) {
  const uint8_t changed = on ? mask & uint8_t(~sources_) : mask & sources_;
  if (on) sources_ |= mask; else sources_ &= uint8_t(~mask);
  for (uint8_t i = 0; i < PSU_MAX_UNITS; ++i) if (changed & (1U << i)) {
    current_[i] = 0; voltage_[i] = 5400; // Commissioned offline baseline; live settings reset on AC loss.
    readyAt_[i] = now_ + 8000;
  }
}
void SimulatedCan::batteryPower(uint8_t mask, bool on) {
  if (on) batteries_ |= mask; else batteries_ &= uint8_t(~mask);
}
void SimulatedCan::permit(bool allowed) {
  if (permit_ == allowed) return;
  permit_ = allowed;
  for (uint8_t i = 0; i < PSU_MAX_UNITS; ++i) {
    current_[i] = 0; voltage_[i] = 5400;
    readyAt_[i] = now_ + (scenario_ == DebugScenario::SlowStartup || scenario_ == DebugScenario::ColdStart ? 8000 : 1000);
  }
}
void SimulatedCan::scenario(DebugScenario value) {
  scenario_ = value; scenarioSince_ = changedSource_ = now_;
  if (value == DebugScenario::WakeCurrent) wakeSeen_ = 0;
  if (value == DebugScenario::BatteryCan) sourcePower(0xff, false);
  if (value == DebugScenario::ColdStart || value == DebugScenario::SlowStartup) {
    if (value == DebugScenario::ColdStart) batteryPower(0xff, false);
    sourcePower(0xff, false); sourcePower(0xff, true);
  }
  if (value == DebugScenario::SourceAbsent || value == DebugScenario::SourceReturn ||
      value == DebugScenario::PartialReturn || value == DebugScenario::UnstableSource) sourcePower(0x18, false);
}
bool SimulatedCan::present(uint8_t i) const { return (ac(i) || (batteries_ & (1U << i))) && !(scenario_ == DebugScenario::Missing && i == 1); }
bool SimulatedCan::outputReady(uint8_t i) const {
  return ac(i) && int32_t(now_ - readyAt_[i]) >= 0 && !(standby_ & (1U << i)) && !(scenario_ == DebugScenario::NotReady && i == 1);
}
uint16_t SimulatedCan::measuredCurrent(uint8_t i) const {
  if (!outputReady(i)) return 0;
  if (scenario_ == DebugScenario::WakeCurrent && !current_[i] && (wakeSeen_ & (1U << i))) return 200;
  return scenario_==DebugScenario::Extremes && current_[i] ? 6000 : current_[i]*9UL/10;
}
CanFrame SimulatedCan::identityFrame(uint8_t i) const {
  auto f = protocol::request(i + 1, protocol::infoCommand); f.id &= ~0x80UL; f.data[1] = 2;
  auto id = simIdentity(scenario_ == DebugScenario::Conflict && i == 1 ? 0 : i);
  if (scenario_ == DebugScenario::Replacement && i == 1) id.bytes[4] = 1;
  memcpy(f.data + 2, id.bytes, 6); return f;
}
CanFrame SimulatedCan::metricFrame(uint8_t i, uint8_t m) const {
  static const uint8_t regs[] = {0x70,0x71,0x72,0x73,0x74,0x75,0x76,0x78,0x7f,0x80,0x81,0x82};
  auto f = protocol::request(i + 1); f.id &= ~0x80UL; f.data[0] = 1; f.data[1] = regs[m];
  const uint32_t amps = measuredCurrent(i);
  const uint32_t watts = uint32_t(voltage_[i]) * amps / 10000;
  uint32_t raw = 0;
  switch (m) {
    case protocol::InputPower: raw = watts * 1100; break;
    case protocol::InputFrequency: raw = 50UL * 1024; break;
    case protocol::InputCurrent: raw = watts * 1024 / 210; break;
    case protocol::OutputPower: raw = scenario_ == DebugScenario::Extremes ? 1234567890UL : watts * 1024; break;
    case protocol::Efficiency: raw = 930; break;
    case protocol::OutputVoltage: raw = uint32_t(voltage_[i]) * 1024 / 100; break;
    case protocol::CurrentCapacity: raw = 60UL * 1024; break;
    case protocol::InputVoltage: raw = ac(i) ? 227UL * 1024 : 0; break;
    case protocol::OutputTemperature: raw = (scenario_ == DebugScenario::SensorFault ? 1000UL :
        scenario_ == DebugScenario::Hot ? 60UL : scenario_ == DebugScenario::Cool ? 25UL : 40UL + i * 3) * 1024; break;
    case protocol::InputTemperature: raw = scenario_ == DebugScenario::Extremes ? uint32_t(-12L * 1024) : 30UL * 1024; break;
    default: raw = amps * 1024 / 100; break;
  }
  bigEndian(f.data + 4, raw); return f;
}
CanFrame SimulatedCan::broadcastFrame(uint8_t i) const {
  CanFrame f = {}; f.extended = true; f.length = 8;
  f.id = 0x1000117eUL | uint32_t(i + 1) << 16; f.data[1] = 1;
  f.data[3] = outputReady(i) ? 0 : 1;
  const uint16_t raw = measuredCurrent(i) / 5; f.data[6] = raw >> 8; f.data[7] = raw; return f;
}
bool SimulatedCan::send(const CanFrame& f) {
  const uint8_t a = protocol::address(f.id), cmd = protocol::command(f.id);
  if (cmd == protocol::setCommand && scenario_ == DebugScenario::Transport) return false;
  if (!a || a > PSU_MAX_UNITS || !present(a - 1)) return true;
  if ((scenario_==DebugScenario::IdentityTimeout || scenario_==DebugScenario::IdentityPending) && a==2 && cmd==protocol::infoCommand) return true;
  if (pending_) return false;
  if (cmd == protocol::infoCommand) response_ = identityFrame(a - 1);
  else if (cmd == protocol::setCommand) {
    if (scenario_ == DebugScenario::Timeout && a == 2) return true;
    response_ = f; response_.id &= ~0x80UL;
    const bool reject = scenario_ == DebugScenario::Reject || (scenario_ == DebugScenario::Partial && a == 2) ||
        (scenario_ == DebugScenario::FanReject && f.data[1] == protocol::FanMode) ||
        ((scenario_ == DebugScenario::ZeroReject || scenario_ == DebugScenario::StopUnconfirmed) &&
            f.data[1] == protocol::OnlineCurrent && protocol::readBigEndian(f.data + 4) == 0) ||
        (scenario_ == DebugScenario::StopUnconfirmed && f.data[1] == protocol::Standby);
    response_.data[0] = reject ? 0x21 : 1;
    if (!reject) {
      const uint16_t value = uint16_t(f.data[6]) << 8 | f.data[7];
      if (f.data[1] == protocol::OnlineVoltage) voltage_[a - 1] = uint32_t(value) * 100 / 1024;
      if (f.data[1] == protocol::OnlineCurrent) current_[a - 1] = uint32_t(value) * 5000 / 1024;
      if (f.data[1] == protocol::Standby) {
        if (f.data[3]) standby_ |= 1U << (a - 1);
        else { standby_ &= uint8_t(~(1U << (a - 1))); wakeSeen_ |= 1U << (a - 1); }
      }
      if (f.data[1] == protocol::FanMode) {
        if (f.data[3]) fanMax_ |= 1U << (a - 1); else fanMax_ &= uint8_t(~(1U << (a - 1)));
      }
    }
  } else if (cmd == protocol::descriptionCommand) {
    response_ = f; response_.id &= ~0x80UL; memcpy(response_.data + 2, "SIMPSU", 6);
  } else return true; // Periodic synthetic telemetry supplies DATA replies.
  pending_ = true; return true;
}
void SimulatedCan::service(uint32_t now) {
  now_ = now;
  if ((scenario_ == DebugScenario::SourceReturn || scenario_ == DebugScenario::PartialReturn) && uint32_t(now - scenarioSince_) >= 25000)
    sourcePower(scenario_ == DebugScenario::SourceReturn ? 0x18 : 0x08, true);
  if (scenario_ == DebugScenario::UnstableSource && uint32_t(now - changedSource_) >= 2000) {
    changedSource_ = now; sourcePower(0x18, !(sources_ & 0x18));
  }
  budget_ = 4;
  if (!cycling_ && uint32_t(now - lastCycle_) >= 377) { cycling_ = true; slot_ = field_ = 0; lastCycle_ = now; }
}
bool SimulatedCan::receive(CanFrame& f) {
  if (!budget_) return false;
  --budget_;
  if (pending_) { pending_ = false; f = response_; return true; }
  if (!cycling_) return false;
  while (slot_ < PSU_MAX_UNITS && !present(slot_)) { ++slot_; field_ = 0; }
  if (slot_ == PSU_MAX_UNITS) { cycling_ = false; return false; }
  if (field_==0 && slot_==1 && (scenario_==DebugScenario::IdentityTimeout || scenario_==DebugScenario::IdentityPending)) ++field_;
  if (field_ == 0) f = identityFrame(slot_);
  else if (field_ == 1) f = broadcastFrame(slot_);
  else if (field_ == protocol::MetricCount + 2) {
    f = protocol::request(slot_ + 1); f.id &= ~0x80UL; f.data[0] = 1; f.data[1] = 0x83;
    bigEndian(f.data + 4, ac(slot_) ? 0 : (uint32_t(1) << 29));
  } else f = metricFrame(slot_, field_ - 2);
  if (++field_ == protocol::MetricCount + 3 || (scenario_ == DebugScenario::Stale && slot_ == 1 && field_ == 2)) {
    field_ = 0; ++slot_;
  }
  return true;
}
}
#endif
