#include <CAN.h>
#include <cassert>
#include <vector>
#include <utility>
#include <iostream>
TestSpi SPI; TestCan CAN; TestHardwareSerial Serial;
std::vector<std::pair<int,int>> gpio;
uint32_t clockMs=0;
uint32_t millis() { return clockMs; }
uint32_t micros() { return clockMs*1000; }
void digitalWrite(uint8_t pin,uint8_t value) { gpio.emplace_back(pin,value); if(pin==10) SPI.select(value); }
void pinMode(uint8_t pin,uint8_t mode) { gpio.emplace_back(pin,10+mode); }
void attachInterrupt(uint8_t,void (*fn)(),int) { CAN.callback=fn; }
void setup(); void loop();
void command(const char* text) { Serial.input+=text; for(int n=0;n<100;++n) loop(); }
int cutoffLevel() { int value=-1; for(auto e:gpio) if(e.first==27 && e.second<2) value=e.second; return value; }
int main() {
  setup();
  assert(gpio.size()>=10 && gpio[0]==std::make_pair(27,0) && gpio[1]==std::make_pair(27,11));
  for(int pin : {22,25}) {
    bool high=false, output=false;
    for(auto e:gpio) if(e.first==pin) {
      if(e.second==1) high=true;
      if(e.second==11) { assert(high); output=true; }
    }
    assert(high && output);
  }
  command("ac on\n"); command("confirm yes\n");
  assert(cutoffLevel()==1);
  command("ac off\n"); assert(cutoffLevel()==0);
  std::cout<<"PASS: actual observer setup/loop holds cutoff LOW first, parks shared SPI devices, applies explicit AC commands\n";
}
