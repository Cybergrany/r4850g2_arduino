#pragma once
#include "Discovery.h"
#include "PsuControl.h"
#include "Availability.h"
#include "Alerts.h"
#include "Thermal.h"
#include "../diagnostics/BusProfile.h"
namespace psu {
enum class Operation : uint8_t { All, Voltage, GroupCurrent, Offline, Restore };
struct ApplyPlan {
  Operation operation;
  int8_t group;
  SettingScope scope;
  SettingFields fields;
  UnitMask members, recipients, missing, excluded;
  uint16_t voltage[PSU_MAX_UNITS], current[PSU_MAX_UNITS];
  Issue issues[PSU_MAX_UNITS], blocker;
  uint32_t configurationRevision, topologyRevision, created;
  bool partial, prepared;
};
struct OperationReport {
  uint32_t sequence;
  Operation operation;
  UnitMask requested, recipients, succeeded, failed, skipped;
  bool active, prepared;
  CommandStatus units[PSU_MAX_UNITS];
};
struct GroupStatus {
  UnitMask members, responding, ready, missing;
  uint16_t requestedCurrent, authorizedCurrent;
};
struct BusStatus { uint8_t configured, responding, verified, broadcasting; };
struct CurrentCheck {
  Issue issue;
  uint8_t slot;
  uint16_t share, maximum;
};
struct ControlStatus { Activity activity; Issue blocker; uint8_t slot; };
struct ProfileReview {
  uint32_t generation = 0, revision = 0, topology = 0, availability = 0, created = 0;
  UnitMask included = 0;
  uint8_t slot = 0, mismatch = PSU_MAX_UNITS;
  bool restoreInclusion = false;
  Issue blocker = Issue::None;
  StorageResult storage = StorageResult::Ok;
};
enum class ProfileLoadState : uint8_t { Idle, Applying, Saving, Complete, Failed };
struct ProfileLoadStatus {
  uint32_t operation = 0, revision = 0, topology = 0, availability = 0;
  UnitMask participation = 0;
  uint16_t saveToken = 0;
  ProfileLoadState state = ProfileLoadState::Idle;
  StorageResult storage = StorageResult::Ok;
  bool prepared = false, older = false, restoreParticipation = false;
};
class PsuController {
 public:
  explicit PsuController(CanTransport& transport);
#if PSU_BUS_PROFILE
  BusProfile& busProfile() { return busProfile_; }
  void busReceiveStats(BusReceiveStats& stats) const { transport_.busReceiveStats(stats); }
#endif
  bool begin();
  void tick(uint32_t now);
  // Bounded RX/protection service between drawing slices. No new setting or
  // read requests are dispatched; the normal tick owns command scheduling.
  uint8_t serviceReceive(uint32_t now);
  const Configuration& configuration() const { return config_; }
  Result configure(const Configuration& config, bool resumeSaved = false);
  Result setCount(uint8_t count);
  Result setGroup(const char* name, UnitMask members);
  Result removeGroup(uint8_t group);
  Result bind(uint8_t slot, const Identity& identity, uint16_t ratedCurrent, uint32_t now);
  // Also permits releasing a retained inactive slot. This does not turn off a PSU.
  Result unbind(uint8_t slot);
  Result setVoltage(uint16_t voltage, bool offline = false);
  Result setCurrent(uint8_t group, uint16_t total, bool offline = false);
  Result set(SettingScope scope, SettingFields field, uint16_t value);
  uint16_t settingMaximum(SettingScope scope, SettingFields field) const;
  CurrentCheck checkCurrent(uint8_t group, uint16_t total) const;
  // Exact maximum accepted by checkCurrent, including unequal ratings/rounding.
  uint16_t groupCurrentMaximum(uint8_t group) const;
  uint32_t configurationRevision() const { return revision_; }
  Result setAutoResume(bool enabled);
  void sampleEnableInput(bool active, uint32_t now);
  const EnableControl& enableControl() const { return enable_; }
  const UnitControl& unitControl(uint8_t slot) const { return control_[slot]; }
  ReadinessStatus readinessStatus(uint8_t slot, uint32_t now) const;
  // Immediate, volatile fan test; targets are configured slots, not CAN addresses.
  // AUTO is only allowed while OFF and confirmed awake at zero current.
  Issue fanOverrideIssue(uint8_t slot, FanOverride mode, uint32_t now) const;
  Result setFanOverride(UnitMask mask, FanOverride mode, uint32_t now);
  // Logical duty (0..255), independent of the optional physical pin and enable.
  ExternalFanStatus externalFanStatus(uint32_t now) const;
  ControlStatus controlStatus(uint32_t now) const;
  // Live permission, not measured current or an acknowledgement. Includes a
  // positive-current start while its output readiness is still being qualified.
  bool psuActive(uint32_t now) const;
  // Explicit retry after an operator has reviewed a latched control failure.
  void reconcile();
  void configureStartup(bool installed, bool configurationReady, uint32_t now);
  const StartupStatus& startupStatus() const { return startup_; }
  bool chargeAllowed() const { return startup_.permit == PermitState::Allowed; }
  bool powerAvailable() const { return startup_.permit == PermitState::NotFitted || chargeAllowed(); }
  Result hardStop();
  UnitMask excluded() const { return excluded_; }
  UnitMask acceptedAbsences() const { return (config_.acceptedAbsent | config_.disabled | sessionAccepted_) & UnitMask(~sessionRequired_); }
  UnitMask participants() const { return membersMask(count()) & UnitMask(~excluded_); }
  UnitMask requiredMembers() const { return membersMask(count()) & UnitMask(~(excluded_ & acceptedAbsences())); }
  UnitMask deliberateExclusions() const { return (config_.disabled | sessionAccepted_) & UnitMask(~sessionRequired_); }
  ParticipationStatus participationStatus(uint8_t slot) const;
  AvailabilityStatus availabilityStatus(uint8_t group, uint32_t now) const;
  AvailabilityPlan previewParticipation(UnitMask members, UnitMask excluded, bool remember, uint32_t now) const;
  AvailabilityReason availabilityReason(uint8_t slot, uint32_t now) const;
  bool availabilitySettled(uint8_t group, uint32_t now) const;
  bool availabilityUnstable(uint8_t group, uint32_t now) const;
  UnitMask availabilityAttention(uint32_t now) const;
  uint32_t availabilityRevision() const { return availabilityRevision_; }
  AvailabilityPlan previewAvailability(AvailabilityAction action, UnitMask mask, bool remember, uint32_t now) const;
  Result confirmAvailability(const AvailabilityPlan& plan, MemoryManager& memory, uint32_t now);
  const AvailabilityReport& availabilityReport() const { return availabilityReport_; }
  bool availabilitySaving() const { return availabilityReport_.saving; }
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
  ApplyPlan preview(SettingScope scope, SettingFields fields, uint32_t now, bool partial = false,
      const uint16_t* voltage = nullptr, const uint16_t* current = nullptr) const;
  // Plan expiry + config/topology checks apply to every queue, including confirmations.
  Result queue(const ApplyPlan& plan, bool confirmed, uint32_t now);
  ProfileReview previewProfile(MemoryManager& memory, uint8_t slot, bool inclusion, uint32_t now) const;
  Result loadProfile(MemoryManager& memory, const ProfileReview& review, uint32_t now);
  void profileTick(MemoryManager& memory);
  const ProfileLoadStatus& profileLoad() const { return profileLoad_; }
  const OperationReport& report() const { return report_; }
  bool voltageSynchronized(uint8_t slot) const { return slot < count() && (voltageSynced_ & (1U << slot)); }
  bool currentSynchronized(uint8_t slot) const { return slot < count() && (currentSynced_ & (1U << slot)); }
  const Discovery& discovery() const { return discovery_; }
  bool busy() const { return report_.active; }
  bool ready() const { return ready_; }
  AlertManager& alerts() { return alerts_; }
  const AlertManager& alerts() const { return alerts_; }
  ThermalStatus thermalStatus() const;
  uint32_t protectionRevision() const { return thermalRevision_; }
  uint16_t effectiveCurrent(uint8_t slot) const;
  bool acknowledgedCurrent(uint8_t slot, uint16_t& raw) const;
  bool temperatures(uint8_t slot, float& highest, uint32_t now) const;
  Result retryTemperature(UnitMask mask, uint32_t now);
  void serviceProtection(MemoryManager& memory, uint32_t now);
  void storageStatus(StorageResult result, bool older = false);
  void localStatus(bool display, bool calibration, bool saveFailed = false);
  uint8_t count() const { return config_.count; }
  uint16_t pollInterval() const { return config_.pollMs; }
  uint32_t staleMs() const {
    static_assert(limits::maxPollMs <= UINT16_MAX / 3, "Telemetry timeout must fit");
    const uint16_t age = config_.pollMs * uint16_t(3);
    return age < limits::minimumStaleMs ? limits::minimumStaleMs : age;
  }
  uint16_t txFailures() const { return txFailures_; }
  uint8_t droppedFrames() const { return transport_.droppedFrames(); }
  uint8_t receiveHighWater() const { return transport_.receiveHighWater(); }
  uint16_t hardwareOverflows() const { return transport_.hardwareOverflows(); }
  uint16_t receiveInterruptMaxUs() const { return transport_.receiveInterruptMaxUs(); }
  uint16_t readFailures() const { return readFailures_; }
  uint32_t maxLoopUs() const { return maxLoopUs_; }
  void recordLoopTime(uint32_t us) { if (us > maxLoopUs_) maxLoopUs_ = us; }
  // A sweep probes 1..127 with INFO requests; no broadcast/configuration writes.
  void scan() { scanAddress_ = 1; }
  uint8_t scanning() const { return scanAddress_; }
  // Read requests are queued and paced. OK means queued, not a received reply;
  // readFailures reports subsequent transport failures or disappearing targets.
  Result requestDescription(uint8_t address);
  Result requestPoll(UnitMask mask, uint32_t now);
  void resetAmpHours();
  // Final flag identifies an accepted routine refresh; failures/corrections stay visible.
  using FrameObserver = void (*)(void*, int8_t, const CanFrame&, bool);
  void observeFrames(FrameObserver observer, void* context) { observer_ = observer; context_ = context; }
 private:
#if PSU_BUS_PROFILE
  BusProfile busProfile_;
#endif
  __attribute__((noinline)) Result applyProfile(MemoryManager& memory, const ProfileReview& review, uint32_t now);
  __attribute__((noinline)) Result stageProfile(MemoryManager& memory, const ProfileReview& review);
  static constexpr int8_t unknownRoute = -4, unboundRoute = -3;
  // Keep the common lookup small: no variable-bit loop or out-of-line register
  // save on each metric/status query. Resolving and validation stay in one path.
  inline __attribute__((always_inline)) int8_t routeIndex(uint8_t slot, uint32_t now) const {
    if (slot >= count()) return -1;
    if (routeTime_ == now && routeRevision_ == discovery_.revision() && routes_[slot] != unknownRoute)
      return routes_[slot];
    return resolveRoute(slot, now);
  }
  int8_t resolveRoute(uint8_t slot, uint32_t now) const;
  void validateSource(uint8_t slot, const Psu& device, uint32_t now) const;
  void clearRoutes() const;
  // Source identity/lease validation only, never readiness, telemetry values or
  // permission. Time/revision/config changes clear it; RX refreshes its source
  // before ACK/readiness processing, including another frame in the same ms.
  mutable int8_t routes_[PSU_MAX_UNITS] = {};
  mutable UnitMask validSources_ = 0;
  mutable uint32_t routeTime_ = 0, routeRevision_ = 0;
  void thermalTick(uint32_t now);
  void alertTick(uint32_t now);
  void thermalStop(ThermalStop reason);
  Result requestEnable(bool on, EnableSource source);
  uint16_t limitedCurrent(uint8_t slot, uint16_t authorized) const;
  uint16_t effectiveRaw(uint8_t slot) const;
  bool correctCurrent(uint32_t now);
  AlertManager alerts_;
  uint32_t thermalLast_ = 0, hotMs_ = 0, thermalRevision_ = 0;
  float highestTemperature_ = 0;
  uint16_t acknowledgedCurrent_[PSU_MAX_UNITS] = {};
  uint16_t protectionToken_ = 0, storageSeenToken_ = 0;
  UnitMask acknowledgedMask_ = 0, validTemperatures_ = 0, hotContributors_ = 0;
  UnitMask sensorFaults_ = 0, thermalIgnored_ = 0, energized_ = 0, activeLost_ = 0, identityLost_ = 0;
  ThermalLevel thermalLevel_ = ThermalLevel::Normal;
  ThermalStop savingStop_ = ThermalStop::None;
  EnableSource resumeSource_ = EnableSource::Serial;
  bool hotPrevious_ = false, cool_ = false, protectionDirty_ = false, protectionFailed_ = false, resumePending_ = false;
  bool controlCorrection_ = false;
  Result change(Configuration& candidate, UnitMask invalidate = 0);
  ApplyPlan buildPlan(Operation operation, SettingScope scope, SettingFields fields, uint32_t now, bool partial,
      const uint16_t* voltage = nullptr, const uint16_t* current = nullptr) const;
  enum class TxPurpose : uint8_t { Background, Job, ManualRead, Control };
  bool send(const CanFrame& frame, TxPurpose purpose = TxPurpose::Background);
  void serviceTransmit(uint32_t now);
  void reconcileSessions(const Configuration& next);
  void receive(const CanFrame& frame, uint32_t now);
  void advance(bool success);
  void beginReport(Operation operation, UnitMask requested, UnitMask recipients, UnitMask skipped = 0);
  void runJob(uint32_t now);
  void restore(uint32_t now);
  void refreshSettings(uint32_t now);
  void availabilityTick(uint32_t now);
  void startupTick(uint32_t now);
  void finishAvailability(uint32_t now);
  void applyAvailability(const AvailabilityPlan& plan);
  void applyParticipation(UnitMask members, UnitMask excluded, bool remember, bool releaseHot, uint32_t now);
  Issue routingIssue(uint8_t slot, uint32_t now) const;
  void enableChanged(bool wasEnabled);
  void cancelJob();
  void controlTick(uint32_t now);
  bool controlReceive(const CanFrame& frame, uint32_t now);
  void controlCommand(uint8_t slot, uint8_t reg, uint16_t value, uint32_t now);
  void controlComplete(CommandState state, uint32_t now);
  void latchControlFault(uint8_t slot, CommandState state, uint8_t reg, uint32_t now);
  void controlPhase(uint8_t slot, ControlPhase phase, uint32_t now);
  void updateFan(uint8_t slot, uint32_t now);
  void clearFanOverride(uint8_t slot);
  bool readinessAcceptable(uint8_t slot, uint32_t now) const;
  bool readinessEvidence(uint8_t slot, uint32_t now) const;
  void readinessReceive(uint8_t slot, const CanFrame& frame, uint32_t now);
  void currentSent(uint8_t slot, uint16_t raw, uint32_t now);
  void currentAccepted(uint8_t slot, uint16_t raw);
  bool wakeAllowed(uint8_t slot, uint32_t now) const;
  bool releaseAllowed(uint32_t now) const;
  bool controlsOnline(UnitMask mask) const;
  uint16_t reconciliationVoltage(uint8_t slot) const;
  CanTransport& transport_;
  EnableControl enable_;
  UnitControl control_[PSU_MAX_UNITS];
  uint32_t controlStarted_ = 0, controlEpoch_ = 0;
  uint16_t controlValue_ = 0;
  uint8_t controlSlot_ = 0, controlRegister_ = 0;
  bool controlActive_ = false, controlWaiting_ = false, controlVerified_ = false;
  bool controlRefresh_ = false, refreshCurrent_ = false;
  uint8_t refreshSlot_ = 0;
  uint32_t refreshEpoch_ = 0;
  StartupStatus startup_;
  uint32_t now_ = 0, acReleased_ = 0;
  bool startupConfigured_ = false;
  UnitMask excluded_ = 0, sessionAccepted_ = 0, sessionRequired_ = 0;
  uint32_t availabilityRevision_ = 1;
  AvailabilityReason availabilityReasons_[PSU_MAX_UNITS] = {};
  uint32_t groupChanged_[limits::maxGroups] = {}, groupEpisode_[limits::maxGroups] = {};
  uint32_t refreshed_[PSU_MAX_UNITS] = {};
  uint8_t refreshIndex_ = 0;
  AvailabilityPlan savedAvailability_;
  AvailabilityReport availabilityReport_;
  MemoryManager* availabilityMemory_ = nullptr;
  uint16_t availabilitySaveToken_ = 0;
  Configuration config_;
  ProfileLoadStatus profileLoad_;
  Discovery discovery_;
  uint32_t revision_ = 1, lastSend_ = 0, lastProbe_ = 0;
  uint32_t reportSequence_ = 0;
  uint32_t lastScan_ = 0, maxLoopUs_ = 0, txJobSequence_ = 0;
  float sessionAh_[PSU_MAX_UNITS] = {};
  UnitMask sessionSeen_ = 0;
  uint32_t observedEpoch_[PSU_MAX_UNITS] = {};
  UnitMask voltageSynced_ = 0, running_ = 0, restorePending_ = 0, restoreBlocked_ = 0;
  UnitMask currentSynced_ = 0;
  uint8_t scanAddress_ = 1, probeIndex_ = 0;
  UnitMask requestedPoll_ = 0;
  uint8_t requestedDescription_ = 0, txJobSlot_ = 0;
  bool txPending_ = false, sentThisTick_ = false;
  TxPurpose txPurpose_ = TxPurpose::Background;
  bool ready_ = false, waiting_ = false, secondPhase_ = false;
  bool jobStarted_ = false;
  bool verifying_ = false, identityChecked_ = false;
  uint32_t verificationStarted_ = 0;
  UnitMask pending_ = 0;
  uint8_t jobSlot_ = 0;
  uint16_t expected_ = 0, txFailures_ = 0;
  uint16_t readFailures_ = 0;
  uint8_t jobAddresses_[PSU_MAX_UNITS] = {};
  uint32_t jobEpochs_[PSU_MAX_UNITS] = {};
  ApplyPlan job_ = {};
  OperationReport report_ = {};
  FrameObserver observer_ = nullptr;
  void* context_ = nullptr;
};
}
