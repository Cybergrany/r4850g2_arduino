#pragma once
#include <Arduino.h>
#include "can/CanTransport.h"
#include <cassert>
#include <vector>
constexpr uint8_t MSBFIRST=1, SPI_MODE0=0;
constexpr uint8_t INPUT=0;
inline uint8_t digitalPinToInterrupt(uint8_t pin) { return pin; }
void attachInterrupt(uint8_t, void (*)(), int);
void detachInterrupt(uint8_t);
int digitalRead(uint8_t);
struct SPISettings { SPISettings(uint32_t,uint8_t,uint8_t) {} };
struct TestSpi {
  unsigned operations=0;
  bool open=false, selected=false, registered=false;
  bool heldLow=false;
  uint8_t bytes=0;
  void (*boundary)()=nullptr;
  void (*duringTransfer)()=nullptr;
  std::vector<uint8_t> bursts, lengths;
  uint8_t registers[128]={}, phase=0, command=0, address=0, mask=0;
  void usingInterrupt(uint8_t pin) { assert(pin==2); registered=true; }
  void beginTransaction(SPISettings) { assert(!open); open=true; ++operations; phase=0; bytes=0; }
  void endTransaction() {
    assert(open && !selected); open=false; lengths.push_back(bytes);
    if (boundary) boundary();
  }
  void select(uint8_t level) {
    if (!level) { assert(open && !selected); selected=true; }
    else {
      assert(open && selected); selected=false;
      if (command==0x90 || command==0x94) {
        registers[0x2c] &= uint8_t(~(command==0x90 ? 1 : 2)); bursts.push_back(command);
      }
    }
  }
  uint8_t transfer(uint8_t value) {
    assert(open && selected); ++bytes;
    if (duringTransfer) duringTransfer();
    if (!phase++) { command=value; if (command==0x90 || command==0x94) address=command==0x90 ? 0x61 : 0x71; return 0; }
    if (command==0x90 || command==0x94) return registers[address++];
    if (phase==2) { address=value; return 0; }
    if (command==3) return registers[address++];
    if (command==2) registers[address++]=value;
    if (command==5) {
      if (phase==3) mask=value;
      else registers[address]=(registers[address]&~mask)|(value&mask);
    }
    return 0;
  }
  void load(const psu::CanFrame& frame, uint8_t buffer) {
    assert(buffer<2 && !(registers[0x2c] & (1<<buffer)));
    uint8_t* r=registers+(buffer ? 0x71 : 0x61);
    r[0]=frame.id>>21;
    r[1]=((frame.id>>13)&0xe0)|((frame.id>>16)&3)|(frame.extended ? 8 : 0);
    r[2]=frame.id>>8; r[3]=frame.id;
    r[4]=frame.length|(frame.rtr ? 0x40 : 0);
    for (uint8_t i=0;i<8;++i) r[i+5]=frame.data[i];
    registers[0x2c] |= 1<<buffer;
  }
  bool interruptLow() const { return heldLow || (registers[0x2c] & 3); }
};
extern TestSpi SPI;
struct TestCan {
  void (*callback)()=nullptr;
  void (*onArm)()=nullptr;
  unsigned interrupts=0;
  void setPins(uint8_t,uint8_t) {}
  void setClockFrequency(uint32_t) {}
  void setSPIFrequency(uint32_t) {}
  bool begin(uint32_t) { return true; }
  void dispatch() {
    unsigned budget=8;
    // Model LOW-level retriggering, not merely one synthetic function call.
    while (callback && SPI.interruptLow()) {
      assert(budget-- && "CAN interrupt storm starves foreground/Timer0");
      ++interrupts; callback();
    }
  }
  void arm(void (*fn)()) { callback=fn; if (onArm) onArm(); }
  void inject(const psu::CanFrame& value) { SPI.load(value,0); assert(SPI.registered); dispatch(); }
};
extern TestCan CAN;
