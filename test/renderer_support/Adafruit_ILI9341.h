#pragma once
#include <Arduino.h>
#include <array>
#include <cassert>
#include <vector>
namespace raster {
struct Transfer { uint16_t x,y,w,h; uint32_t us; };
extern std::array<uint16_t,320*240> pixels;
extern std::vector<Transfer> transfers;
extern uint64_t clockUs;
extern uint32_t spiHz, yields, cpuCost, wireScale;
extern uint32_t solidWrites, pixelWrites;
extern void (*afterTransfer)();
extern bool open;
}
constexpr uint8_t LOW=0, HIGH=1, OUTPUT=1;
inline void digitalWrite(uint8_t, uint8_t) {}
inline void pinMode(uint8_t, uint8_t) {}
inline void analogWrite(uint8_t, int) {}
class Adafruit_ILI9341 {
 public:
  Adafruit_ILI9341(uint8_t,uint8_t,int8_t) {}
  void begin(uint32_t hz) { raster::spiHz=hz; }
  void setSPISpeed(uint32_t hz) { raster::spiHz=hz; }
  uint8_t readcommand8(uint8_t command,uint8_t i) { assert(command==0xd3); return i==2?0x93:0x41; }
  void setRotation(uint8_t) {}
  void fillScreen(uint16_t color) { raster::pixels.fill(color); }
  void startWrite() { assert(!raster::open); raster::open=true; }
  void setAddrWindow(uint16_t x,uint16_t y,uint16_t w,uint16_t h) { assert(raster::open); current_={x,y,w,h,0}; }
  void writePixels(uint16_t* data,uint32_t count) {
    ++raster::pixelWrites; transfer(data,count);
  }
  void writeColor(uint16_t color,uint32_t count) {
    ++raster::solidWrites; assert(count<=48);
    uint16_t data[48]; for(uint32_t i=0;i<count;++i) data[i]=color;
    transfer(data,count);
  }
  void endWrite() { assert(raster::open); raster::open=false; raster::transfers.push_back(current_); ++raster::yields; if(raster::afterTransfer) raster::afterTransfer(); }
 private:
  void transfer(const uint16_t* data,uint32_t count) {
    assert(raster::open && count==uint32_t(current_.w)*current_.h);
    assert(current_.x+current_.w<=320 && current_.y+current_.h<=240);
    for(uint16_t y=0;y<current_.h;++y) for(uint16_t x=0;x<current_.w;++x)
      raster::pixels[(current_.y+y)*320+current_.x+x]=data[y*current_.w+x];
    current_.us=((uint64_t(count)*2+11)*8000000+raster::spiHz-1)/raster::spiHz*raster::wireScale;
    raster::clockUs+=current_.us;
  }
  raster::Transfer current_={};
};
