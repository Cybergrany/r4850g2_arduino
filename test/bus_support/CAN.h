#pragma once
#include <Arduino.h>
#include "can/CanTransport.h"
constexpr uint8_t MSBFIRST=1, SPI_MODE0=0;
struct SPISettings { SPISettings(uint32_t,uint8_t,uint8_t) {} };
struct TestSpi {
  unsigned operations=0;
  uint8_t registers[128]={}, phase=0, command=0, address=0, mask=0;
  void beginTransaction(SPISettings) { ++operations; phase=0; }
  void endTransaction() {}
  uint8_t transfer(uint8_t value) {
    if (!phase++) { command=value; return 0; }
    if (phase==2) { address=value; return 0; }
    if (command==3) return registers[address++];
    if (command==2) registers[address++]=value;
    if (command==5) {
      if (phase==3) mask=value;
      else registers[address]=(registers[address]&~mask)|(value&mask);
    }
    return 0;
  }
};
extern TestSpi SPI;
struct TestCan {
  psu::CanFrame frame={}; uint8_t offset=0; void (*callback)(int)=nullptr;
  void setPins(uint8_t,uint8_t) {}
  void setClockFrequency(uint32_t) {}
  void setSPIFrequency(uint32_t) {}
  bool begin(uint32_t) { return true; }
  void onReceive(void (*fn)(int)) { callback=fn; }
  bool packetExtended() const { return frame.extended; }
  bool packetRtr() const { return frame.rtr; }
  uint32_t packetId() const { return frame.id; }
  int available() const { return frame.length-offset; }
  int read() { return frame.data[offset++]; }
  void inject(psu::CanFrame value) { frame=value; offset=0; callback(frame.length); }
};
extern TestCan CAN;
