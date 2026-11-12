#include "BusProfileText.h"
#if PSU_BUS_PROFILE
#if !PSU_CAN_OBSERVER
#include "../ui/UiText.h"
#endif
namespace psu {
namespace {
const __FlashStringHelper* eventName(BusEvent kind) {
  switch (kind) {
    case BusEvent::Boot: return F("boot");
    case BusEvent::Negotiation: return F("negotiation");
    case BusEvent::Quiet: return F("negotiation-end");
    case BusEvent::Identity: return F("identity");
    case BusEvent::Status: return F("status");
    case BusEvent::Ready: return F("broadcast-ready");
    case BusEvent::TxSetting: return F("tx-setting");
    case BusEvent::Ack: return F("rx-ack");
    case BusEvent::TxFailure: return F("tx-failure");
    case BusEvent::Route: return F("route-reset");
    case BusEvent::Phase: return F("phase");
    case BusEvent::Fan: return F("fan-demand");
    case BusEvent::Reconcile: return F("reconcile");
    case BusEvent::Permit: return F("ac-permit");
    case BusEvent::Enable: return F("enable");
    case BusEvent::Fault: return F("fault");
    case BusEvent::Marker: return F("marker");
    case BusEvent::AcReview: return F("ac-review");
  }
  return F("unknown");
}
void hexBytes(Print& out, uint32_t value, uint8_t bytes) {
  for (uint8_t i=bytes; i; --i) {
    const uint8_t b = uint8_t(value >> ((i-1)*8));
    if (b < 16) out.print('0');
    out.print(b,HEX);
  }
}
}
void printBusEvent(Print& out, const BusEventRecord& e) {
  out.print(F("BUS E t=")); out.print(e.ms); out.print(' '); out.print(eventName(e.kind));
  const bool slot = e.kind==BusEvent::Route || e.kind==BusEvent::Phase || e.kind==BusEvent::Fan || e.kind==BusEvent::Fault;
  out.print(slot ? F(" slot=") : F(" addr=")); out.print(e.kind==BusEvent::Negotiation ? uint8_t(e.target&0x7f) : e.target);
  if (e.kind==BusEvent::Identity || e.kind==BusEvent::Negotiation) {
    out.print(F(" id=")); hexBytes(out,e.value,4); hexBytes(out,e.detail,2);
    if (e.kind==BusEvent::Negotiation) out.print(e.target&0x80 ? F(" type=30-02") : F(" type=30-01"));
  } else {
    out.print(F(" value=")); out.print(e.value); out.print(F(" detail=")); out.print(e.detail);
    if (e.kind==BusEvent::Status) {
      out.print(F(" raw=0x")); out.print(e.value,HEX);
      out.print(F(" reported-fan=")); out.print(e.value & (uint32_t(1)<<10) ? F("MAX") : F("AUTO"));
    }
#if !PSU_CAN_OBSERVER
    if (e.kind==BusEvent::Route) { out.print(F(" reason=")); out.print(issueName(Issue(e.detail>>8))); }
    if (e.kind==BusEvent::Fan) {
      out.print(F(" mode=")); out.print(fanName(FanMode(e.detail>>8)));
      out.print(F(" reason=")); out.print(fanReasonName(FanReason(uint8_t(e.detail))));
    }
    if (e.kind==BusEvent::Phase) { out.print(F(" to=")); out.print(phaseName(ControlPhase(uint8_t(e.detail)))); }
#endif
  }
  out.println();
}
void printBusSummary(Print& out, const BusProfile& p, const BusReceiveStats& r, uint32_t now) {
  // Fixed CSV schema, <256 bytes even when every counter has its largest value.
  // RX/drop/invalid are uint32; class/TX counters wrap uint16; maxima persist.
  out.print(F("BUS S,")); out.print(now);
  out.print(','); out.print(r.received); out.print(','); out.print(r.dropped); out.print(','); out.print(r.invalid);
  out.print(','); out.print(r.hardwareOverflows); out.print(','); out.print(r.highWater);
  out.print(','); out.print(r.maxServiceGapUs); out.print(','); out.print(r.maxQueueAgeMs);
  out.print(','); out.print(r.negotiationFrames);
  out.print(','); out.print(p.infoTx); out.print(','); out.print(p.dataTx); out.print(','); out.print(p.describeTx); out.print(','); out.print(p.settingTx);
  out.print(','); out.print(p.sent); out.print(','); out.print(p.failed); out.print(','); out.print(p.duringNegotiation);
  out.print(','); out.print(p.negotiationRx); out.print(','); out.print(p.infoRx); out.print(','); out.print(p.dataRx);
  out.print(','); out.print(p.broadcastRx); out.print(','); out.print(p.ackRx); out.print(','); out.print(p.otherRx);
  out.print(','); out.print(p.lost());
  out.print(','); out.print(r.errorFlags); out.print(','); out.print(r.txErrorCount); out.print(','); out.println(r.rxErrorCount);
}
}
#endif
