#pragma once
#include <Arduino.h>
#include <util/delay_basic.h>
// CPU/raster benchmark with nominal 8 MHz SPI wire cost, not a hardware model.
class Adafruit_ILI9341 {
 public:
  Adafruit_ILI9341(int8_t,int8_t,int8_t) {}
  void begin(uint32_t) {}
  void setRotation(uint8_t) {}
  void setSPISpeed(uint32_t) {}
  uint8_t readcommand8(uint8_t,uint8_t index) { return index==2?0x93:0x41; }
  void fillScreen(uint16_t) {}
  void startWrite() {}
  void setAddrWindow(uint16_t,uint16_t,uint16_t,uint16_t) { _delay_loop_2(44); }
  void writePixels(uint16_t*,uint32_t count) { _delay_loop_2(uint16_t(count)*8); }
  void writeColor(uint16_t,uint32_t count) { _delay_loop_2(uint16_t(count)*8); }
  void endWrite() {}
};
