#include "Ili9341Display.h"
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
  changes_.invalidate();
}
void Ili9341Display::Glyph::prepare(char c) {
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
  const uint8_t first = large && quadrant % 2 ? 3 : 0;
  const uint8_t end = large && !(quadrant % 2) ? 3 : 5;
  for (uint8_t y = 0; y < ui::cellHeight; y += scale) {
    const uint8_t sy = (y + quadrant / 2 * ui::cellHeight) >> (large ? 2 : 1);
    uint16_t row = 0, pixels = large ? 15 : 3;
    for (uint8_t x = first; x < end; ++x, pixels <<= scale)
      if (columns[x] & (1U << sy)) row |= pixels;
    for (uint8_t repeat = 0; repeat < scale; ++repeat) rows[y + repeat] = row;
  }
}
void Ili9341Display::service(const UiFrame& frame) {
  if (!available_ || !pending()) return;
  const uint32_t started = micros();
  uint8_t transfers = 0;
  while (transfers < ui::burstsPerTick(board::displaySpiHz) && uint32_t(micros() - started) < ui::renderBudgetUs) {
    if (scanRow_ == ui::cellHeight) {
      uint8_t searchBudget = ui::scanCellsPerBatch;
      if (!changes_.next(frame, previous_, searchBudget, activeIndex_, active_)) {
        if (!changes_.pending()) break;
        continue; // Recheck the time budget between bounded comparison batches.
      }
      const auto old = previous_.cell(activeIndex_);
      const uint16_t bg = active_.style & selectedStyle ? 0x21e5 : 0x10c2;
      const uint16_t oldBg = old.style & selectedStyle ? 0x21e5 : 0x10c2;
      // Foreground styles cannot affect an empty cell. Still commit metadata so
      // the next invalidation does not rediscover the same invisible difference.
      if (old.character == ' ' && active_.character == ' ') {
        if (bg == oldBg) { previous_.cell(activeIndex_, active_); continue; }
        dirtyRows_ = 0xffff; // Filled blank: no glyph expansion or pixel raster.
      } else {
        glyph_.prepare(active_.character);
        Glyph previousGlyph;
        if (old.character == active_.character) memcpy(previousGlyph.rows, glyph_.rows, sizeof(glyph_.rows));
        else previousGlyph.prepare(old.character);
        dirtyRows_ = 0;
        const uint16_t fg = color(active_.style), oldFg = color(old.style);
        for (uint8_t y = 0; y < ui::cellHeight; ++y) {
          const uint16_t a = glyph_.rows[y], b = previousGlyph.rows[y];
          // Four pixel classes: foreground on both, background on both, and
          // the two glyph edges. Compare RGB565 values, not style metadata.
          if ((fg != oldFg && (a & b)) || (bg != oldBg && (~(a | b) & 0x0fff)) ||
              (fg != oldBg && (a & ~b)) || (bg != oldFg && (b & ~a))) dirtyRows_ |= uint16_t(1U << y);
        }
      }
      scanRow_ = 0;
      // Raster comparison is also a work unit. Do not start an SPI burst if it
      // consumed this call's budget; the prepared glyph resumes next time.
      if (uint32_t(micros() - started) >= ui::renderBudgetUs) break;
    }
    while (scanRow_ < ui::cellHeight && !(dirtyRows_ & (1U << scanRow_))) ++scanRow_;
    if (scanRow_ == ui::cellHeight) { previous_.cell(activeIndex_, active_); continue; }
    uint8_t rows = 1;
    while (rows < ui::burstRows(board::displaySpiHz) && scanRow_ + rows < ui::cellHeight &&
        (dirtyRows_ & (1U << (scanRow_ + rows)))) ++rows;
    uint16_t pixels[ui::cellWidth * ui::burstRows(board::displaySpiHz)];
    const uint16_t fg = color(active_.style), bg = active_.style & selectedStyle ? 0x21e5 : 0x10c2;
    const uint16_t first = active_.character == ' ' ? 0 : glyph_.rows[scanRow_];
    bool solid = first == 0 || first == 0x0fff;
    if (active_.character != ' ')
      for (uint8_t y = 1; solid && y < rows; ++y) solid = glyph_.rows[scanRow_ + y] == first;
    if (!solid) {
      for (uint8_t y = 0; y < rows; ++y) {
        uint16_t bits = glyph_.rows[scanRow_ + y];
        for (uint8_t x = 0; x < ui::cellWidth; ++x, bits >>= 1)
          pixels[y * ui::cellWidth + x] = bits & 1 ? fg : bg;
      }
    }
    // Finish at least one prepared strip per call. Otherwise an expensive
    // raster/interrupt before this check could discard it on every pass.
    // The one bounded strip may overrun; subsequent strips respect the budget.
    if (uint32_t(micros() - started) >= ui::renderBudgetUs && transfers) break;
    // CAN's registered RX interrupt is masked only inside this bounded burst.
    const uint32_t transaction = micros();
    tft_.startWrite();
    tft_.setAddrWindow(4 + activeIndex_ % ui::columns * ui::cellWidth,
                      activeIndex_ / ui::columns * ui::cellHeight + scanRow_, ui::cellWidth, rows);
    if (solid) tft_.writeColor(first ? fg : bg, ui::cellWidth * rows);
    else tft_.writePixels(pixels, ui::cellWidth * rows);
    tft_.endWrite();
    const uint32_t transactionUs = micros() - transaction;
    if (transactionUs > stats_.transactionMaxUs) stats_.transactionMaxUs = transactionUs;
    ++transfers; scanRow_ += rows;
    // Skip unchanged trailing rows even at the transfer limit; pending() must
    // reflect exactly the pixels and comparison pass still outstanding.
    while (scanRow_ < ui::cellHeight && !(dirtyRows_ & (1U << scanRow_))) ++scanRow_;
    if (scanRow_ == ui::cellHeight) previous_.cell(activeIndex_, active_);
  }
  const uint32_t elapsed = micros() - started;
  if (elapsed > stats_.renderMaxUs) stats_.renderMaxUs = elapsed;
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
