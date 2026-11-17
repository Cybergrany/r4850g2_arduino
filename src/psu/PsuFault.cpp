#include "PsuController.h"
#include <math.h>
namespace psu {
void PsuController::captureControlFault(uint8_t slot, CommandState state, uint8_t reg,
    uint32_t now, FaultCause cause) {
  if (firstFault_.slot) return;
  auto& f = firstFault_; const auto& u = control_[slot];
  f.ms = now; f.slot = slot + 1; f.identity = config_.units[slot].identity;
  f.cause = cause; f.state = state; f.reg = reg; f.phase = u.phase;
  f.phaseMs = uint32_t(now - u.since); f.demand = u.outputDemand;
  f.ratedCurrent = config_.units[slot].ratedCurrent;
  if (config_.operating.currentMask & (1U << slot)) f.authorizedCurrent = config_.operating.current[slot];
  f.source = enable_.source(); f.permit = startup_.permit;
  f.route = routingIssue(slot, now);
  f.flags = (u.zeroConfirmed ? ControlFault::Zero : 0) | (u.standbyConfirmed ? ControlFault::Standby : 0) |
      (u.awakeConfirmed ? ControlFault::Awake : 0) | (enable_.enabled() ? ControlFault::Enabled : 0);
  if (acknowledgedCurrent(slot, f.ackRaw)) f.metrics |= ControlFault::Ack;
  if (controlActive_ && controlSlot_ == slot) {
    f.flags |= ControlFault::Command; if (controlWaiting_) f.flags |= ControlFault::Waiting;
    f.commandReg = controlRegister_; f.commandRaw = controlValue_;
  }
  f.currentAge = f.voltageAge = f.broadcastAge = f.statusAge = UINT32_MAX;
  const auto* d = deviceForSlot(slot, now);
  if (!d) return;
  f.address = d->address;
  const auto& t = d->telemetry;
  if (t.validMask & (1U << protocol::OutputCurrent)) {
    f.amps = t.values[protocol::OutputCurrent]; f.currentAge = uint32_t(now - t.updated[protocol::OutputCurrent]);
    if (isfinite(f.amps)) f.metrics |= ControlFault::Current;
    float value; if (isfinite(f.amps) && freshMetric(slot, protocol::OutputCurrent, value, now)) f.metrics |= ControlFault::CurrentFresh;
  }
  if (t.validMask & (1U << protocol::OutputVoltage)) {
    f.volts = t.values[protocol::OutputVoltage]; f.voltageAge = uint32_t(now - t.updated[protocol::OutputVoltage]);
    if (isfinite(f.volts)) f.metrics |= ControlFault::Voltage;
    float value; if (isfinite(f.volts) && freshMetric(slot, protocol::OutputVoltage, value, now)) f.metrics |= ControlFault::VoltageFresh;
  }
  if (t.broadcastSeen) {
    f.metrics |= ControlFault::Broadcast; if (t.ready) f.metrics |= ControlFault::Ready;
    f.broadcastAge = uint32_t(now - t.lastBroadcast);
    if (f.broadcastAge <= staleMs()) f.flags |= ControlFault::BroadcastFresh;
  }
  if (t.alarmSeen) {
    f.metrics |= ControlFault::Status; f.status = t.alarmBits; f.statusAge = uint32_t(now - t.lastAlarm);
    if (f.statusAge <= staleMs()) f.flags |= ControlFault::StatusFresh;
  }
}
}
