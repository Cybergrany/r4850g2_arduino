#include <iostream>
#include <stdexcept>
#include <limits>
#include <fstream>
#include <sstream>
#include "ui/Ili9341Display.h"
#include "config/BoardConfig.h"
#include <glcdfont.c>
#define CHECK(x) do { if(!(x)) throw std::runtime_error(std::string(__FILE__)+":"+std::to_string(__LINE__)+" " #x); } while(0)
namespace raster {
std::array<uint16_t,320*240> pixels;
std::vector<Transfer> transfers;
uint64_t clockUs=0;
uint32_t spiHz=0,yields=0,cpuCost=1,wireScale=1;
uint32_t solidWrites=0,pixelWrites=0;
void (*afterTransfer)()=nullptr;
bool open=false;
}
uint32_t micros() { const auto now=raster::clockUs; raster::clockUs+=raster::cpuCost; return now; }
uint32_t millis() { return raster::clockUs/1000; }
using namespace psu;
uint16_t color(uint8_t style) { const uint16_t palette[]={0xefbd,0xadf5,0x6f2f,0xfdcd,0xfa69,0x8ef6,0xefbd,0xefbd}; return palette[style&7]; }
void imageMatches(const UiFrame& frame) {
  std::array<uint16_t,320*240> expected; expected.fill(ui::backgroundColor);
  // Independent pixel reference: complete rectangles, then the classic font.
  for (unsigned focused=0;focused<2;++focused) for (unsigned i=0;i<frame.outlineCount();++i) {
    const auto o=frame.outline(i); if (!o.valid() || o.focused()!=bool(focused)) continue;
    const int x0=2+12*o.column(), x1=x0+12*o.width()+4;
    const int y0=std::max(0,16*int(o.row())-2), y1=std::min(240,16*(int(o.row())+o.height())+2);
    for (int y=y0;y<y1;++y) for (int x=x0;x<x1;++x)
      if (x<x0+2 || x>=x1-2 || y<y0+2 || y>=y1-2) expected[y*320+x]=focused ? ui::focusColor : ui::outlineColor;
  }
  for(uint16_t i=0;i<UiFrame::cells;++i) {
    const auto cell=frame.cell(i); uint8_t code=uint8_t(cell.character),size=2,ox=0,oy=0;
    if(code>=128 && code<176) {
      const uint8_t n=(code-128)/4,q=(code-128)%4; code=n<10?'0'+n:n==10?'.':'-'; size=4; ox=q%2*12; oy=q/2*16;
    }
    bool ink[16][12]={};
    for(uint8_t y=0;y<16;++y) for(uint8_t x=0;x<12;++x)
      for(uint8_t gx=0;gx<5;++gx) for(uint8_t gy=0;gy<8;++gy)
        if((font[code*5+gx]&(1<<gy)) && x+ox>=gx*size && x+ox<(gx+1)*size && y+oy>=gy*size && y+oy<(gy+1)*size) ink[y][x]=true;
    for(unsigned y=0;y<16;++y) for(unsigned x=0;x<12;++x) {
      const auto index=(i/26*16+y)*320+4+i%26*12+x;
      if (ink[y][x]) expected[index]=color(cell.style);
      else if ((x && ink[y][x-1]) || (x<11 && ink[y][x+1]) || (y && ink[y-1][x]) || (y<15 && ink[y+1][x])) expected[index]=ui::backgroundColor;
    }
  }
  for (size_t i=0;i<expected.size();++i) if (raster::pixels[i]!=expected[i])
    throw std::runtime_error("pixel mismatch x="+std::to_string(i%320)+" y="+std::to_string(i/320));
}
void step(Ili9341Display& display,const UiFrame& frame) {
  const size_t before=raster::transfers.size(); const auto start=raster::clockUs;
  display.service(frame); CHECK(!raster::open);
  CHECK(raster::transfers.size()-before<=ui::burstsPerTick(board::displaySpiHz));
  uint32_t biggest=0;
  for(size_t i=before;i<raster::transfers.size();++i) {
    const auto t=raster::transfers[i]; CHECK(t.w*t.h<=ui::cellWidth*ui::burstRows(board::displaySpiHz));
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
void area(UiFrame& f,uint8_t row,uint8_t col,uint8_t width,uint8_t height,uint8_t style) {
  for(uint8_t y=row;y<row+height;++y) f.text(y,col,width,y==row?"Settings":"",style);
}
void highlightMovement() {
  for(unsigned mode=0;mode<3;++mode) {
    Ili9341Display d; UiFrame f; f.clear(); CHECK(d.begin());
    const uint8_t width=mode==0?8:mode==1?26:12,height=mode==0?12:mode==1?5:2;
    const uint8_t secondRow=mode==1?8:2,secondCol=mode==1?0:mode==0?9:14;
    area(f,2,0,width,height,Accent); area(f,secondRow,secondCol,width,height,Accent);
    CHECK(f.outline(2,0,width,height,true)); CHECK(f.outline(secondRow,secondCol,width,height));
    d.invalidate(); finish(d,f);
    const auto before=raster::transfers.size();
    auto first=f.outline(0),second=f.outline(1); first.c &= ~4; second.c |= 4;
    f.outline(0,first); f.outline(1,second);
    d.invalidate(true); unsigned calls=0;
    while(d.pending() && calls<10000) { step(d,f); ++calls; }
    CHECK(!d.pending()); imageMatches(f);
    uint32_t pixels=0;
    for (auto i=before;i<raster::transfers.size();++i) pixels+=raster::transfers[i].w*raster::transfers[i].h;
    if (mode<2) CHECK(pixels<2U*width*height*192/10); // >=90% less than filled selection.
    const auto change=d.statistics().changeMs;
    CHECK(change && d.statistics().changeMaxMs==change);
    const auto unchanged=raster::transfers.size(); raster::clockUs+=500000;
    d.invalidate(); step(d,f); CHECK(!d.pending());
    CHECK(raster::transfers.size()==unchanged && d.statistics().changeMs==change);
    std::cout<<board::displaySpiHz<<" Hz "<<(mode==0?"group":mode==1?"charger":"button")
      <<" outline transfers="<<raster::transfers.size()-before<<" pixels="<<pixels<<" calls="<<calls<<" change="<<change<<" ms (synthetic SPI only)\n";
    // Reversals interrupt edge recolouring without ever repainting the bodies.
    for(unsigned i=0;i<16;++i) {
      first.c ^= 4; second.c ^= 4; f.outline(0,first); f.outline(1,second);
      d.invalidate(true); step(d,f);
    }
    finish(d,f);
    // Removal, movement and replacement can interrupt an erase or a draw.
    for (unsigned i=0;i<32;++i) {
      f.clear(); f.text(2,0,26,i%2 ? "old gyp_ glyphs" : "NEW 54.10V",i%2 ? Error : Good);
      f.largeNumber(8,i%2 ? "-0.5" : "58.5",Accent);
      if (i%3) f.outline(i%5, i%7, 17, 3, i%2);
      if (i%4) f.outline(7,0,26,5,i%2);
      d.invalidate(true); for(unsigned n=0;n<i%7+1;++n) step(d,f);
    }
    finish(d,f);
    f.clear(); d.invalidate(true); finish(d,f); // No stray border pixels in gutters.
    d.resetStatistics(); CHECK(!d.statistics().changeMaxMs && !d.statistics().renderMaxUs);
  }
}
void packing() {
  CHECK(sizeof(UiFrame)<=565);
  UiFrame f; f.clear();
  for (unsigned style=0;style<8;++style) for (unsigned i=0;i<UiFrame::cells;++i) {
    f.cell(i,{'X',uint8_t(style)}); CHECK(f.cell(i).style==style && f.cell(i).character=='X');
    if(i) CHECK(f.cell(i-1).style==style);
  }
  for (unsigned col=0;col<26;++col) for(unsigned width=0;width<=26-col;++width) {
    f.clear(); f.text(7,col,width,"test",Error);
    for(unsigned i=0;i<UiFrame::cells;++i) CHECK(f.cell(i).style==(i>=7*26+col && i<7*26+col+width ? Error : Text));
  }
  f.clear();
  for (unsigned i=0;i<UiFrame::maxOutlines;++i) CHECK(f.outline(i,25-i,1+i,15-i,i%2));
  CHECK(!f.outline(0,0,1,1) && !f.outline(14,25,2,2));
  for (unsigned i=0;i<UiFrame::maxOutlines;++i) { const auto o=f.outline(i);
    CHECK(o.row()==i && o.column()==25-i && o.width()==1+i && o.height()==15-i && o.focused()==bool(i%2)); }
}
void run() {
  Ili9341Display display; UiFrame frame; frame.clear(); CHECK(display.begin());
  display.invalidate(); finish(display,frame); CHECK(raster::transfers.empty());
  // Invisible style metadata changes must not touch SPI, including the blank
  // top/bottom padding of filled buttons.
  frame.text(1,0,26,"",Accent); display.invalidate(); finish(display,frame); CHECK(raster::transfers.empty());
  frame.text(2,0,26,"Settings",Text);
  frame.text(3,0,26,"",Text); frame.text(4,0,26,"",Text);
  display.invalidate(); finish(display,frame);
  const size_t before=raster::transfers.size();
  for(uint8_t row=2;row<=4;++row) frame.text(row,0,26,row==2?"Settings":"",Accent);
  display.invalidate(); finish(display,frame);
  const size_t highlight=raster::transfers.size()-before;
  CHECK(highlight>0 && highlight<26*3*16/ui::burstRows(board::displaySpiHz)/4);
  std::cout<<board::displaySpiHz<<" Hz highlight transfers="<<highlight<<" scene="<<display.statistics().sceneMs<<" ms (synthetic SPI only)\n";
  // Full ASCII set + every large digit quadrant retain the former GFX appearance.
  frame.clear(); for(uint8_t ch=32;ch<127;++ch) frame.cell(ch-32,{char(ch),uint8_t(ch%6)});
  frame.largeNumber(8,"-54.10",Accent); display.invalidate(); finish(display,frame);
  // Foreground-only differences draw no blank glyph scanlines.
  const size_t start=raster::transfers.size(); frame.cell(0,{' ',Warning}); display.invalidate(); finish(display,frame); CHECK(raster::transfers.size()==start);
  // Replace a scene while a selected cell is only partly transferred; reverse
  // rapidly, including reverting to the original scene, and verify every pixel.
  for(unsigned i=0;i<90;++i) {
    frame.cell(0,{char(i%2?'8':' '),uint8_t(i%3?Text:Accent)});
    frame.largeNumber(8,i%2?"99.9":"0.5",i%3?Text:Accent);
    display.invalidate(); step(display,frame);
  }
  finish(display,frame);
  interruptedFrame=&frame; interruptedDisplay=&display; raster::afterTransfer=changeDuringTransfer;
  frame.cell(0,{'W',Text}); display.invalidate(); step(display,frame); finish(display,frame);
  CHECK(!raster::afterTransfer);
  CHECK(raster::yields==raster::transfers.size());
  const size_t idle=raster::transfers.size(); for(unsigned i=0;i<100;++i) step(display,frame); CHECK(raster::transfers.size()==idle);
  // micros rollover and explicit overrun visibility: budget checked between
  // bounded units, never described as a hard deadline.
  raster::clockUs=std::numeric_limits<uint32_t>::max()-20;
  frame.text(0,0,26,"Rollover",Error); display.invalidate(); step(display,frame); finish(display,frame);
  raster::clockUs=(uint64_t(std::numeric_limits<uint32_t>::max())-3)*1000;
  frame.text(0,0,26,"Millis rollover",Accent); display.invalidate(); finish(display,frame);
  CHECK(display.statistics().sceneMs<1000);
  raster::wireScale=20; frame.text(0,0,26,"Slow transaction",Good); display.invalidate(); finish(display,frame);
  CHECK(display.statistics().renderMaxUs>ui::renderBudgetUs && display.statistics().transactionMaxUs>ui::renderBudgetUs);
  raster::wireScale=1;
  // Costly preparation must yield but never keep discarding the first strip.
  raster::cpuCost=250; frame.text(0,0,26,"Prepared work",Good);
  display.invalidate(true); finish(display,frame); raster::cpuCost=1;
}
void snapshots(int argc,char** argv) {
  Ili9341Display display; CHECK(display.begin());
  for (int file=1;file<argc;++file) {
    std::ifstream in(argv[file],std::ios::binary); CHECK(bool(in));
    UiFrame frame; frame.clear(); std::string line;
    for (unsigned row=0;row<ui::rows;++row) {
      CHECK(bool(std::getline(in,line)) && line.size()==ui::columns);
      for(unsigned col=0;col<ui::columns;++col) frame.cell(row*ui::columns+col,{line[col],Text});
    }
    CHECK(bool(std::getline(in,line)) && line.size()==UiFrame::cells);
    for(unsigned i=0;i<UiFrame::cells;++i) { auto cell=frame.cell(i); cell.style=line[i]-'0'; CHECK(cell.style<8); frame.cell(i,cell); }
    CHECK(bool(std::getline(in,line)));
    for(auto& c:line) if(c<'0' || c>'9') c=' ';
    std::istringstream rectangles(line); unsigned row,col,width,height,focus;
    while(rectangles>>row>>col>>width>>height>>focus) CHECK(frame.outline(row,col,width,height,focus));
    display.invalidate(true); finish(display,frame);
  }
  if(argc>1) std::cout<<"PASS production snapshots: "<<argc-1<<" screens at "<<board::displaySpiHz<<" Hz\n";
}
int main(int argc,char** argv) { try { packing(); run(); highlightMovement(); snapshots(argc,argv); } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; } }
