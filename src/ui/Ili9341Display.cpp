#include "Ili9341Display.h"
#if PSU_ENABLE_DISPLAY
#include "../config/BoardConfig.h"
#include <string.h>
namespace psu {
namespace {
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
  pinMode(board::canChipSelect, OUTPUT); digitalWrite(board::canChipSelect, HIGH);
  if (board::displayBacklight >= 0) { pinMode(board::displayBacklight, OUTPUT); analogWrite(board::displayBacklight, 0); }
  tft_.begin(board::displaySpiHz); tft_.setRotation(board::displayRotation);
  available_ = !board::displayVerifyId ||
      (tft_.readcommand8(ILI9341_RDID4, 2) == 0x93 && tft_.readcommand8(ILI9341_RDID4, 3) == 0x41);
  if (!available_) return false;
  // Boot-only full clear, before CAN's receive interrupt exists.
  tft_.fillScreen(0x10c2); previous_.clear(); dim(false); return true;
}
bool Ili9341Display::canDim() const { return board::displayBacklight >= 0 && ui::backlightIdleMs != 0; }
void Ili9341Display::dim(bool dimmed) {
  if (board::displayBacklight >= 0) analogWrite(board::displayBacklight, dimmed ? ui::dimBrightness : ui::fullBrightness);
}
void Ili9341Display::Glyph::drawPixel(int16_t x, int16_t y, uint16_t value) {
  if (x < 0 || y < 0 || x >= ui::cellWidth || y >= ui::cellHeight) return;
  if (value) rows[y] |= uint16_t(1U << x); else rows[y] &= uint16_t(~(1U << x));
}
void Ili9341Display::Glyph::prepare(char c) { memset(rows, 0, sizeof(rows)); drawChar(0, 0, c, 1, 0, 2); }
void Ili9341Display::service(const UiFrame& frame) {
  if (!available_) return;
  uint8_t searchBudget = ui::scanCellsPerTick;
  for (uint8_t slice = 0; slice < ui::slicesPerTick; ++slice) {
    if (scanRow_ == ui::cellHeight) {
      if (!changes_.next(frame, previous_, searchBudget, activeIndex_, active_)) return;
      glyph_.prepare(active_.character); scanRow_ = 0;
    }
    uint16_t pixels[ui::cellWidth * ui::sliceRows];
    const uint16_t foreground = color(active_.style), background = active_.style & selectedStyle ? 0x21e5 : 0x10c2;
    for (uint8_t y = 0; y < ui::sliceRows; ++y)
      for (uint8_t x = 0; x < ui::cellWidth; ++x)
        pixels[y * ui::cellWidth + x] = glyph_.rows[scanRow_ + y] & (1U << x) ? foreground : background;
    // arduino-CAN's SPI.usingInterrupt masks its RX IRQ during transactions.
    // Release it every 24 pixels, never hold it for a whole glyph/line/screen.
    tft_.startWrite();
    tft_.setAddrWindow(4 + activeIndex_ % ui::columns * ui::cellWidth,
                      activeIndex_ / ui::columns * ui::cellHeight + scanRow_, ui::cellWidth, ui::sliceRows);
    tft_.writePixels(pixels, ui::cellWidth * ui::sliceRows); tft_.endWrite();
    scanRow_ += ui::sliceRows;
    if (scanRow_ == ui::cellHeight) previous_.cell(activeIndex_, active_);
  }
}
static_assert(ui::cellHeight % ui::sliceRows == 0, "Glyph slices must fit exactly");
static_assert(ui::columns * ui::cellWidth + 8 == 320 && ui::rows * ui::cellHeight == 240, "ILI9341 landscape layout");
}
#endif
