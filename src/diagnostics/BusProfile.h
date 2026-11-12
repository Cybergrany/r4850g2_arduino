#pragma once
#include "../config/BusProfileConfig.h"
#if PSU_BUS_PROFILE
#include "../can/CanTransport.h"
namespace psu {
enum class BusEvent : uint8_t {
  Boot, Negotiation, Quiet, Identity, Status, Ready, TxSetting, Ack,
  TxFailure, Route, Phase, Fan, Reconcile, Permit, Enable, Fault, Marker, AcReview
};
struct BusEventRecord {
  uint32_t ms, value;
  uint16_t detail;
  BusEvent kind;
  uint8_t target;
};
// Main-loop-only recorder. Counters wrap at 65536; event loss saturates and is
// reported separately from CAN RX loss. Existing control policy never reads it.
class BusProfile {
 public:
  void note(BusEvent kind, uint8_t target, uint32_t value, uint16_t detail, uint32_t now);
  void received(const CanFrame& frame, uint32_t now);
  void requested(const CanFrame& frame, uint8_t purpose, bool negotiating, uint32_t now);
  void completed(bool success, uint32_t now);
  void identity(uint8_t address, const uint8_t* sixBytes, uint32_t now);
  void negotiation(const CanFrame& frame, uint32_t now);
  bool pop(BusEventRecord& event);
  bool pending() const { return used_ != 0; }
  bool streaming() const { return streaming_; }
  void stream(bool enabled) { streaming_ = enabled; }
  bool summaryDue(uint32_t now) const { return uint32_t(now - summaryAt_) >= busprofile::summaryMs; }
  void summaryPrinted(uint32_t now) { summaryAt_ = now; }
  uint16_t lost() const { return lost_; }
  uint16_t infoTx = 0, dataTx = 0, describeTx = 0, settingTx = 0;
  uint16_t sent = 0, failed = 0, duringNegotiation = 0;
  uint16_t negotiationRx = 0, infoRx = 0, dataRx = 0, broadcastRx = 0, ackRx = 0, otherRx = 0;
 private:
  BusEventRecord events_[busprofile::eventSlots];
  uint32_t summaryAt_ = 0;
  uint16_t lost_ = 0;
  uint8_t head_ = 0, tail_ = 0, used_ = 0, txAddress_ = 0, txCommand_ = 0;
  bool streaming_ = true;
};
}
#endif
