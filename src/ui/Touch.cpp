#include "Touch.h"
#include "../config/BoardConfig.h"
#include <string.h>
#include <math.h>
#include <stdlib.h>
namespace psu {
namespace {
uint32_t get32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }
void put32(uint8_t* p, uint32_t v) { for (uint8_t i = 0; i < 4; ++i) p[i] = v >> (i * 8); }
uint16_t checksum(const uint8_t* data) {
  uint16_t crc = 0xffff;
  for (uint8_t i = 1; i < 38; ++i) {
    crc ^= uint16_t(data[i]) << 8;
    for (uint8_t b = 0; b < 8; ++b) crc = crc & 0x8000 ? (crc << 1) ^ 0x1021 : crc << 1;
  }
  return crc;
}
bool newer(uint32_t a, uint32_t b) { return int32_t(a - b) > 0; }
}
bool TouchCalibration::map(int16_t x, int16_t y, TouchPoint& p) const {
  if (!valid || x < 0 || x > 4095 || y < 0 || y > 4095) return false;
  const int64_t px = int64_t(coefficient[0]) * x + int64_t(coefficient[1]) * y + coefficient[2];
  const int64_t py = int64_t(coefficient[3]) * x + int64_t(coefficient[4]) * y + coefficient[5];
  const int32_t xx = px / 65536, yy = py / 65536;
  if (xx < 0 || xx >= 320 || yy < 0 || yy >= 240) return false;
  p = {int16_t(xx), int16_t(yy)}; return true;
}
bool CalibrationStore::read(uint8_t slot, uint8_t* d) const {
  if (bytes_.length() < 512 || !bytes_.ready()) return false;
  for (uint8_t i = 0; i < touch::recordBytes; ++i) d[i] = bytes_.read(touch::recordAddress[slot] + i);
  return d[0] == 0xc7 && d[1] == 1 && d[2] == board::displayRotation && d[3] == 40 &&
      d[8] == uint8_t(touch::profile) && d[9] == uint8_t(touch::profile >> 8) && d[10] == 'T' && d[11] == 'C' &&
      d[38] == uint8_t(checksum(d)) && d[39] == uint8_t(checksum(d) >> 8);
}
bool CalibrationStore::load(TouchCalibration& c) const {
  if (memory_.saving()) return false;
  uint8_t d[40]; uint32_t best = 0; int8_t found = -1;
  for (uint8_t i = 0; i < 2; ++i) if (read(i, d) && (found < 0 || newer(get32(d + 4), best))) { best = get32(d + 4); found = i; }
  if (found < 0) return false;
  read(found, d); for (uint8_t i = 0; i < 6; ++i) c.coefficient[i] = int32_t(get32(d + 12 + i * 4));
  c.valid = true; return true;
}
StorageResult CalibrationStore::start(const TouchCalibration& c) {
  if (memory_.saving() || !bytes_.ready()) return StorageResult::Busy;
  if (!c.valid) return StorageResult::InvalidConfig;
  if (bytes_.length() < 512) return StorageResult::TooSmall;
  uint32_t best = 0; int8_t found = -1;
  for (uint8_t i = 0; i < 2; ++i) if (read(i, record_) && (found < 0 || newer(get32(record_ + 4), best))) { best = get32(record_ + 4); found = i; }
  memset(record_, 0, sizeof(record_)); record_[0] = 0xc7; record_[1] = 1;
  record_[2] = board::displayRotation; record_[3] = 40; put32(record_ + 4, best + 1);
  record_[8] = uint8_t(touch::profile); record_[9] = uint8_t(touch::profile >> 8); record_[10] = 'T'; record_[11] = 'C';
  for (uint8_t i = 0; i < 6; ++i) put32(record_ + 12 + i * 4, uint32_t(c.coefficient[i]));
  const auto crc = checksum(record_); record_[38] = crc; record_[39] = crc >> 8;
  slot_ = found < 0 ? 0 : 1 - found; at_ = 0; saving_ = true;
  memory_.auxiliaryBusy(true); result_ = StorageResult::Busy; return StorageResult::Ok;
}
void CalibrationStore::step() {
  if (!saving_ || !bytes_.ready()) return;
  const auto base = touch::recordAddress[slot_];
  if (!at_) bytes_.update(base, 0);
  else if (at_ < 40) bytes_.update(base + at_, record_[at_]);
  else if (at_ == 40) bytes_.update(base, record_[0]);
  else {
    uint8_t d[40]; result_ = read(slot_, d) && !memcmp(d, record_, 40) ? StorageResult::Ok : StorageResult::WriteFailed;
    saving_ = false; memory_.auxiliaryBusy(false);
  }
  ++at_;
}
TouchPoint CalibrationWizard::target(uint8_t i) {
  switch (i) {
    case 0: return {46, 56}; case 1: return {274, 56}; case 2: return {46, 168};
    case 3: return {274, 168}; default: return {166, 104};
  }
}
void CalibrationWizard::begin(uint32_t now) {
  state_ = CalibrationState::Collecting; result_ = {}; target_ = samples_ = 0;
  release_ = true; releaseSeen_ = false; sumX_ = sumY_ = 0; lastProgress_ = now;
}
void CalibrationWizard::sample(TouchSample s, uint32_t now) {
  if (state_ != CalibrationState::Collecting) return;
  if (uint32_t(now - lastProgress_) > touch::calibrationTimeoutMs) { state_ = CalibrationState::Failed; return; }
  if (release_) {
    if (!s.down) {
      if (!releaseSeen_) { releasedAt_=now; releaseSeen_=true; }
      if (uint32_t(now-releasedAt_)>=touch::debounceMs) {
        release_ = false; releaseSeen_=false;
        if (target_ == 5) state_ = solve() ? CalibrationState::Ready : CalibrationState::Failed;
      }
    } else releaseSeen_=false;
    return;
  }
  if (!s.down || s.pressure < int16_t(touch::pressureMinimum) || s.x < 0 || s.y < 0 || s.x > 4095 || s.y > 4095) {
    samples_ = 0; sumX_ = sumY_ = 0; return;
  }
  if (!samples_) { minX_ = maxX_ = s.x; minY_ = maxY_ = s.y; }
  if (s.x < minX_) minX_ = s.x;
  if (s.x > maxX_) maxX_ = s.x;
  if (s.y < minY_) minY_ = s.y;
  if (s.y > maxY_) maxY_ = s.y;
  if (maxX_ - minX_ > int16_t(touch::sampleSpread) || maxY_ - minY_ > int16_t(touch::sampleSpread)) { samples_ = 0; sumX_ = sumY_ = 0; return; }
  sumX_ += s.x; sumY_ += s.y;
  if (++samples_ == touch::stableSamples) {
    points_[target_++] = {int16_t(sumX_ / samples_), int16_t(sumY_ / samples_)};
    samples_ = 0; sumX_ = sumY_ = 0; release_ = true; releaseSeen_=false; lastProgress_ = now;
  }
}
bool CalibrationWizard::solve() {
  const float x0 = points_[0].x, y0 = points_[0].y;
  const float dx1 = points_[1].x - x0, dy1 = points_[1].y - y0;
  const float dx2 = points_[2].x - x0, dy2 = points_[2].y - y0;
  const float det = dx1 * dy2 - dx2 * dy1;
  if (fabsf(det) < 10000) return false;
  const float a = 228 * dy2 / det, b = -228 * dx2 / det;
  const float d = -112 * dy1 / det, e = 112 * dx1 / det;
  const float coeff[6] = {a,b,46-a*x0-b*y0,d,e,56-d*x0-e*y0};
  for (uint8_t i = 0; i < 6; ++i) {
    if (!isfinite(coeff[i]) || fabsf(coeff[i]) > 10000) return false;
    result_.coefficient[i] = int32_t(coeff[i] * 65536);
  }
  result_.valid = true;
  for (uint8_t i = 0; i < 5; ++i) {
    TouchPoint p; const auto goal = target(i);
    if (!result_.map(points_[i].x, points_[i].y, p) || abs(p.x-goal.x) > touch::verificationPixels || abs(p.y-goal.y) > touch::verificationPixels) {
      result_.valid = false; return false;
    }
  }
  return true;
}
uint8_t TouchGesture::update(bool down, uint8_t hit, bool repeatable, bool enabled, uint32_t now) {
  if (!enabled) { cancel(); return 0; }
  if (!down) {
    if (!releasing_) { releasing_=true; releasedAt_=now; return 0; }
    if (uint32_t(now-releasedAt_)<touch::debounceMs) return 0;
    const uint8_t result = armed_ && target_ && !repeating_ && uint32_t(releasedAt_ - pressedAt_) >= touch::debounceMs ? target_ : 0;
    armed_ = true; target_ = 0; repeating_ = false; return result;
  }
  releasing_=false;
  if (!armed_) return 0;
  if (!target_) {
    // A touch starting outside a button is consumed until release.
    if (!hit) { cancel(); return 0; }
    target_ = hit; pressedAt_ = repeatedAt_ = now; return 0;
  }
  if (target_ != hit) { cancel(); return 0; }
  if (repeatable && uint32_t(now - pressedAt_) >= touch::repeatDelayMs && uint32_t(now - repeatedAt_) >= touch::repeatMs) {
    repeatedAt_ = now; repeating_ = true; return target_;
  }
  return 0;
}
}
