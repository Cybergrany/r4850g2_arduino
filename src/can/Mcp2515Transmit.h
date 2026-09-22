#pragma once
#include "CanTransport.h"

namespace psu { namespace mcp2515 {
// MCP2515 datasheet DS20001801K, sections 3.4/3.6 and register 3-1.
constexpr uint8_t txControl = 0x30;
constexpr uint8_t txId = 0x31;
constexpr uint8_t txLength = 0x35;
constexpr uint8_t txData = 0x36;
constexpr uint8_t interruptFlags = 0x2c;
constexpr uint8_t txRequest = 0x08;
constexpr uint8_t txError = 0x10;
constexpr uint8_t txAborted = 0x40;
constexpr uint8_t txComplete = 0x04;

// The adapter exclusively owns TX buffer 0. arduino-CAN still initializes the
// controller and handles RX, but its unbounded endPacket() is not called.
// RegisterIo provides read/write/modify; the caller supplies time. Keeping this
// operation independent of Arduino lets tests simulate a permanently busy chip.
class Transmitter {
 public:
  TransmitState state() const { return state_; }
  template<class RegisterIo>
  bool start(RegisterIo& io, const CanFrame& f, uint32_t now) {
    if (state_ == TransmitState::Pending) return false;
    if (!f.extended || f.rtr || f.length != 8 || f.id > 0x1fffffffUL) return false;
    if (io.read(txControl) & txRequest) {
      // Never overwrite a buffer whose previous abort has not completed.
      io.modify(txControl, txRequest, 0);
      return false;
    }
    io.modify(interruptFlags, txComplete, 0);
    io.write(txId, uint8_t(f.id >> 21));
    io.write(txId + 1, uint8_t(((f.id >> 18) & 7) << 5) | 0x08 | uint8_t((f.id >> 16) & 3));
    io.write(txId + 2, uint8_t(f.id >> 8));
    io.write(txId + 3, uint8_t(f.id));
    io.write(txLength, f.length);
    for (uint8_t i = 0; i < f.length; ++i) io.write(txData + i, f.data[i]);
    started_ = now;
    io.write(txControl, txRequest);
    state_ = TransmitState::Pending;
    return true;
  }
  // Exactly one status check per service pass. Interrupts and the main loop
  // both remain available while arbitration/transmission/timeout proceeds.
  template<class RegisterIo>
  void poll(RegisterIo& io, uint32_t now, uint16_t timeoutMs) {
    if (state_ != TransmitState::Pending) return;
    const uint8_t state = io.read(txControl);
    if (!(state & txRequest)) {
      const bool complete = io.read(interruptFlags) & txComplete;
      io.modify(interruptFlags, txComplete, 0);
      // Lost arbitration can precede a successful retry; TX0IF proves completion.
      state_ = complete && !(state & (txError | txAborted)) ? TransmitState::Sent : TransmitState::Failed;
      return;
    }
    if ((state & txError) || uint32_t(now - started_) >= timeoutMs) {
      // Datasheet-supported per-buffer abort. Do not enter another unbounded
      // wait for abort completion. An already transmitting frame may finish.
      io.modify(txControl, txRequest, 0);
      state_ = TransmitState::Failed;
    }
  }
 private:
  TransmitState state_ = TransmitState::Idle;
  uint32_t started_ = 0;
};
} }
