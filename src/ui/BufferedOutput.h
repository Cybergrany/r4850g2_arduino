#pragma once
#include <Arduino.h>
#include "../config/ConsoleConfig.h"
namespace psu {
// Main-loop-only Print sink. Producers reserve a complete bounded response;
// the UART drain never writes more bytes than availableForWrite() permits.
class BufferedOutput : public Print {
 public:
  using Print::write;
  size_t write(uint8_t byte) override {
    if (used_ == sizeof(bytes_)) { if (overruns_ != UINT16_MAX) ++overruns_; return 0; }
    bytes_[head_] = byte;
    if (++head_ == sizeof(bytes_)) head_ = 0;
    ++used_; return 1;
  }
  void drain(Stream& stream) {
    int room = stream.availableForWrite();
    if (room > console::outputBytesPerTick) room = console::outputBytesPerTick;
    while (room-- > 0 && used_) {
      if (!stream.write(bytes_[tail_])) break;
      if (++tail_ == sizeof(bytes_)) tail_ = 0;
      --used_;
    }
  }
  uint16_t free() const { return sizeof(bytes_) - used_; }
  uint16_t overruns() const { return overruns_; }
 private:
  uint8_t bytes_[console::outputBufferBytes];
  uint16_t head_ = 0, tail_ = 0, used_ = 0, overruns_ = 0;
};
}
