#include "Psu.h"

namespace psu {
void Psu::reset(const PsuConfig& config) {
  config_ = config;
  telemetry_ = {};
  status_ = {};
}
float Psu::metric(protocol::Metric m) const {
  const float value = telemetry_.values[m];
  return m == protocol::CurrentCapacity ? value * config_.ratedCurrent / 100.0f : value;
}
bool Psu::hasMetric(protocol::Metric m) const { return telemetry_.validMask & (1U << m); }
bool Psu::stale(uint32_t now, uint32_t timeout) const {
  return !telemetry_.seen || uint32_t(now - telemetry_.lastSeen) > timeout;
}
void Psu::receive(const CanFrame& f, uint32_t now) {
  telemetry_.seen = true;
  telemetry_.lastSeen = now;
  if (protocol::isCurrentBroadcast(f)) {
    // Deliberately matches the reference's /20 and nominal 377 ms sample period.
    // This is an estimate; lost frames undercount. It is not an energy meter.
    const uint16_t raw = uint16_t(f.data[6]) << 8 | f.data[7];
    telemetry_.ampHours += (raw / protocol::ahCurrentDivisor) * (protocol::ahSampleSeconds / 3600.0f);
  } else if (protocol::command(f.id) == protocol::dataCommand && f.data[0] == 1) {
    const int8_t m = protocol::metric(f.data[1]);
    if (m < 0) return;
    const uint32_t raw = protocol::readBigEndian(f.data + 4);
    // Temperature is a signed fixed-point value; other telemetry is unsigned.
    telemetry_.values[m] = (m == protocol::InputTemperature || m == protocol::OutputTemperature)
        ? int32_t(raw) / float(protocol::fixedPointScale) : raw / float(protocol::fixedPointScale);
    telemetry_.validMask |= uint16_t(1U << m);
  }
}
}
