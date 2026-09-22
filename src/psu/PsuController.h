#pragma once
#include "Discovery.h"
namespace psu {
enum class Result : uint8_t { Ok, Invalid, Busy, TransportError, Blocked, ConfirmRequired, Changed };
enum class Issue : uint8_t {
  None, Missing, Unbound, Unassigned, IdentityPending, Conflict, NoTelemetry,
  NotReady, VoltageUnsynced, DeploymentMismatch, Busy, Invalid, Capacity
};
enum class Operation : uint8_t { All, Voltage, GroupCurrent, Offline, Restore };
struct ApplyPlan {
  Operation operation;
  int8_t group;
  uint8_t members, recipients, missing;
  uint16_t voltage, current[PSU_MAX_UNITS];
  uint8_t addresses[PSU_MAX_UNITS];
  uint32_t epochs[PSU_MAX_UNITS];
  Issue issues[PSU_MAX_UNITS], blocker;
  uint32_t configurationRevision, topologyRevision, created;
  bool partial;
};
struct OperationReport {
  uint32_t sequence;
  Operation operation;
  uint8_t requested, recipients, succeeded, failed, skipped;
  bool active;
  CommandStatus units[PSU_MAX_UNITS];
};
struct GroupStatus {
  uint8_t members, responding, ready, missing;
  uint16_t requestedCurrent, authorizedCurrent;
};
struct BusStatus { uint8_t configured, responding, verified, broadcasting; };
struct CurrentCheck {
  Issue issue;
  uint8_t slot;
  uint16_t share, maximum;
};
class PsuController {
 public:
  explicit PsuController(CanTransport& transport);
  bool begin();
  void tick(uint32_t now);
  const Configuration& configuration() const { return config_; }
  Result configure(const Configuration& config, bool resumeSaved = false);
  Result setCount(uint8_t count);
  Result setGroup(const char* name, uint8_t members);
  Result removeGroup(uint8_t group);
  Result bind(uint8_t slot, const Identity& identity, uint16_t ratedCurrent, uint32_t now);
  // Also permits releasing a retained inactive slot. This does not turn off a PSU.
  Result unbind(uint8_t slot);
  Result setVoltage(uint16_t voltage, bool offline = false);
  Result setCurrent(uint8_t group, uint16_t total, bool offline = false);
  CurrentCheck checkCurrent(uint8_t group, uint16_t total) const;
  // Exact maximum accepted by checkCurrent, including unequal ratings/rounding.
  uint16_t groupCurrentMaximum(uint8_t group) const;
  uint32_t configurationRevision() const { return revision_; }
  Result setAutoResume(bool enabled);
  Result setPollInterval(uint16_t ms);
  int8_t groupIndex(const char* name) const;
  const Psu* deviceForSlot(uint8_t slot, uint32_t now) const;
  Issue issue(uint8_t slot, uint32_t now) const;
  GroupStatus groupStatus(uint8_t group, uint32_t now) const;
  BusStatus busStatus(uint32_t now) const;
  // Returns last measurement, not a freshness guarantee. CurrentCapacity uses
  // the commissioned rating; efficiency remains a fraction (multiply by 100 for %).
  bool metric(uint8_t slot, protocol::Metric metric, float& value, uint32_t now) const;
  bool freshMetric(uint8_t slot, protocol::Metric metric, float& value, uint32_t now) const;
  float sessionAmpHours(uint8_t slot) const { return slot < count() ? sessionAh_[slot] : 0; }
  bool sessionObserved(uint8_t slot) const { return slot < count() && (sessionSeen_ & (1U << slot)); }
  ApplyPlan preview(Operation operation, int8_t group, uint32_t now, bool partial = false) const;
  // Plan expiry + config/topology checks apply to every queue, including confirmations.
  Result queue(const ApplyPlan& plan, bool confirmed, uint32_t now);
  const OperationReport& report() const { return report_; }
  bool voltageSynchronized(uint8_t slot) const { return slot < count() && (voltageSynced_ & (1U << slot)); }
  bool currentSynchronized(uint8_t slot) const { return slot < count() && (currentSynced_ & (1U << slot)); }
  const Discovery& discovery() const { return discovery_; }
  bool busy() const { return report_.active; }
  bool ready() const { return ready_; }
  uint8_t count() const { return config_.count; }
  uint16_t pollInterval() const { return config_.pollMs; }
  uint32_t staleMs() const { return uint32_t(config_.pollMs) * 3 < limits::minimumStaleMs ? limits::minimumStaleMs : uint32_t(config_.pollMs) * 3; }
  uint16_t txFailures() const { return txFailures_; }
  uint8_t droppedFrames() const { return transport_.droppedFrames(); }
  uint8_t receiveHighWater() const { return transport_.receiveHighWater(); }
  uint16_t hardwareOverflows() const { return transport_.hardwareOverflows(); }
  uint16_t readFailures() const { return readFailures_; }
  uint32_t maxLoopUs() const { return maxLoopUs_; }
  void recordLoopTime(uint32_t us) { if (us > maxLoopUs_) maxLoopUs_ = us; }
  // A sweep probes 1..127 with INFO requests; no broadcast/configuration writes.
  void scan() { scanAddress_ = 1; }
  uint8_t scanning() const { return scanAddress_; }
  // Read requests are queued and paced. OK means queued, not a received reply;
  // readFailures reports subsequent transport failures or disappearing targets.
  Result requestDescription(uint8_t address);
  Result requestPoll(uint8_t mask, uint32_t now);
  void resetAmpHours();
  using FrameObserver = void (*)(void*, int8_t, const CanFrame&);
  void observeFrames(FrameObserver observer, void* context) { observer_ = observer; context_ = context; }
 private:
  Result change(const Configuration& candidate, uint8_t invalidate = 0);
  enum class TxPurpose : uint8_t { Background, Job, ManualRead };
  bool send(const CanFrame& frame, TxPurpose purpose = TxPurpose::Background);
  void serviceTransmit(uint32_t now);
  void reconcileSessions(const Configuration& next);
  void receive(const CanFrame& frame, uint32_t now);
  void advance(bool success);
  void beginReport(Operation operation, uint8_t requested, uint8_t recipients, uint8_t skipped = 0);
  void runJob(uint32_t now);
  void restore(uint32_t now);
  CanTransport& transport_;
  Configuration config_;
  Discovery discovery_;
  uint32_t revision_ = 1, lastSend_ = 0, lastProbe_ = 0;
  uint32_t reportSequence_ = 0;
  uint32_t lastScan_ = 0, maxLoopUs_ = 0, txJobSequence_ = 0;
  float sessionAh_[PSU_MAX_UNITS] = {};
  uint8_t sessionSeen_ = 0;
  uint32_t observedEpoch_[PSU_MAX_UNITS] = {};
  uint8_t voltageSynced_ = 0, running_ = 0, restorePending_ = 0, restoreBlocked_ = 0;
  uint8_t currentSynced_ = 0;
  uint8_t scanAddress_ = 1, probeIndex_ = 0;
  uint8_t requestedPoll_ = 0, requestedDescription_ = 0, txJobSlot_ = 0;
  bool txPending_ = false, sentThisTick_ = false;
  TxPurpose txPurpose_ = TxPurpose::Background;
  bool ready_ = false, waiting_ = false, secondPhase_ = false;
  bool voltageCommissioned_ = false, verifying_ = false, identityChecked_ = false;
  uint32_t verificationStarted_ = 0;
  uint8_t pending_ = 0, jobSlot_ = 0;
  uint16_t expected_ = 0, txFailures_ = 0;
  uint16_t readFailures_ = 0;
  ApplyPlan job_ = {};
  OperationReport report_ = {};
  FrameObserver observer_ = nullptr;
  void* context_ = nullptr;
};
}
