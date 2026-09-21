#pragma once
#include "../config/ChargerConfig.h"
#include "../protocol/HuaweiProtocol.h"

namespace psu {
enum class CommandState : uint8_t { Idle, Queued, Waiting, Success, Rejected, Timeout, TransportError };
struct Telemetry {
  float values[protocol::MetricCount];
  uint16_t validMask;
  float ampHours; // Session estimate from unsolicited current frames, as in reference.
  uint32_t lastSeen;
  bool seen;
};
struct CommandStatus {
  CommandState state;
  uint8_t reg;
  uint32_t rawValue;
};

class Psu {
 public:
  const PsuConfig& config() const { return config_; }
  const Telemetry& telemetry() const { return telemetry_; }
  const CommandStatus& commandStatus() const { return status_; }
  float metric(protocol::Metric metric) const;
  bool hasMetric(protocol::Metric metric) const;
  bool stale(uint32_t now, uint32_t timeout) const;
 private:
  friend class PsuController;
  void reset(const PsuConfig& config);
  void receive(const CanFrame& frame, uint32_t now);
  PsuConfig config_ = {};
  Telemetry telemetry_ = {};
  CommandStatus status_ = {};
};
}
