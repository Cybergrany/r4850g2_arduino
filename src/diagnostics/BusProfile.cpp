#include "BusProfile.h"
#if PSU_BUS_PROFILE
#include "../protocol/HuaweiProtocol.h"
namespace psu {
namespace {
uint32_t settingValue(const CanFrame& f) {
  return f.data[1] == protocol::FanMode || f.data[1] == protocol::Standby ? f.data[3] : protocol::readBigEndian(f.data + 4);
}
}
void BusProfile::note(BusEvent kind, uint8_t target, uint32_t value, uint16_t detail, uint32_t now) {
  if (used_ == busprofile::eventSlots) { if (lost_ != UINT16_MAX) ++lost_; return; }
  events_[head_] = {now, value, detail, kind, target};
  if (++head_ == busprofile::eventSlots) head_ = 0;
  ++used_;
}
bool BusProfile::pop(BusEventRecord& event) {
  if (!used_) return false;
  event = events_[tail_];
  if (++tail_ == busprofile::eventSlots) tail_ = 0;
  --used_; return true;
}
void BusProfile::identity(uint8_t address, const uint8_t* id, uint32_t now) {
  note(BusEvent::Identity,address,protocol::readBigEndian(id),uint16_t(id[4]) << 8 | id[5],now);
}
void BusProfile::negotiation(const CanFrame& f, uint32_t now) {
  // The CAN address is temporary; retain the six-byte initiating identity.
  note(BusEvent::Negotiation,protocol::address(f.id) | (f.data[1]==2 ? 0x80 : 0),
      protocol::readBigEndian(f.data+2),uint16_t(f.data[6]) << 8 | f.data[7],now);
}
void BusProfile::received(const CanFrame& f, uint32_t now) {
  if (protocol::isNegotiation(f)) ++negotiationRx;
  else if (protocol::isCurrentBroadcast(f)) ++broadcastRx;
  else if (protocol::isReply(f)) {
    const uint8_t cmd = protocol::command(f.id);
    if (cmd == protocol::infoCommand) ++infoRx;
    else if (cmd == protocol::dataCommand) ++dataRx;
    else if (cmd == protocol::setCommand) {
      ++ackRx;
      note(BusEvent::Ack,protocol::address(f.id),settingValue(f),uint16_t(f.data[0]) << 8 | f.data[1],now);
    } else ++otherRx;
  } else ++otherRx;
}
void BusProfile::requested(const CanFrame& f, uint8_t purpose, bool negotiating, uint32_t now) {
  txAddress_ = protocol::address(f.id); txCommand_ = protocol::command(f.id);
  if (negotiating) ++duringNegotiation;
  if (txCommand_ == protocol::infoCommand) ++infoTx;
  else if (txCommand_ == protocol::dataCommand) ++dataTx;
  else if (txCommand_ == protocol::descriptionCommand) ++describeTx;
  else if (txCommand_ == protocol::setCommand) {
    ++settingTx;
    note(BusEvent::TxSetting,txAddress_,settingValue(f),uint16_t(purpose) << 8 | f.data[1],now);
  }
}
void BusProfile::completed(bool success, uint32_t now) {
  if (success) ++sent;
  else { ++failed; note(BusEvent::TxFailure,txAddress_,0,txCommand_,now); }
}
}
#endif
