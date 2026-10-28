#include "io/ChargeAllowOutput.h"
#include <Arduino.h>
#include <cassert>
#include <vector>
struct Event { uint8_t kind, pin, value; };
std::vector<Event> events;
void digitalWrite(uint8_t pin, uint8_t value) { events.push_back({0,pin,value}); }
void pinMode(uint8_t pin, uint8_t value) { events.push_back({1,pin,value}); }
int main() {
  psu::ChargeAllowOutput output; output.begin();
  assert(events.size()==2 && events[0].kind==0 && events[0].value==LOW);
  assert(events[1].kind==1 && events[1].value==OUTPUT);
  for (const auto& e:events) assert(e.pin==27);
  output.write(false); assert(events.size()==2);
  output.write(true); assert(events.size()==3 && events.back().value==HIGH);
  output.write(true); assert(events.size()==3);
  output.write(false); assert(events.size()==4 && events.back().value==LOW);
  output.begin(); assert(events.size()==6 && events[4].value==LOW);
}
