#pragma once
#include "../config/BuildOptions.h"
#if PSU_CAN_OBSERVER
#include "../diagnostics/BusProfile.h"
#include "../ui/BufferedOutput.h"
namespace psu {
// No controller/configuration/storage dependency. Hardware writes only the AC
// permissive selected by allowAc(); there is no application CAN transmit path.
class BusObserver {
 public:
  BusObserver(CanTransport& can, Stream& serial) : can_(can), serial_(serial) {}
  void begin(uint32_t now);
  void tick(uint32_t now);
  bool allowAc() const { return allowed_; }
  bool ready() const { return ready_; }
  BusProfile& profile() { return profile_; }
 private:
  struct Address {
    uint32_t status = 0;
    uint8_t identity[6] = {};
    uint8_t address = 0, flags = 0, ready = 0;
  } addresses_[busprofile::observerAddresses];
  enum class Message : uint8_t { None, Help, Status, Review, Confirmed, Off, Invalid, Expired, Full, Reset };
  void receive(const CanFrame& frame, uint32_t now);
  void execute(uint32_t now);
  void message(Message value);
  void printMessage();
  CanTransport& can_;
  Stream& serial_;
  BufferedOutput out_;
  BusProfile profile_;
  char line_[32] = {};
  uint32_t reviewAt_ = 0, inputAt_ = 0, lastNegotiation_ = 0;
  uint16_t addressOverflow_ = 0, replyLost_ = 0;
  uint8_t length_ = 0;
  Message message_ = Message::None;
  bool ready_ = false, allowed_ = false, review_ = false, discard_ = false, afterCr_ = false, negotiating_ = false;
};
}
#endif
