#include "Psu.h"
namespace psu {
bool Psu::fresh(uint32_t now, uint32_t timeout) const { return occupied && uint32_t(now - lastSeen) <= timeout; }
bool Psu::verified(uint32_t now) const {
  return identitySamples >= 2 && identified(identity) && !conflict && uint32_t(now - lastIdentity) <= limits::identityLeaseMs;
}
void Psu::receive(const CanFrame& f, uint32_t now) {
  lastSeen = now;
  if (protocol::isCurrentBroadcast(f)) {
    telemetry.broadcastSeen = true; telemetry.lastBroadcast = now;
    // Observed protocol field, not a complete alarm decoder. Raw 0x0183 retained.
    telemetry.ready = f.data[3] == 0;
    const uint16_t raw = uint16_t(f.data[6]) << 8 | f.data[7];
    telemetry.ampHours += raw / protocol::ahCurrentDivisor * protocol::ahSampleSeconds / 3600;
  } else if (protocol::command(f.id) == protocol::dataCommand && f.data[0] == 1) {
    if (f.data[1] == 0x83) { telemetry.alarmBits = protocol::readBigEndian(f.data + 4); telemetry.alarmSeen = true; }
    const int8_t m = protocol::metric(f.data[1]);
    if (m < 0) return;
    const uint32_t raw = protocol::readBigEndian(f.data + 4);
    telemetry.values[m] = (m == protocol::InputTemperature || m == protocol::OutputTemperature)
        ? int32_t(raw) / float(protocol::fixedPointScale) : raw / float(protocol::fixedPointScale);
    telemetry.validMask |= uint16_t(1U << m);
    telemetry.dataSeen = true; telemetry.lastData = now;
  }
}
}
