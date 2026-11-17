#include "SerialConsole.h"
#if PSU_ENABLE_SERIAL
#include "UiText.h"
namespace psu {
namespace {
const __FlashStringHelper* causeName(FaultCause c) {
  switch (c) {
    case FaultCause::ZeroHoldCurrent: return F("current-exceeded-zero-hold");
    case FaultCause::ZeroObservation: return F("zero-current-observation-timeout");
    case FaultCause::WakeObservation: return F("wake-observation-timeout");
    case FaultCause::StandbyObservation: return F("standby-observation-timeout");
    case FaultCause::OutputReadiness: return F("output-ready-timeout");
    case FaultCause::ReadinessRecovery: return F("readiness-retry-limit");
    case FaultCause::ProtectiveApply: return F("protective-apply-failure");
    default: return F("control-command-failure");
  }
}
void age(Print& io, uint32_t ms) { if (ms == UINT32_MAX) io.print(F("unknown")); else io.print(ms); }
}
void SerialConsole::faultRow(uint8_t row) {
  const auto& f = controller_.firstControlFault();
  if (!f.slot) return;
  if (row == 0) {
    io_.print(F("FAULT SNAPSHOT first-since-boot uptime-ms=")); io_.print(f.ms);
    io_.print(F(" PSU=")); io_.print(f.slot); io_.print(F(" id=")); printIdentity(f.identity);
    io_.print(F(" addr=")); io_.println(f.address);
  } else if (row == 1) {
    io_.print(F(" cause=")); io_.print(causeName(f.cause)); io_.print(F(" state=")); io_.print(stateName(f.state));
    io_.print(F(" reg=0x")); io_.print(f.reg, HEX); io_.print(F(" phase=")); io_.print(phaseName(f.phase));
    io_.print(F(" phase-age-ms=")); io_.println(f.phaseMs);
  } else if (row == 2) {
    io_.print(F(" current-A=")); if (f.metrics & ControlFault::Current) io_.print(f.amps, 4); else io_.print(F("unknown/invalid"));
    io_.print(F(" age-ms=")); age(io_, f.currentAge); io_.print(F(" fresh=")); io_.print(bool(f.metrics & ControlFault::CurrentFresh));
    io_.print(F(" zero-threshold-A=")); io_.print(control::stoppedCurrentAmps, 4);
    io_.print(F(" ack-raw=")); if (f.metrics & ControlFault::Ack) io_.print(f.ackRaw); else io_.print(F("unknown"));
    io_.print(F(" ack-limit-A="));
    if (f.metrics & ControlFault::Ack) io_.print(f.ackRaw * (f.ratedCurrent / 100.0f) / 1024.0f, 4); else io_.print(F("unknown"));
    io_.print(F(" authorized-A="));
    if (f.authorizedCurrent == UINT16_MAX) io_.println(F("none")); else io_.println(f.authorizedCurrent / 100.0f);
  } else if (row == 3) {
    io_.print(F(" voltage-V=")); if (f.metrics & ControlFault::Voltage) io_.print(f.volts, 3); else io_.print(F("unknown/invalid"));
    io_.print(F(" age-ms=")); age(io_, f.voltageAge); io_.print(F(" fresh=")); io_.print(bool(f.metrics & ControlFault::VoltageFresh));
    io_.print(F(" zero/standby/awake-before=")); io_.print(bool(f.flags & ControlFault::Zero)); io_.print('/');
    io_.print(bool(f.flags & ControlFault::Standby)); io_.print('/'); io_.print(bool(f.flags & ControlFault::Awake));
    io_.print(F(" demand="));
    io_.println(f.demand == OutputDemand::Zero ? F("zero") : f.demand == OutputDemand::Starting ? F("starting") :
        f.demand == OutputDemand::Active ? F("active") : f.demand == OutputDemand::Stopping ? F("stopping") : F("unknown"));
  } else if (row == 4) {
    io_.print(F(" broadcast-age-ms=")); age(io_, f.broadcastAge); io_.print(F(" ready="));
    if (f.metrics & ControlFault::Broadcast) io_.print(bool(f.metrics & ControlFault::Ready)); else io_.print(F("unknown"));
    io_.print(F(" fresh=")); io_.print(bool(f.flags & ControlFault::BroadcastFresh));
    io_.print(F(" raw-status=0x")); if (f.metrics & ControlFault::Status) io_.print(f.status, HEX); else io_.print(F("unknown"));
    io_.print(F(" age-ms=")); age(io_, f.statusAge); io_.print(F(" fresh=")); io_.print(bool(f.flags & ControlFault::StatusFresh));
    io_.print(F(" AC=")); io_.println(!(f.flags & ControlFault::StatusFresh) ? F("unknown") :
        f.status & protocol::statusAcAbsent ? F("absent") : F("present"));
  } else {
    io_.print(F(" enable=")); io_.print(bool(f.flags & ControlFault::Enabled)); io_.print(F(" source=")); io_.print(enableSourceName(f.source));
    io_.print(F(" permit-before=")); io_.print(permitName(f.permit)); io_.print(F(" route=")); io_.print(uint8_t(f.route));
    io_.print(F(" pending-control="));
    if (f.flags & ControlFault::Command) { io_.print(f.commandReg); io_.print(':'); io_.print(f.commandRaw); }
    else io_.print(F("none"));
    io_.print(F(" waiting-ACK=")); io_.println(bool(f.flags & ControlFault::Waiting));
    io_.println(F("diag fault repeats this record until reset. ACK=limit acceptance; voltage may be battery-backed."));
  }
}
}
#endif
