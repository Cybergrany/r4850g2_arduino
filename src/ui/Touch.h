#pragma once
#include "../config/TouchConfig.h"
#include "../storage/MemoryManager.h"
namespace psu {
struct TouchPoint { int16_t x, y; };
struct TouchSample { int16_t x, y, pressure; bool down; };
struct TouchCalibration {
  int32_t coefficient[6] = {}; // Q16.16 affine map from raw ADC to landscape pixels.
  bool valid = false;
  bool map(int16_t rawX, int16_t rawY, TouchPoint& point) const;
};
class CalibrationStore {
 public:
  CalibrationStore(ByteStorage& bytes, MemoryManager& memory) : bytes_(bytes), memory_(memory) {}
  bool load(TouchCalibration& value) const;
  StorageResult start(const TouchCalibration& value);
  void step();
  bool saving() const { return saving_; }
  StorageResult result() const { return result_; }
 private:
  bool read(uint8_t slot, uint8_t* data) const;
  ByteStorage& bytes_; MemoryManager& memory_;
  uint8_t record_[touch::recordBytes] = {}, at_ = 0, slot_ = 0;
  bool saving_ = false;
  StorageResult result_ = StorageResult::NoValidRecord;
};
enum class CalibrationState : uint8_t { Idle, Collecting, Ready, Failed };
class CalibrationWizard {
 public:
  void begin(uint32_t now);
  void sample(TouchSample sample, uint32_t now);
  void cancel() { state_ = CalibrationState::Idle; }
  CalibrationState state() const { return state_; }
  uint8_t targetIndex() const { return target_; }
  static TouchPoint target(uint8_t index);
  const TouchCalibration& result() const { return result_; }
 private:
  bool solve();
  CalibrationState state_ = CalibrationState::Idle;
  TouchCalibration result_;
  TouchPoint points_[5] = {};
  int32_t sumX_ = 0, sumY_ = 0;
  int16_t minX_ = 0, maxX_ = 0, minY_ = 0, maxY_ = 0;
  uint8_t target_ = 0, samples_ = 0;
  bool release_ = true, releaseSeen_ = false;
  uint32_t releasedAt_ = 0;
  uint32_t lastProgress_ = 0;
};
// Captures the initial target; dragging never activates a different button.
// A blocked/new screen consumes contact until a subsequent complete release.
class TouchGesture {
 public:
  void cancel() { armed_ = false; target_ = 0; repeating_ = releasing_ = false; }
  uint8_t update(bool down, uint8_t target, bool repeatable, bool enabled, uint32_t now);
 private:
  uint32_t pressedAt_ = 0, repeatedAt_ = 0, releasedAt_ = 0;
  uint8_t target_ = 0;
  bool armed_ = false, repeating_ = false, releasing_ = false;
};
}
