#include "../util/FlashString.h"
#include "UiFrame.h"
#include <string.h>
#include <math.h>
#include <stdio.h>
namespace psu {
static_assert(sizeof(UiOutline)==3 && sizeof(UiFrame)<=565, "UI scenes must retain Mega RAM headroom");
namespace {
char read(const char* p, bool flash) {
#ifdef __AVR__
  return flash ? pgm_read_byte(p) : *p;
#else
  (void)flash; return *p;
#endif
}
}
void UiFrame::clear() { memset(characters_, ' ', sizeof(characters_)); memset(styles_, 0, sizeof(styles_)); outlineCount_ = 0; }
void UiFrame::invalidate() { memset(characters_, 0, sizeof(characters_)); memset(styles_, 0xff, sizeof(styles_)); }
bool UiFrame::sameText(const UiFrame& other) const {
  return !memcmp(characters_,other.characters_,sizeof(characters_)) && !memcmp(styles_,other.styles_,sizeof(styles_));
}
UiCell UiFrame::cell(uint16_t i) const {
  const uint16_t bit = i * 3, index = bit / 8; const uint8_t shift = bit & 7;
  uint16_t value = styles_[index];
  if (shift > 5) value |= uint16_t(styles_[index + 1]) << 8;
  return {characters_[i], uint8_t((value >> shift) & 7)};
}
void UiFrame::style(uint16_t i, uint8_t value) {
  const uint16_t bit = i * 3, index = bit / 8; const uint8_t shift = bit & 7;
  value &= 7;
  styles_[index] = (styles_[index] & uint8_t(~(7 << shift))) | (value << shift);
  if (shift > 5) styles_[index + 1] = (styles_[index + 1] & uint8_t(~(7 >> (8 - shift)))) | (value >> (8 - shift));
}
void UiFrame::cell(uint16_t i, UiCell v) {
  characters_[i] = v.character; style(i, v.style);
}
bool UiFrame::outline(uint8_t r, uint8_t col, uint8_t w, uint8_t h, bool focused) {
  if (!w || !h || r + h > ui::rows || col + w > ui::columns || outlineCount_ == maxOutlines) return false;
  outlines_[outlineCount_++] = {uint8_t(r | (h << 4)), uint8_t(col | (w << 5)), uint8_t((w >> 3) | (focused ? 4 : 0))};
  return true;
}
void UiFrame::outline(uint8_t i, UiOutline value) {
  if (i >= maxOutlines) return;
  while (outlineCount_ <= i) outlines_[outlineCount_++] = {};
  outlines_[i] = value;
}
bool FrameChanges::next(const UiFrame& frame, const UiFrame& painted, uint8_t& budget,
                        uint16_t& index, UiCell& value) {
  while (remaining_ && budget) {
    --remaining_; --budget; index = cursor_;
    if (++cursor_ == UiFrame::cells) cursor_ = 0;
    value = frame.cell(index); const auto old = painted.cell(index);
    if (value.character != old.character || value.style != old.style) return true;
  }
  return false;
}
void UiFrame::put(uint8_t row, uint8_t col, uint8_t width, const char* value, bool flash, uint8_t style, bool right) {
  if (row >= ui::rows || col >= ui::columns) return;
  if (width > ui::columns - col) width = ui::columns - col;
  uint8_t n = 0; while (n < width && read(value + n, flash)) ++n;
  const bool cut = n == width && read(value + n, flash);
  const uint8_t pad = right && !cut ? width - n : 0;
  const uint16_t start = uint16_t(row) * ui::columns + col;
  memset(characters_ + start, ' ', width);
  for (uint8_t i = 0; i < n; ++i) {
    char c = read(value + i, flash);
    if (c < 32 || c > 126) c = '?';
    characters_[start + pad + i] = c;
  }
  if (cut && width) characters_[start + width - 1] = '~';
  // Eight three-bit colours occupy three bytes. Compact colours pay for the
  // bounded outline list without increasing either scene cache's RAM usage.
  uint16_t first = start; uint8_t left = width; style &= 7;
  while (left && (first & 7)) { this->style(first++, style); --left; }
  const uint8_t repeated[] = {uint8_t(style | (style << 3) | (style << 6)),
      uint8_t((style >> 2) | (style << 1) | (style << 4) | (style << 7)),
      uint8_t((style >> 1) | (style << 2) | (style << 5))};
  while (left >= 8) { memcpy(styles_ + first * 3 / 8, repeated, 3); first += 8; left -= 8; }
  while (left--) this->style(first++, style);
}
void UiFrame::text(uint8_t r, uint8_t c, uint8_t w, const char* s, uint8_t style, bool right) { put(r, c, w, s, false, style, right); }
void UiFrame::text(uint8_t r, uint8_t c, uint8_t w, FlashText s, uint8_t style, bool right) { put(r, c, w, s.data, true, style, right); }
void UiFrame::wrap(uint8_t row, uint8_t end, FlashText value, uint8_t style) {
  const char* p = value.data;
  while (row <= end && row < ui::rows && read(p, true)) {
    char line[ui::columns + 1]; uint8_t n = 0, space = 0;
    while (n < ui::columns && read(p + n, true)) { line[n] = read(p + n, true); if (line[n] == ' ') space = n; ++n; }
    if (n == ui::columns && read(p + n, true) && space) n = space;
    line[n] = 0; text(row++, 0, ui::columns, line, style); p += n;
    while (read(p, true) == ' ') ++p;
  }
}
void formatValue(char* out, uint8_t size, float value, const char* unit, uint8_t decimals) {
  if (!size) return;
  out[0] = 0;
  if (!isfinite(value)) { PSU_SNPRINTF(out, size, "--"); return; }
  const bool negative = value < 0; float magnitude = fabsf(value);
  if (decimals > 2) decimals = 2;
  const char prefixes[] = {'\0', 'k', 'M'};
  for (uint8_t scale = 0; scale < 3; ++scale, magnitude /= 1000) {
    for (int8_t precision = decimals; precision >= 0; --precision) {
      const uint16_t divisor = precision == 2 ? 100 : precision == 1 ? 10 : 1;
      if (magnitude * divisor >= 4294967040.0f) continue;
      const uint32_t rounded = uint32_t(magnitude * divisor + 0.5f);
      char number[24];
      int n = PSU_SNPRINTF(number, sizeof(number), "%s%lu", negative && rounded ? "-" : "", (unsigned long)(rounded / divisor));
      // AVR-libc aborts printf on '*' widths/precisions. Generate the at-most
      // two fractional digits directly, including leading/trailing zeroes.
      if (precision) {
        number[n++] = '.';
        if (precision == 2) number[n++] = char('0' + rounded / 10 % 10);
        number[n++] = char('0' + rounded % 10);
      }
      if (scale) number[n++] = prefixes[scale];
      number[n] = 0;
      if (n + strlen(unit) < size) { strcpy(out, number); strcat(out, unit); return; }
    }
  }
  PSU_SNPRINTF(out, size, "OVF");
}
}

namespace psu {
void UiFrame::largeNumber(uint8_t row, const char* value, uint8_t style) {
  const uint8_t n = strlen(value); if (n > 8 || row + 1 >= ui::rows) return;
  const uint8_t first = (ui::columns - n * 2) / 2;
  for (uint8_t i = 0; i < n; ++i) {
    const char c = value[i]; const uint8_t digit = c >= '0' && c <= '9' ? c - '0' : c == '.' ? 10 : 11;
    for (uint8_t q = 0; q < 4; ++q)
      cell(uint16_t(row + q / 2) * ui::columns + first + 2 * i + q % 2, {char(128 + digit * 4 + q),style});
  }
}
}
