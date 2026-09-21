#pragma once
#include "Psu.h"

namespace psu {
enum class Result : uint8_t { Ok, Invalid, Busy, Disabled, TransportError };
enum class ApplyMode : uint8_t { Online, Offline, OnlineAndOffline };

class PsuController {
 public:
  explicit PsuController(CanTransport& transport);
  bool begin();
  void tick(uint32_t now);
  uint8_t count() const { return count_; }
  const Psu& unit(uint8_t index) const { return units_[index]; } // index < count()
  bool ready() const { return ready_; }
  bool busy() const { return jobMask_ != 0; }
  uint16_t pollInterval() const { return pollMs_; }
  bool applyOnBoot() const { return applyOnBoot_; }
  uint16_t txFailures() const { return txFailures_; }
  uint8_t droppedFrames() const { return transport_.droppedFrames(); }
  uint32_t unknownFrames() const { return unknownFrames_; }

  // Stage validated settings in RAM. No CAN writes and no EEPROM writes.
  // Ranges are zero-based [first, end); ALL targets validate before any change.
  Result modifySingle(uint8_t index, Parameter parameter, float value);
  Result modifyRange(uint8_t first, uint8_t end, Parameter parameter, float value);
  Result setCount(uint8_t count);
  Result setPollInterval(uint16_t milliseconds);
  Result setApplyOnBoot(bool apply);
  Configuration configuration() const;
  Result configure(const Configuration& config);

  // Queue addressed writes. "Ok" means queued, not accepted by the PSU.
  // One command is outstanding at a time; per-PSU status records ACK/error/timeout.
  // Disabled slots are skipped. Bus writes cannot be atomic across several PSUs.
  Result applySingle(uint8_t index, ApplyMode mode = ApplyMode::Online);
  Result applyRange(uint8_t first, uint8_t end, ApplyMode mode = ApplyMode::Online);
  Result requestData(uint8_t first, uint8_t end);
  Result requestDescription(uint8_t index);
  Result resetAmpHours(uint8_t first, uint8_t end);

  using FrameObserver = void (*)(void*, int8_t, const CanFrame&);
  // Called in main-loop context only. index == -1 means unconfigured/unknown PSU.
  void observeFrames(FrameObserver observer, void* context);
 private:
  bool validRange(uint8_t first, uint8_t end) const;
  bool send(const CanFrame& frame);
  void receive(const CanFrame& frame, uint32_t now);
  void finishUnit();
  CanTransport& transport_;
  Psu units_[PSU_MAX_UNITS];
  uint8_t count_ = 1;
  uint16_t pollMs_ = limits::defaultPollMs;
  bool applyOnBoot_ = false;
  bool ready_ = false;
  uint8_t pollIndex_ = 0;
  uint32_t lastPoll_ = 0;
  uint32_t lastCommand_ = 0;
  uint16_t txFailures_ = 0;
  uint32_t unknownFrames_ = 0;
  uint8_t jobMask_ = 0;
  uint8_t jobIndex_ = 0;
  uint8_t phase_ = 0;
  bool waiting_ = false;
  ApplyMode mode_ = ApplyMode::Online;
  uint16_t expected_ = 0;
  FrameObserver observer_ = nullptr;
  void* observerContext_ = nullptr;
};
}
