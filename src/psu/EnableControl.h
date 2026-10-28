#pragma once
#include "../config/ControlConfig.h"
namespace psu {
enum class EnableSource : uint8_t { Boot, Serial, Switch };
// Debounced edges, not a continuous OR: a held switch cannot undo serial input.
class EnableControl {
 public:
  void boot(bool on) { enabled_ = on; source_ = EnableSource::Boot; }
  void request(bool on, EnableSource source) { enabled_ = on; source_ = source; }
  bool sample(bool active, uint32_t now) {
    present_ = true;
    if (!sampled_ || active != raw_) { sampled_ = true; raw_ = active; changed_ = now; }
    if (uint32_t(now - changed_) < control::enableDebounceMs) return false;
    if (!settled_) {
      settled_ = true; stable_ = raw_;
      if (source_ != EnableSource::Boot || !stable_) return false;
      request(true, EnableSource::Switch); return true; // Boot: saved OR switch.
    }
    if (raw_ == stable_) return false;
    stable_ = raw_; request(stable_, EnableSource::Switch); return true;
  }
  bool enabled() const { return enabled_; }
  EnableSource source() const { return source_; }
  bool present() const { return present_; }
  bool settled() const { return settled_; }
  bool raw() const { return raw_; }
  bool stable() const { return stable_; }
 private:
  uint32_t changed_ = 0;
  EnableSource source_ = EnableSource::Boot;
  bool enabled_ = false, present_ = false, sampled_ = false, settled_ = false;
  bool raw_ = false, stable_ = false;
};
}
