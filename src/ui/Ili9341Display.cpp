#include "Ili9341Display.h"
#include "../diagnostics/RuntimeTiming.h"
#if PSU_ENABLE_DISPLAY
#include "../config/BoardConfig.h"
#include "../config/TouchConfig.h"
#include <string.h>
#include <glcdfont.c> // Pinned Adafruit classic font, stored in flash on AVR.
namespace psu {
namespace {
// Datasheet section 8.3.23: ID4 is 0xD3. Adafruit's ILI9341_RDID4 names 0xDD.
constexpr uint8_t readId4Command = 0xd3;
uint16_t color(uint8_t style) {
  switch (style & 7) {
    case Muted: return 0xadf5;
    case Good: return 0x6f2f;
    case Warning: return 0xfdcd;
    case Error: return 0xfa69;
    case Accent: return 0x8ef6;
    default: return 0xefbd;
  }
}
}
Ili9341Display::Ili9341Display() : tft_(board::displayChipSelect, board::displayDataCommand, board::displayReset) {}
bool Ili9341Display::begin() {
#if PSU_ENABLE_TOUCH
  pinMode(touch::chipSelect, OUTPUT); digitalWrite(touch::chipSelect, HIGH);
#endif
  pinMode(board::canChipSelect, OUTPUT); digitalWrite(board::canChipSelect, HIGH);
  if (board::displayBacklight >= 0) { pinMode(board::displayBacklight, OUTPUT); analogWrite(board::displayBacklight, 0); }
  tft_.begin(board::displaySpiHz); tft_.setRotation(board::displayRotation);
  available_ = true;
  if (board::displayVerifyId) {
    const uint32_t readHz = board::displaySpiHz < board::displayReadMaxSpiHz ?
        board::displaySpiHz : board::displayReadMaxSpiHz;
    tft_.setSPISpeed(readHz);
    const uint8_t high = tft_.readcommand8(readId4Command, 2);
    const uint8_t low = tft_.readcommand8(readId4Command, 3);
    tft_.setSPISpeed(board::displaySpiHz);
    available_ = high == 0x93 && low == 0x41;
  }
  if (!available_) return false;
  // Boot-only full clear, before CAN's receive interrupt exists.
  tft_.fillScreen(0x10c2); previous_.clear(); dim(false); return true;
}
bool Ili9341Display::canDim() const { return board::displayBacklight >= 0 && ui::backlightIdleMs != 0; }
void Ili9341Display::dim(bool dimmed) {
  if (board::displayBacklight >= 0) analogWrite(board::displayBacklight, dimmed ? ui::dimBrightness : ui::fullBrightness);
}
void Ili9341Display::invalidate(bool change) {
  // A newer user action supersedes the old scene. A periodic update during
  // painting keeps the original timer and cannot erase its input provenance.
  if (change || !pending()) { sceneStarted_ = millis(); changeScene_ = change; }
  changes_.invalidate(); outlineRemaining_ = UiFrame::maxOutlines; outlineNext_ = 0; sceneCheck_=true;
}
void Ili9341Display::Glyph::prepare(char c, uint8_t firstRow, uint8_t endRow) {
  memset(rows, 0, sizeof(rows));
  if (c == ' ') return;
  const uint8_t code = uint8_t(c);
  const bool large = code >= 128 && code < 176;
  const uint8_t digit = large ? (code - 128) / 4 : 0, quadrant = large ? (code - 128) % 4 : 0;
  const uint8_t character = large ? (digit < 10 ? '0' + digit : digit == 10 ? '.' : '-') : code;
  const uint8_t scale = large ? 4 : 2;
  uint8_t columns[5];
  for (uint8_t x = 0; x < 5; ++x) {
#ifdef __AVR__
    columns[x] = pgm_read_byte(font + uint16_t(character) * 5 + x);
#else
    columns[x] = font[uint16_t(character) * 5 + x];
#endif
  }
  if (!large) {
    // The common 2x font needs five fixed masks, not a variable-width shift for
    // every column of every row. Large editor quadrants retain the general path.
    const uint8_t start=firstRow & 0xfe;
    uint8_t bit=uint8_t(1U<<(start/2));
    for (uint8_t y=start; y<endRow; y+=2, bit<<=1) {
      uint16_t row=0;
      if (columns[0]&bit) row|=0x003;
      if (columns[1]&bit) row|=0x00c;
      if (columns[2]&bit) row|=0x030;
      if (columns[3]&bit) row|=0x0c0;
      if (columns[4]&bit) row|=0x300;
      rows[y/2]=row;
    }
    return;
  }
  const uint8_t first = large && quadrant % 2 ? 3 : 0;
  const uint8_t end = large && !(quadrant % 2) ? 3 : 5;
  for (uint8_t y = firstRow & uint8_t(~(scale-1)); y < endRow; y += scale) {
    const uint8_t sy = (y + quadrant / 2 * ui::cellHeight) >> (large ? 2 : 1);
    uint16_t row = 0, pixels = large ? 15 : 3;
    for (uint8_t x = first; x < end; ++x, pixels <<= scale)
      if (columns[x] & (1U << sy)) row |= pixels;
    for (uint8_t repeat = 0; repeat < scale; repeat+=2) rows[(y + repeat)/2] = row;
  }
}
Ili9341Display::Rect Ili9341Display::bounds(UiOutline o) {
  const uint16_t top = uint16_t(o.row()) * ui::cellHeight;
  const uint16_t bottom = uint16_t(o.row() + o.height()) * ui::cellHeight;
  const uint16_t y = top >= 2 ? top - 2 : 0, end = bottom + 2 < 240 ? bottom + 2 : 240;
  return {uint16_t(2 + o.column() * ui::cellWidth), y, uint16_t(o.width() * ui::cellWidth + 4), uint16_t(end - y)};
}
Ili9341Display::Rect Ili9341Display::edge(UiOutline o, uint8_t side) {
  const auto r = bounds(o); const uint8_t n = ui::outlineWidth;
  if (side == 0) return {r.x, r.y, r.w, n};
  if (side == 1) return {r.x, uint16_t(r.y + r.h - n), r.w, n};
  return {uint16_t(side == 2 ? r.x : r.x + r.w - n), uint16_t(r.y + n), n, uint16_t(r.h - 2*n)};
}
bool Ili9341Display::intersects(Rect a, Rect b) {
  return a.x < b.x+b.w && b.x < a.x+a.w && a.y < b.y+b.h && b.y < a.y+a.h;
}
void Ili9341Display::outlines(const UiFrame& frame, Rect strip, uint16_t* pixels, int8_t replace, UiOutline value) {
  // Outline draws already know that this complete strip lies on the target
  // edge. Only erasure and ordinary glyph strips need general composition.
  const bool solidEdge=replace>=0 && value.valid();
  const uint16_t base=solidEdge ? (value.focused() ? ui::focusColor : ui::outlineColor) : ui::backgroundColor;
  for (uint16_t i=0; i<strip.w*strip.h; ++i) pixels[i]=base;
  if (solidEdge && value.focused()) return;
  // Focus wins at shared edges. Most rectangles are rejected before considering
  // their four edges; no per-pixel scan of the outline list is necessary.
  const uint8_t count=replace>=int8_t(frame.outlineCount()) ? replace+1 : frame.outlineCount();
  for (uint8_t i=0; i<count; ++i) {
    const auto o=i==replace ? value : frame.outline(i);
    if (!o.valid() || (solidEdge && !o.focused()) || !intersects(bounds(o),strip)) continue;
    const uint16_t shade=o.focused() ? ui::focusColor : ui::outlineColor;
    for (uint8_t side=0; side<4; ++side) {
      const auto e=edge(o,side); if (!intersects(e,strip)) continue;
      const uint16_t x0=e.x>strip.x ? e.x : strip.x, y0=e.y>strip.y ? e.y : strip.y;
      const uint16_t x1=e.x+e.w<strip.x+strip.w ? e.x+e.w : strip.x+strip.w;
      const uint16_t y1=e.y+e.h<strip.y+strip.h ? e.y+e.h : strip.y+strip.h;
      uint16_t* row=pixels+(y0-strip.y)*strip.w+x0-strip.x;
      for (uint16_t y=y0; y<y1; ++y, row+=strip.w) {
        uint16_t* p=row;
        for (uint16_t x=x0; x<x1; ++x, ++p) if (*p!=ui::focusColor) *p=shade;
      }
    }
  }
}
void Ili9341Display::ink(uint16_t* pixels, Rect strip, uint16_t x, uint16_t y, const Glyph& glyph, uint16_t fg) {
  const uint16_t x0=x>strip.x ? x : strip.x, y0=y>strip.y ? y : strip.y;
  const uint16_t x1=x+ui::cellWidth<strip.x+strip.w ? x+ui::cellWidth : strip.x+strip.w;
  const uint16_t y1=y+ui::cellHeight<strip.y+strip.h ? y+ui::cellHeight : strip.y+strip.h;
  uint16_t* destination=pixels+(y0-strip.y)*strip.w+x0-strip.x;
  const uint8_t skip=x0-x;
  for (uint16_t sy=y0; sy<y1; ++sy, destination+=strip.w) {
    const uint8_t row=sy-y; uint16_t bits=glyph.row(row);
    // Preserve every glyph, with a one-pixel dark clearance where an outline
    // meets text. This also protects descenders and tight one-line menus.
    uint16_t halo=bits | (bits<<1) | (bits>>1);
    if (row) halo |= glyph.row(row-1);
    if (row+1<ui::cellHeight) halo |= glyph.row(row+1);
    bits >>= skip; halo >>= skip;
    uint16_t* p=destination;
    for (uint16_t sx=x0; sx<x1; ++sx, ++p, bits>>=1, halo>>=1) {
      if (bits & 1) *p=fg;
      else if (halo & 1) *p=ui::backgroundColor;
    }
  }
}
void Ili9341Display::text(const UiFrame& frame, Rect strip, uint16_t* pixels) {
  const uint16_t x0=strip.x<4 ? 4 : strip.x, x1=strip.x+strip.w>316 ? 316 : strip.x+strip.w;
  if (x0>=x1) return;
  for (uint8_t row=strip.y/16; row<=(strip.y+strip.h-1)/16; ++row)
    for (uint8_t col=(x0-4)/12; col<=(x1-5)/12; ++col) {
      const uint16_t index=uint16_t(row)*ui::columns+col;
      if (frame.character(index)==' ') continue;
      const auto cell=frame.cell(index);
      const uint16_t top=uint16_t(row)*16;
      const uint8_t first=strip.y>top ? strip.y-top-1 : 0;
      const uint8_t end=strip.y+strip.h+1<top+16 ? strip.y+strip.h+1-top : 16;
      Glyph glyph; glyph.prepare(cell.character,first,end);
      ink(pixels,strip,4+uint16_t(col)*12,uint16_t(row)*16,glyph,color(cell.style));
    }
}
void Ili9341Display::transfer(Rect r, uint16_t* pixels) {
  const uint8_t count=r.w*r.h; bool solid=true;
  for (uint8_t i=1; i<count && solid; ++i) solid=pixels[i]==pixels[0];
  const uint32_t started=micros();
  tft_.startWrite(); tft_.setAddrWindow(r.x,r.y,r.w,r.h);
  if (solid) tft_.writeColor(pixels[0],count); else tft_.writePixels(pixels,count);
  tft_.endWrite();
  const uint32_t elapsed=timing::elapsed(started,micros());
  if (elapsed!=timing::invalid && elapsed>stats_.transactionMaxUs) stats_.transactionMaxUs=elapsed;
}
bool Ili9341Display::outlineStep(const UiFrame& frame, bool eraseOnly) {
  if (!outlineStage_) {
    uint8_t budget=ui::scanCellsPerBatch;
    while (outlineRemaining_ && budget--) {
      --outlineRemaining_; activeIndex_=outlineNext_++;
      const auto old=previous_.outline(activeIndex_); outlineTarget_=eraseOnly ? UiOutline{} : frame.outline(activeIndex_);
      if (old==outlineTarget_) continue;
      outlineStage_=old.valid() && !old.sameBounds(outlineTarget_) ? 1 : 2;
      outlineEdge_=0; outlineOffset_=0; break;
    }
    if (!outlineStage_) return false;
  }
  const auto o=outlineStage_==1 ? previous_.outline(activeIndex_) : outlineTarget_;
  if (!o.valid()) { previous_.outline(activeIndex_,outlineTarget_); outlineStage_=0; return false; }
  auto strip=edge(o,outlineEdge_);
  const bool horizontal=outlineEdge_<2;
  const uint16_t length=horizontal ? strip.w : strip.h;
  constexpr uint8_t segment=ui::cellWidth*ui::burstRows(board::displaySpiHz)/ui::outlineWidth;
  const uint16_t n=length-outlineOffset_<segment ? length-outlineOffset_ : segment;
  if (horizontal) { strip.x+=outlineOffset_; strip.w=n; }
  else { strip.y+=outlineOffset_; strip.h=n; }
  uint16_t pixels[ui::cellWidth*ui::burstRows(board::displaySpiHz)];
  // Only render tracked geometry. Painting a newer scene's unrelated outlines
  // here could leave untracked fragments if that scene is superseded again.
  outlines(previous_,strip,pixels,activeIndex_,outlineStage_==1 ? UiOutline{} : outlineTarget_);
  // Finish an in-flight glyph before reaching here. The committed text cache
  // then describes the actual text beneath every erased/recoloured edge.
  text(previous_,strip,pixels); transfer(strip,pixels);
  outlineOffset_+=n;
  if (outlineOffset_==length) { outlineOffset_=0; ++outlineEdge_; }
  if (outlineEdge_==4) {
    if (outlineStage_==1 && outlineTarget_.valid()) {
      previous_.outline(activeIndex_,{}); outlineStage_=2; outlineEdge_=0;
    }
    else { previous_.outline(activeIndex_,outlineTarget_); outlineStage_=0; }
  }
  return true;
}
void Ili9341Display::service(const UiFrame& frame) {
  if (!available_ || !pending()) return;
  const uint32_t started=micros(); uint8_t transfers=0;
  while (transfers<ui::burstsPerTick(board::displaySpiHz) && timing::elapsed(started,micros())<ui::renderBudgetUs) {
    if (scanRow_==ui::cellHeight) {
      if (sceneCheck_ && !outlineStage_) {
        sceneCheck_=false; outlinesAfterText_=false;
        for (uint8_t i=0; i<UiFrame::maxOutlines; ++i)
          if (!previous_.outline(i).sameBounds(frame.outline(i))) { outlinesAfterText_=true; break; }
        outlineNext_=0; outlineRemaining_=UiFrame::maxOutlines;
      }
      if (outlineStage_ || outlineRemaining_) {
        if (outlineStep(frame,outlinesAfterText_)) ++transfers;
        continue;
      }
      // Selection alone changes only outline metadata. Compare packed text in
      // one bounded pass instead of decoding all 390 cells on every wheel step.
      if (changes_.fullPass() && frame.sameText(previous_)) changes_.finish();
      if (!changes_.pending()) {
        // A changed layout clears old edges, paints text, then adds new edges.
        // No glyph needs to repeatedly composite a not-yet-drawn border.
        if (outlinesAfterText_) { outlinesAfterText_=false; outlineNext_=0; outlineRemaining_=UiFrame::maxOutlines; continue; }
        break;
      }
      uint8_t searchBudget=ui::scanCellsPerBatch;
      if (!changes_.next(frame,previous_,searchBudget,activeIndex_,active_)) {
        continue;
      }
      const auto old=previous_.cell(activeIndex_);
      if (old.character==' ' && active_.character==' ') { previous_.cell(activeIndex_,active_); continue; }
      glyph_.prepare(active_.character); Glyph previousGlyph;
      if (old.character==active_.character) memcpy(previousGlyph.rows,glyph_.rows,sizeof(glyph_.rows));
      else previousGlyph.prepare(old.character);
      const Rect cell={uint16_t(4+activeIndex_%ui::columns*ui::cellWidth),uint16_t(activeIndex_/ui::columns*ui::cellHeight),ui::cellWidth,ui::cellHeight};
      activeBorder_=false;
      for (uint8_t i=0; i<previous_.outlineCount() && !activeBorder_; ++i) if (previous_.outline(i).valid()) {
        const auto b=bounds(previous_.outline(i));
        activeBorder_=intersects(cell,b) && (cell.x<b.x+ui::outlineWidth || cell.y<b.y+ui::outlineWidth ||
            cell.x+cell.w>b.x+b.w-ui::outlineWidth || cell.y+cell.h>b.y+b.h-ui::outlineWidth);
      }
      dirtyRows_=0;
      const bool recolor=color(active_.style)!=color(old.style);
      for (uint8_t y=0; y<ui::cellHeight; y+=2) {
        const auto a=glyph_.row(y), b=previousGlyph.row(y);
        dirtyColumns_[y/2]=activeBorder_ ? 0x0fff : recolor ? a|b : a^b;
        if (dirtyColumns_[y/2]) dirtyRows_ |= uint16_t(3U<<y);
      }
      scanRow_=0;
      if (timing::elapsed(started,micros())>=ui::renderBudgetUs) break;
    }
    while (scanRow_<ui::cellHeight && !(dirtyRows_ & (1U<<scanRow_))) ++scanRow_;
    if (scanRow_==ui::cellHeight) { previous_.cell(activeIndex_,active_); continue; }
    uint8_t rows=1;
    while (rows<ui::burstRows(board::displaySpiHz) && scanRow_+rows<ui::cellHeight &&
        (dirtyRows_ & (1U<<(scanRow_+rows)))) ++rows;
    const uint16_t x=4+activeIndex_%ui::columns*ui::cellWidth, y=activeIndex_/ui::columns*ui::cellHeight;
    uint16_t columns=0;
    for (uint8_t row=0;row<rows;++row) columns |= dirtyColumns_[(scanRow_+row)/2];
    uint8_t first=0, end=ui::cellWidth;
    while (!(columns & (1U<<first))) ++first;
    while (!(columns & (1U<<(end-1)))) --end;
    const Rect strip={uint16_t(x+first),uint16_t(y+scanRow_),uint16_t(end-first),rows};
    uint16_t pixels[ui::cellWidth*ui::burstRows(board::displaySpiHz)];
    if (activeBorder_) {
      outlines(previous_,strip,pixels); ink(pixels,strip,x,y,glyph_,color(active_.style));
    } else {
      const uint16_t fg=color(active_.style); uint16_t* p=pixels;
      for (uint8_t row=0; row<rows; ++row) {
        uint16_t bits=glyph_.row(scanRow_+row)>>first;
        for (uint8_t col=first; col<end; ++col, bits>>=1) *p++=bits&1 ? fg : ui::backgroundColor;
      }
    }
    // Commit a prepared strip even after expensive preparation. No transaction
    // remains open at a yield; the pixel cap is unchanged at every SPI clock.
    transfer(strip,pixels); ++transfers; scanRow_+=rows;
    while (scanRow_<ui::cellHeight && !(dirtyRows_ & (1U<<scanRow_))) ++scanRow_;
    if (scanRow_==ui::cellHeight) previous_.cell(activeIndex_,active_);
  }
  const uint32_t elapsed = timing::elapsed(started, micros());
  if (elapsed != timing::invalid && elapsed > stats_.renderMaxUs) stats_.renderMaxUs = elapsed;
  if (!pending()) {
    stats_.sceneMs = millis() - sceneStarted_;
    if (stats_.sceneMs > stats_.sceneMaxMs) stats_.sceneMaxMs = stats_.sceneMs;
    if (changeScene_) {
      stats_.changeMs = stats_.sceneMs;
      if (stats_.changeMs > stats_.changeMaxMs) stats_.changeMaxMs = stats_.changeMs;
      changeScene_ = false;
    }
  }
}
static_assert(ui::columns * ui::cellWidth + 8 == 320 && ui::rows * ui::cellHeight == 240, "ILI9341 landscape layout");
}
#endif
