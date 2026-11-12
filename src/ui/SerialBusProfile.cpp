#include "SerialConsole.h"
#if PSU_BUS_PROFILE && PSU_ENABLE_SERIAL
#include "../diagnostics/BusProfileText.h"
namespace psu {
namespace {
void metric(Print& out, const PsuController& c, uint8_t slot, protocol::Metric field, uint32_t now) {
  float value;
  if (c.freshMetric(slot,field,value,now)) out.print(value,2); else out.print('-');
}
}
void SerialConsole::busProfileRow(uint32_t now, bool summary) {
  auto& profile=controller_.busProfile();
  beginOutput();
  if (summary) {
    BusReceiveStats stats; controller_.busReceiveStats(stats);
    printBusSummary(io_,profile,stats,now); profile.summaryPrinted(now);
  } else {
    BusEventRecord event;
    if (profile.pop(event)) printBusEvent(io_,event);
  }
  // Command-driven diagnostics restore the prompt in execute's caller. Automatic
  // rows restore it below, where the empty-input guard bounds it to two bytes.
}
void SerialConsole::busProfileUnit(uint32_t now) {
  if (busProfileUnit_ >= controller_.count()) busProfileUnit_ = 0;
  const uint8_t slot = busProfileUnit_++;
  busProfileUnitAt_ = now;
  const auto& c = controller_.configuration();
  const auto& u = controller_.unitControl(slot);
  const auto* d = controller_.deviceForSlot(slot,now);
  beginOutput();
  // Fixed CSV, one unit per second: bounded evidence even when event rows drop.
  io_.print(F("BUS U,")); io_.print(now); io_.print(','); io_.print(slot+1);
  io_.print(','); io_.print(d ? d->address : 0); io_.print(','); printIdentity(c.units[slot].identity);
  io_.print(','); io_.print(uint8_t(controller_.issue(slot,now)));
  io_.print(','); io_.print(uint8_t(u.phase)); io_.print(','); io_.print(uint8_t(u.fanRequested));
  io_.print(','); io_.print(uint8_t(u.fanAcknowledged)); io_.print(','); io_.print(uint8_t(u.fanReason));
  io_.print(','); if (d && d->telemetry.alarmSeen) io_.print(d->telemetry.alarmBits); else io_.print('-');
  io_.print(','); if (d && d->telemetry.alarmSeen) io_.print(uint32_t(now-d->telemetry.lastAlarm)); else io_.print('-');
  io_.print(','); if (d) io_.print(uint32_t(now-d->lastSeen)); else io_.print('-');
  io_.print(','); if (d && d->telemetry.broadcastSeen) io_.print(uint32_t(now-d->telemetry.lastBroadcast)); else io_.print('-');
  io_.print(','); metric(io_,controller_,slot,protocol::InputTemperature,now);
  io_.print(','); metric(io_,controller_,slot,protocol::OutputTemperature,now);
  io_.print(','); metric(io_,controller_,slot,protocol::OutputVoltage,now);
  io_.print(','); metric(io_,controller_,slot,protocol::OutputCurrent,now);
  io_.print(','); metric(io_,controller_,slot,protocol::InputVoltage,now);
  io_.print(','); if (c.operating.currentMask & (1U<<slot)) io_.print(c.operating.current[slot]); else io_.print('-');
  io_.print(','); io_.print(controller_.effectiveCurrent(slot));
  uint16_t acknowledged;
  io_.print(','); if (controller_.acknowledgedCurrent(slot,acknowledged)) io_.print(acknowledged); else io_.print('-');
  io_.print(','); if (c.operating.voltageMask & (1U<<slot)) io_.print(c.operating.voltage[slot]); else io_.print('-');
  const uint8_t flags = (controller_.enableControl().enabled() ? 1 : 0) | (controller_.chargeAllowed() ? 2 : 0) |
      (controller_.discovery().negotiating() ? 4 : 0) | (u.zeroConfirmed ? 8 : 0) | (u.awakeConfirmed ? 16 : 0) |
      (controller_.voltageSynchronized(slot) ? 32 : 0) | (controller_.currentSynchronized(slot) ? 64 : 0);
  io_.print(','); io_.print(flags);
  io_.println();
}
bool SerialConsole::busProfileOutput(uint32_t now) {
  auto& profile=controller_.busProfile();
  if (!profile.streaming() || length_ || view_!=View::None || io_.free()<busprofile::outputReserve) return false;
  if (profile.summaryDue(now)) { busProfileRow(now,true); prompt(); return true; }
  if (controller_.count() && uint32_t(now-busProfileUnitAt_) >= busprofile::summaryMs) { busProfileUnit(now); prompt(); return true; }
  if (profile.pending()) { busProfileRow(now,false); prompt(); return true; }
  return false;
}
}
#endif
