#include "BusObserver.h"
#if PSU_CAN_OBSERVER
#include "../diagnostics/BusProfileText.h"
#include "../protocol/HuaweiProtocol.h"
#include "../config/ControlConfig.h"
#include "../util/FlashString.h"
#include <string.h>
namespace psu {
void BusObserver::begin(uint32_t now) {
  allowed_ = review_ = false;
  ready_ = can_.begin();
  profile_.note(BusEvent::Boot,0,ready_,1,now);
  message(Message::Help);
}
void BusObserver::message(Message value) {
  if (message_!=Message::None && replyLost_!=UINT16_MAX) ++replyLost_;
  message_=value;
}
void BusObserver::receive(const CanFrame& f, uint32_t now) {
  profile_.received(f,now);
  if (protocol::isNegotiation(f)) {
    negotiating_=true; lastNegotiation_=now;
    profile_.negotiation(f,now); // Standalone capture keeps every negotiation identity.
    return;
  }
  if (!protocol::isReply(f) && !protocol::isCurrentBroadcast(f)) return;
  const uint8_t addr=protocol::address(f.id); if (!addr) return;
  Address* a=nullptr;
  for (auto& entry:addresses_) if (entry.address==addr) { a=&entry; break; }
  if (!a) for (auto& entry:addresses_) if (!entry.address) { entry.address=addr; a=&entry; break; }
  if (!a) { if(addressOverflow_!=UINT16_MAX) ++addressOverflow_; return; }
  if (protocol::isCurrentBroadcast(f)) {
    if (!(a->flags&1) || a->ready!=f.data[3]) profile_.note(BusEvent::Ready,addr,f.data[3],0,now);
    a->ready=f.data[3]; a->flags|=1;
  } else if (protocol::command(f.id)==protocol::dataCommand && f.data[0]==1 && f.data[1]==0x83) {
    const uint32_t status=protocol::readBigEndian(f.data+4);
    if (!(a->flags&2) || a->status!=status) profile_.note(BusEvent::Status,addr,status,0,now);
    a->status=status; a->flags|=2;
  } else if (protocol::command(f.id)==protocol::infoCommand && f.data[0]==0 && f.data[1]==2) {
    if (!(a->flags&4) || memcmp(a->identity,f.data+2,6)) profile_.identity(addr,f.data+2,now);
    memcpy(a->identity,f.data+2,6); a->flags|=4;
  }
}
void BusObserver::execute(uint32_t now) {
  line_[length_]=0;
  if (!length_) { message(Message::Status); return; }
  if (!PSU_STRCMP(line_,"ac off")) {
    allowed_=review_=false; profile_.note(BusEvent::Permit,0,0,0,now); message(Message::Off);
  } else if (!PSU_STRCMP(line_,"ac on")) {
    if (!ready_ || control::chargeAllowPin<0) { message(Message::Invalid); return; }
    review_=true; reviewAt_=now; profile_.note(BusEvent::AcReview,0,1,0,now); message(Message::Review);
  } else if (!PSU_STRCMP(line_,"confirm yes")) {
    if (!review_ || !ready_ || uint32_t(now-reviewAt_)>=busprofile::confirmationMs) {
      review_=false; message(Message::Expired); return;
    }
    allowed_=true; review_=false; profile_.note(BusEvent::Permit,0,1,0,now); message(Message::Confirmed);
  } else if (!PSU_STRCMP(line_,"confirm no")) { review_=false; message(Message::Status); }
  else if (!PSU_STRCMP(line_,"help") || !PSU_STRCMP(line_,"hello")) message(Message::Help);
  else if (!PSU_STRCMP(line_,"status")) message(Message::Status);
  else if (!PSU_STRCMP(line_,"profile on")) { profile_.stream(true); message(Message::Status); }
  else if (!PSU_STRCMP(line_,"profile off")) { profile_.stream(false); message(Message::Status); }
  else if (!PSU_STRNCMP(line_,"mark ",5)) {
    uint32_t n=0; const char* p=line_+5;
    if (!*p) { message(Message::Invalid); return; }
    for (;*p;++p) {
      if (*p<'0' || *p>'9' || (n=n*10+*p-'0')>UINT16_MAX) { message(Message::Invalid); return; }
    }
    profile_.note(BusEvent::Marker,0,n,0,now); message(Message::Status);
  } else message(Message::Invalid);
}
void BusObserver::printMessage() {
  out_.println();
  switch(message_) {
    case Message::Help:
      out_.println(F("R4850 CAN OBSERVER BUS v1; no PSU commands or EEPROM access"));
      out_.println(F("help | status | ac on | confirm yes/no | ac off | profile on/off | mark N"));
      break;
    case Message::Review:
      out_.println(F("REVIEW: AC may energize DC using PSU defaults. Observer cannot hold zero current."));
      out_.println(F("Confirm with 'confirm yes' within 15s; otherwise 'confirm no'.")); break;
    case Message::Expired: out_.println(F("ERR no valid AC review; use ac on again")); break;
    case Message::Invalid: out_.println(F("ERR invalid command or CAN/cutoff unavailable; help")); break;
    case Message::Full: out_.println(F("ERR incomplete/overlong input discarded")); break;
    case Message::Reset: out_.println(F("Input/review reset; AC command unchanged")); break;
    case Message::Confirmed: out_.println(F("AC release confirmed; no CAN settings sent")); break;
    case Message::Off: out_.println(F("AC inhibit requested")); break;
    default: break;
  }
  out_.print(F("OBSERVER CAN=")); out_.print(ready_); out_.print(F(" AC=")); out_.print(allowed_ ? F("HIGH") : F("LOW"));
  out_.print(F(" table-loss=")); out_.print(addressOverflow_); out_.print(F(" reply-loss=")); out_.println(replyLost_);
  message_=Message::None;
}
void BusObserver::tick(uint32_t now) {
  if (ready_) can_.service(now);
  CanFrame frame;
  for (uint8_t n=0;ready_ && n<32 && can_.receive(frame);++n) receive(frame,now);
  if (negotiating_ && uint32_t(now-lastNegotiation_)>=control::availabilitySettleMs) {
    negotiating_=false; profile_.note(BusEvent::Quiet,0,0,0,now);
  }
  if (review_ && uint32_t(now-reviewAt_)>=busprofile::confirmationMs) review_=false;
  if (length_ && uint32_t(now-inputAt_)>=busprofile::inputExpiryMs) { length_=0; discard_=true; review_=false; }
  // Parse actions even with a disconnected/backpressured monitor. In particular
  // ac off cannot wait for output capacity. A clipped command is never executed.
  for (uint8_t n=0;n<32 && serial_.available();++n) {
    const int c=serial_.read(); if (c<0) break;
    if (c==3 || c==24) { length_=0; discard_=afterCr_=review_=false; message(Message::Reset); break; }
    if (afterCr_ && c=='\n') { afterCr_=false; continue; }
    afterCr_=c=='\r'; inputAt_=now;
    if (c=='\r' || c=='\n') {
      if (discard_) { review_=false; message(Message::Full); } else execute(now);
      length_=0; discard_=false; break;
    }
    if (discard_) continue;
    if (c==8 || c==127) { if(length_) --length_; continue; }
    if (c<32 || c>126 || length_==sizeof(line_)-1) { discard_=true; continue; }
    line_[length_++]=char(c);
    if (out_.free()>=busprofile::outputReserve) out_.write(uint8_t(c));
  }
  out_.drain(serial_);
  if (out_.free()<busprofile::outputReserve) return;
  if (message_!=Message::None) { printMessage(); return; }
  if (!profile_.streaming() || length_) return;
  if (profile_.summaryDue(now)) {
    BusReceiveStats stats; can_.busReceiveStats(stats); printBusSummary(out_,profile_,stats,now); profile_.summaryPrinted(now);
  } else { BusEventRecord event; if(profile_.pop(event)) printBusEvent(out_,event); }
}
}
#endif
