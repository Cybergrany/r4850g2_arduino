#include "PsuController.h"

namespace psu {
PsuController::PsuController(CanTransport& transport) : transport_(transport) {
  configure(defaultConfiguration());
}
bool PsuController::begin() { ready_ = transport_.begin(); return ready_; }
bool PsuController::validRange(uint8_t first, uint8_t end) const {
  return first < end && end <= count_;
}
Configuration PsuController::configuration() const {
  Configuration c = {};
  c.count = count_; c.pollMs = pollMs_; c.applyOnBoot = applyOnBoot_;
  for (uint8_t i = 0; i < PSU_MAX_UNITS; ++i) c.units[i] = units_[i].config_;
  return c;
}
Result PsuController::configure(const Configuration& c) {
  if (busy()) return Result::Busy;
  if (!validConfig(c)) return Result::Invalid;
  count_ = c.count; pollMs_ = c.pollMs; applyOnBoot_ = c.applyOnBoot;
  pollIndex_ = 0;
  for (uint8_t i = 0; i < PSU_MAX_UNITS; ++i) units_[i].reset(c.units[i]);
  return Result::Ok;
}
Result PsuController::modifySingle(uint8_t i, Parameter p, float v) {
  if (i >= count_) return Result::Invalid;
  return modifyRange(i, i + 1, p, v);
}
Result PsuController::modifyRange(uint8_t first, uint8_t end, Parameter p, float v) {
  if (busy()) return Result::Busy;
  if (!validRange(first, end)) return Result::Invalid;
  Configuration candidate = configuration();
  for (uint8_t i = first; i < end; ++i)
    if (!changeParameter(candidate.units[i], p, v)) return Result::Invalid;
  if (!validConfig(candidate)) return Result::Invalid;
  for (uint8_t i = first; i < end; ++i) {
    if (units_[i].config_.address != candidate.units[i].address)
      units_[i].reset(candidate.units[i]); // Never attribute the old address's telemetry to the new PSU.
    else units_[i].config_ = candidate.units[i];
  }
  return Result::Ok;
}
Result PsuController::setCount(uint8_t count) {
  if (busy()) return Result::Busy;
  Configuration c = configuration(); c.count = count;
  if (!validConfig(c)) return Result::Invalid;
  count_ = count; pollIndex_ = 0;
  return Result::Ok;
}
Result PsuController::setPollInterval(uint16_t ms) {
  if (busy()) return Result::Busy;
  if (ms < limits::minPollMs || ms > limits::maxPollMs) return Result::Invalid;
  pollMs_ = ms; return Result::Ok;
}
Result PsuController::setApplyOnBoot(bool apply) {
  if (busy()) return Result::Busy;
  applyOnBoot_ = apply; return Result::Ok;
}
Result PsuController::applySingle(uint8_t i, ApplyMode mode) {
  if (i >= count_) return Result::Invalid;
  return applyRange(i, i + 1, mode);
}
Result PsuController::applyRange(uint8_t first, uint8_t end, ApplyMode mode) {
  if (busy()) return Result::Busy;
  if (!validRange(first, end)) return Result::Invalid;
  if (!ready_) return Result::TransportError;
  uint8_t mask = 0;
  for (uint8_t i = first; i < end; ++i) {
    if (!validConfig(units_[i].config_)) return Result::Invalid;
    if (units_[i].config_.enabled) mask |= uint8_t(1U << i);
  }
  if (!mask) return Result::Disabled;
  jobMask_ = mask; mode_ = mode; phase_ = 0; waiting_ = false;
  for (uint8_t i = first; i < end; ++i)
    if (mask & (1U << i)) units_[i].status_ = {CommandState::Queued, 0, 0};
  return Result::Ok;
}
bool PsuController::send(const CanFrame& f) {
  if (ready_ && transport_.send(f)) return true;
  if (txFailures_ != UINT16_MAX) ++txFailures_;
  return false;
}
Result PsuController::requestData(uint8_t first, uint8_t end) {
  if (!validRange(first, end)) return Result::Invalid;
  bool any = false;
  for (uint8_t i = first; i < end; ++i) {
    if (!units_[i].config_.enabled) continue;
    any = true;
    if (!send(protocol::request(units_[i].config_.address))) return Result::TransportError;
  }
  return any ? Result::Ok : Result::Disabled;
}
Result PsuController::requestDescription(uint8_t i) {
  if (i >= count_) return Result::Invalid;
  if (!units_[i].config_.enabled) return Result::Disabled;
  return send(protocol::request(units_[i].config_.address, protocol::descriptionCommand))
      ? Result::Ok : Result::TransportError;
}
Result PsuController::resetAmpHours(uint8_t first, uint8_t end) {
  if (!validRange(first, end)) return Result::Invalid;
  for (uint8_t i = first; i < end; ++i) units_[i].telemetry_.ampHours = 0;
  return Result::Ok;
}
void PsuController::observeFrames(FrameObserver observer, void* context) {
  observer_ = observer; observerContext_ = context;
}
void PsuController::finishUnit() {
  jobMask_ &= uint8_t(~(1U << jobIndex_));
  waiting_ = false; phase_ = 0;
}
void PsuController::receive(const CanFrame& f, uint32_t now) {
  int8_t index = -1;
  const bool reply = protocol::isReply(f);
  if (reply || protocol::isCurrentBroadcast(f)) {
    for (uint8_t i = 0; i < count_; ++i) {
      if (units_[i].config_.address != protocol::address(f.id)) continue;
      index = i;
      units_[i].receive(f, now);
      if (reply && protocol::command(f.id) == protocol::setCommand &&
          (f.data[0] == 1 || f.data[0] == 0x21)) {
        const uint32_t raw = protocol::readBigEndian(f.data + 4);
        if (waiting_ && jobIndex_ == i && units_[i].status_.reg == f.data[1] && raw == expected_) {
          auto& status = units_[i].status_;
          const bool rejected = f.data[0] & 0x20;
          status = {rejected ? CommandState::Rejected : CommandState::Success, f.data[1], raw};
          waiting_ = false;
          if (rejected || ++phase_ == (mode_ == ApplyMode::OnlineAndOffline ? 4 : 2)) finishUnit();
          else status.state = CommandState::Queued;
        }
      }
      break;
    }
  }
  if (index < 0) ++unknownFrames_;
  if (observer_) observer_(observerContext_, index, f);
}
void PsuController::tick(uint32_t now) {
  if (!ready_) return;
  CanFrame frame;
  // Bounded work even under unrelated bus traffic. ISR only queues frames.
  for (uint8_t n = 0; n < 32 && transport_.receive(frame); ++n) receive(frame, now);
  if (waiting_ && uint32_t(now - lastCommand_) >= limits::ackTimeoutMs) {
    units_[jobIndex_].status_.state = CommandState::Timeout;
    finishUnit(); // No automatic retries, especially for PSU nonvolatile writes.
  }
  if (jobMask_ && !waiting_ && uint32_t(now - lastCommand_) >= limits::commandGapMs) {
    for (jobIndex_ = 0; !(jobMask_ & (1U << jobIndex_)); ++jobIndex_) {}
    const auto& c = units_[jobIndex_].config_;
    const bool offline = mode_ == ApplyMode::Offline || phase_ >= 2;
    const bool current = phase_ & 1;
    const uint8_t reg = current ? (offline ? protocol::OfflineCurrent : protocol::OnlineCurrent)
                                : (offline ? protocol::OfflineVoltage : protocol::OnlineVoltage);
    expected_ = current ? protocol::encodeCurrent(offline ? c.offlineCurrent : c.current, c.ratedCurrent)
                        : protocol::encodeVoltage(offline ? c.offlineVoltage : c.voltage);
    auto& status = units_[jobIndex_].status_;
    status = {CommandState::Waiting, reg, expected_};
    lastCommand_ = now;
    waiting_ = send(protocol::setting(c.address, reg, expected_));
    if (!waiting_) { status.state = CommandState::TransportError; finishUnit(); }
    return;
  }
  // Stagger requests; a menu or partial serial line never stops polling.
  if (uint32_t(now - lastPoll_) >= uint16_t(pollMs_ / count_)) {
    lastPoll_ = now;
    requestData(pollIndex_, pollIndex_ + 1);
    pollIndex_ = (pollIndex_ + 1) % count_;
  }
}
}
