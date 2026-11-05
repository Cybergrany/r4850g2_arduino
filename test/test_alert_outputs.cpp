#include "io/AlertOutputs.h"
#include <Arduino.h>
#include <cassert>
#include <vector>
struct Event { uint8_t kind, pin, value; };
std::vector<Event> events;
void digitalWrite(uint8_t pin,uint8_t value) { events.push_back({0,pin,value}); }
void pinMode(uint8_t pin,uint8_t value) { events.push_back({1,pin,value}); }
int main() {
  psu::AlertOutputs outputs; psu::AlertManager alerts; outputs.begin();
#if PSU_UI_DEBUG
  assert(events.empty());
#else
  assert(events.size()==4);
  assert(events[0].pin==28 && events[0].kind==0 && events[0].value==LOW);
  assert(events[1].pin==28 && events[1].kind==1 && events[1].value==OUTPUT);
  assert(events[2].pin==29 && events[2].kind==0 && events[2].value==LOW);
  assert(events[3].pin==29 && events[3].kind==1 && events[3].value==OUTPUT);
#endif
  for (uint8_t mask=0;mask<4;++mask) {
    alerts.set(0,mask&1 ? psu::AlertCode::Missing : psu::AlertCode::None,100+mask);
    alerts.set(1,mask&2 ? psu::AlertCode::Command : psu::AlertCode::None,100+mask);
    alerts.acknowledgeAll(); events.clear(); outputs.write(alerts);
    assert(alerts.warning()==bool(mask&1) && alerts.error()==bool(mask&2));
#if PSU_UI_DEBUG
    assert(events.empty());
#else
    assert(events.size()==2 && events[0].pin==28 && events[0].value==bool(mask&1));
    assert(events[1].pin==29 && events[1].value==bool(mask&2));
#endif
  }
}
