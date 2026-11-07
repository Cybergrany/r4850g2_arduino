#include "io/AlertOutputs.h"
#include "io/PsuActiveOutput.h"
#include <Arduino.h>
#include <cassert>
#include <vector>
struct Event { uint8_t kind, pin, value; };
std::vector<Event> events;
void digitalWrite(uint8_t pin,uint8_t value) { events.push_back({0,pin,value}); }
void pinMode(uint8_t pin,uint8_t value) { events.push_back({1,pin,value}); }
int main() {
  psu::AlertOutputs outputs; psu::AlertManager alerts; outputs.begin();
  psu::PsuActiveOutput active; active.begin();
#if PSU_UI_DEBUG
  assert(events.empty());
#else
  assert(events.size()==(PSU_ACTIVE_PIN >= 0 ? 6 : 4));
  assert(events[0].pin==28 && events[0].kind==0 && events[0].value==LOW);
  assert(events[1].pin==28 && events[1].kind==1 && events[1].value==OUTPUT);
  assert(events[2].pin==29 && events[2].kind==0 && events[2].value==LOW);
  assert(events[3].pin==29 && events[3].kind==1 && events[3].value==OUTPUT);
#if PSU_ACTIVE_PIN >= 0
  assert(events[4].pin==30 && events[4].kind==0 && events[4].value==LOW);
  assert(events[5].pin==30 && events[5].kind==1 && events[5].value==OUTPUT);
#endif
#endif
  for (uint8_t mask=0;mask<8;++mask) {
    alerts.set(0,mask&1 ? psu::AlertCode::Missing : psu::AlertCode::None,100+mask);
    alerts.set(1,mask&2 ? psu::AlertCode::Command : psu::AlertCode::None,100+mask);
    alerts.acknowledgeAll(); events.clear(); outputs.write(alerts);
    active.write(mask&4);
    assert(alerts.warning()==bool(mask&1) && alerts.error()==bool(mask&2));
#if PSU_UI_DEBUG
    assert(events.empty());
#else
    assert(events.size()==(PSU_ACTIVE_PIN >= 0 ? 3 : 2) && events[0].pin==28 && events[0].value==bool(mask&1));
    assert(events[1].pin==29 && events[1].value==bool(mask&2));
#if PSU_ACTIVE_PIN >= 0
    assert(events[2].pin==30 && events[2].kind==0 && events[2].value==bool(mask&4));
#endif
#endif
  }
}
