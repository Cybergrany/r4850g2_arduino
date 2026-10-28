#include "io/FanOutput.h"
#include <Arduino.h>
#include <cassert>
#include <vector>

struct Write { char kind; uint8_t pin; int value; };
std::vector<Write> writes;
volatile uint8_t TCCR4A=0xa5, TCCR4B=0xa5, TIMSK4=0xa5, TCCR5A=0xa5, TCCR5B=0xa5, TIMSK5=0xa5;
volatile uint16_t TCNT4=0xcafe, ICR4=0xcafe, OCR4A=0xcafe, OCR4B=0xcafe, OCR4C=0xcafe;
volatile uint16_t TCNT5=0xcafe, ICR5=0xcafe, OCR5A=0xcafe, OCR5B=0xcafe, OCR5C=0xcafe;
void digitalWrite(uint8_t pin, uint8_t value) { writes.push_back({'D',pin,value}); }
void pinMode(uint8_t pin, uint8_t mode) { writes.push_back({'M',pin,mode}); }
int main() {
  constexpr auto pin=psu::control::fanPwmPin;
  constexpr bool timer4=pin<=8;
  constexpr uint8_t channel=timer4 ? pin-6 : 46-pin;
  volatile uint16_t* compare[3]={timer4 ? &OCR4A : &OCR5A, timer4 ? &OCR4B : &OCR5B, timer4 ? &OCR4C : &OCR5C};
  psu::FanOutput fan; fan.begin();
  assert(writes.size()==2 && writes[0].kind=='D' && writes[1].kind=='M');
  assert(writes[0].pin==pin && writes[0].value==(psu::control::fanPwmInverted ? LOW : HIGH));
  assert(writes[1].pin==pin && writes[1].value==OUTPUT);
  assert((timer4 ? ICR4 : ICR5)==320 && F_CPU/(2*(timer4 ? ICR4 : ICR5))==25000);
  assert((timer4 ? TCCR4A : TCCR5A)==(0x02 | (0x80 >> (2*channel)))); // WGM=10, noninverting OC channel.
  assert((timer4 ? TCCR4B : TCCR5B)==0x11 && (timer4 ? TIMSK4 : TIMSK5)==0);
  assert((timer4 ? TCNT4 : TCNT5)==0);
  assert((timer4 ? TCCR5A : TCCR4A)==0xa5 && (timer4 ? TCCR5B : TCCR4B)==0xa5);
  assert((timer4 ? TIMSK5 : TIMSK4)==0xa5 && (timer4 ? ICR5 : ICR4)==0xcafe);
  for(uint8_t i=0;i<3;++i) if(i!=channel) assert(*compare[i]==0xcafe);
  assert(*compare[channel]==(psu::control::fanPwmInverted ? 0 : 320));
  fan.write(255); assert(writes.size()==2); // Full cooling already asserted at boot.
  for(unsigned n=0;n<=255;++n) {
    const uint8_t demand=n;
    fan.write(demand);
    const uint16_t expected=((psu::control::fanPwmInverted ? 255-n : n)*320+127)/255;
    assert(*compare[channel]==expected && writes.size()==2);
    *compare[channel]=0xcafe; fan.write(demand); assert(*compare[channel]==0xcafe); // No duplicate writes.
    *compare[channel]=expected;
  }
  assert(!fan.due(99) && fan.due(100) && !fan.due(199) && fan.due(200));
  assert(fan.due(UINT32_MAX-50) && !fan.due(25) && fan.due(50));
}
