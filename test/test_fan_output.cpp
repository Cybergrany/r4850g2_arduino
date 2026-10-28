#include "io/FanOutput.h"
#include <Arduino.h>
#include <cassert>
#include <vector>

struct Write { char kind; uint8_t pin; int value; };
std::vector<Write> writes;
void digitalWrite(uint8_t pin, uint8_t value) { writes.push_back({'D',pin,value}); }
void pinMode(uint8_t pin, uint8_t mode) { writes.push_back({'M',pin,mode}); }
void analogWrite(uint8_t pin, int value) { writes.push_back({'A',pin,value}); }
int main() {
  psu::FanOutput fan; fan.begin();
  assert(writes.size()==2 && writes[0].kind=='D' && writes[1].kind=='M');
  assert(writes[0].pin==6 && writes[0].value==(psu::control::fanPwmInverted ? LOW : HIGH));
  assert(writes[1].pin==6 && writes[1].value==OUTPUT);
  fan.write(255); assert(writes.size()==2); // Full cooling already asserted at boot.
  for(const uint8_t demand : {0,128,255}) {
    fan.write(demand);
    assert(writes.back().kind=='A' && writes.back().pin==6);
    assert(writes.back().value==(psu::control::fanPwmInverted ? 255-demand : demand));
    const auto before=writes.size(); fan.write(demand); assert(writes.size()==before);
  }
  assert(!fan.due(99) && fan.due(100) && !fan.due(199) && fan.due(200));
  assert(fan.due(UINT32_MAX-50) && !fan.due(25) && fan.due(50));
}
