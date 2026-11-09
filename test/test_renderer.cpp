#include <iostream>
#include <stdexcept>
#include <limits>
#include "ui/Ili9341Display.h"
#include "config/BoardConfig.h"
#include <glcdfont.c>
#define CHECK(x) do { if(!(x)) throw std::runtime_error(std::string(__FILE__)+":"+std::to_string(__LINE__)+" " #x); } while(0)
namespace raster {
std::array<uint16_t,320*240> pixels;
std::vector<Transfer> transfers;
uint64_t clockUs=0;
uint32_t spiHz=0,yields=0,cpuCost=1,wireScale=1;
void (*afterTransfer)()=nullptr;
bool open=false;
}
uint32_t micros() { const auto now=raster::clockUs; raster::clockUs+=raster::cpuCost; return now; }
uint32_t millis() { return raster::clockUs/1000; }
using namespace psu;
uint16_t color(uint8_t style) { const uint16_t palette[]={0xefbd,0xadf5,0x6f2f,0xfdcd,0xfa69,0x8ef6,0xefbd,0xefbd}; return palette[style&7]; }
void imageMatches(const UiFrame& frame) {
  for(uint16_t i=0;i<UiFrame::cells;++i) {
    const auto cell=frame.cell(i); uint8_t code=uint8_t(cell.character),size=2,ox=0,oy=0;
    if(code>=128 && code<176) {
      const uint8_t n=(code-128)/4,q=(code-128)%4; code=n<10?'0'+n:n==10?'.':'-'; size=4; ox=q%2*12; oy=q/2*16;
    }
    for(uint8_t y=0;y<16;++y) for(uint8_t x=0;x<12;++x) {
      // Reference draws the source font onto a clipped canvas, independently of
      // the production row-mask and changed-pixel algorithm.
      bool set=false;
      for(uint8_t gx=0;gx<5;++gx) for(uint8_t gy=0;gy<8;++gy)
        if((font[code*5+gx]&(1<<gy)) && x+ox>=gx*size && x+ox<(gx+1)*size && y+oy>=gy*size && y+oy<(gy+1)*size) set=true;
      const auto expected=set?color(cell.style):(cell.style&selectedStyle?0x21e5:0x10c2);
      CHECK(raster::pixels[(i/26*16+y)*320+4+i%26*12+x]==expected);
    }
  }
}
void step(Ili9341Display& display,const UiFrame& frame) {
  const size_t before=raster::transfers.size(); const auto start=raster::clockUs;
  display.service(frame); CHECK(!raster::open);
  CHECK(raster::transfers.size()-before<=ui::burstsPerTick(board::displaySpiHz));
  uint32_t biggest=0;
  for(size_t i=before;i<raster::transfers.size();++i) {
    const auto t=raster::transfers[i]; CHECK(t.w==12 && t.h<=ui::burstRows(board::displaySpiHz));
    if(t.us>biggest) biggest=t.us;
  }
  CHECK(uint32_t(raster::clockUs-start)<=ui::renderBudgetUs+biggest+20*raster::cpuCost);
}
void finish(Ili9341Display& display,const UiFrame& frame) {
  for(unsigned i=0;display.pending() && i<20000;++i) step(display,frame);
  CHECK(!display.pending()); imageMatches(frame);
}
UiFrame* interruptedFrame=nullptr;
Ili9341Display* interruptedDisplay=nullptr;
void changeDuringTransfer() {
  CHECK(!raster::open); // An ISR can run after every completed transaction.
  interruptedFrame->cell(0,{'Z',Accent}); interruptedFrame->largeNumber(8,"-0.5",Warning);
  interruptedDisplay->invalidate(); raster::afterTransfer=nullptr;
}
void run() {
  Ili9341Display display; UiFrame frame; frame.clear(); CHECK(display.begin());
  display.invalidate(); finish(display,frame); CHECK(raster::transfers.empty());
  // Invisible style metadata changes must not touch SPI, including the blank
  // top/bottom padding of filled buttons.
  frame.text(1,0,26,"",Accent); display.invalidate(); finish(display,frame); CHECK(raster::transfers.empty());
  frame.text(2,0,26,"Settings",Text|selectedStyle);
  frame.text(3,0,26,"",Text|selectedStyle); frame.text(4,0,26,"",Text|selectedStyle);
  display.invalidate(); finish(display,frame);
  const size_t before=raster::transfers.size();
  for(uint8_t row=2;row<=4;++row) frame.text(row,0,26,row==2?"Settings":"",Accent|selectedStyle);
  display.invalidate(); finish(display,frame);
  const size_t highlight=raster::transfers.size()-before;
  CHECK(highlight>0 && highlight<26*3*16/ui::burstRows(board::displaySpiHz)/4);
  std::cout<<board::displaySpiHz<<" Hz highlight transfers="<<highlight<<" scene="<<display.statistics().sceneMs<<" ms (synthetic SPI only)\n";
  // Full ASCII set + every large digit quadrant retain the former GFX appearance.
  frame.clear(); for(uint8_t ch=32;ch<127;++ch) frame.cell(ch-32,{char(ch),uint8_t(ch%6|((ch&1)?selectedStyle:0))});
  frame.largeNumber(8,"-54.10",Accent); display.invalidate(); finish(display,frame);
  // Foreground-only differences draw no blank glyph scanlines.
  const size_t start=raster::transfers.size(); frame.cell(0,{' ',Warning}); display.invalidate(); finish(display,frame); CHECK(raster::transfers.size()==start);
  // Replace a scene while a selected cell is only partly transferred; reverse
  // rapidly, including reverting to the original scene, and verify every pixel.
  for(unsigned i=0;i<90;++i) {
    frame.cell(0,{char(i%2?'8':' '),uint8_t(i%3?Text:Accent|selectedStyle)});
    frame.largeNumber(8,i%2?"99.9":"0.5",i%3?Text:Accent);
    display.invalidate(); step(display,frame);
  }
  finish(display,frame);
  interruptedFrame=&frame; interruptedDisplay=&display; raster::afterTransfer=changeDuringTransfer;
  frame.cell(0,{'W',selectedStyle|Text}); display.invalidate(); step(display,frame); finish(display,frame);
  CHECK(!raster::afterTransfer);
  CHECK(raster::yields==raster::transfers.size());
  const size_t idle=raster::transfers.size(); for(unsigned i=0;i<100;++i) step(display,frame); CHECK(raster::transfers.size()==idle);
  // micros rollover and explicit overrun visibility: budget checked between
  // bounded units, never described as a hard deadline.
  raster::clockUs=std::numeric_limits<uint32_t>::max()-20;
  frame.text(0,0,26,"Rollover",selectedStyle|Error); display.invalidate(); step(display,frame); finish(display,frame);
  raster::clockUs=(uint64_t(std::numeric_limits<uint32_t>::max())-3)*1000;
  frame.text(0,0,26,"Millis rollover",Accent); display.invalidate(); finish(display,frame);
  CHECK(display.statistics().sceneMs<1000);
  raster::wireScale=20; frame.text(0,0,26,"Slow transaction",selectedStyle|Good); display.invalidate(); finish(display,frame);
  CHECK(display.statistics().renderMaxUs>ui::renderBudgetUs && display.statistics().transactionMaxUs>ui::renderBudgetUs);
}
int main() { try { run(); } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; } }
