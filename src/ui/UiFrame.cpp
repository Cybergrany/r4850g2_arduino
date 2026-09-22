#include "UiFrame.h"
#include <string.h>
#include <math.h>
#include <stdio.h>
namespace psu {
namespace {
char read(const char* p, bool flash) {
#ifdef __AVR__
  return flash ? pgm_read_byte(p) : *p;
#else
  (void)flash; return *p;
#endif
}
}
void UiFrame::clear() { memset(characters_, ' ', sizeof(characters_)); memset(styles_, 0, sizeof(styles_)); }
void UiFrame::invalidate() { memset(characters_, 0, sizeof(characters_)); memset(styles_, 0xff, sizeof(styles_)); }
UiCell UiFrame::cell(uint16_t i) const { return {characters_[i], uint8_t((styles_[i / 2] >> (i % 2 * 4)) & 15)}; }
void UiFrame::cell(uint16_t i, UiCell v) {
  characters_[i] = v.character;
  const uint8_t shift = i % 2 * 4;
  styles_[i / 2] = (styles_[i / 2] & uint8_t(~(15 << shift))) | ((v.style & 15) << shift);
}
void UiFrame::put(uint8_t row, uint8_t col, uint8_t width, const char* value, bool flash, uint8_t style, bool right) {
  if (row >= ui::rows || col >= ui::columns) return;
  if (width > ui::columns - col) width = ui::columns - col;
  uint8_t n = 0; while (n < width && read(value + n, flash)) ++n;
  const bool cut = n == width && read(value + n, flash);
  const uint8_t pad = right && !cut ? width - n : 0;
  for (uint8_t i = 0; i < width; ++i) {
    char c = i >= pad && i < n + pad ? read(value + i - pad, flash) : ' ';
    if (cut && i == width - 1) c = '~';
    if (c < 32 || c > 126) c = '?';
    cell(uint16_t(row) * ui::columns + col + i, {c, style});
  }
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
  if (!isfinite(value)) { snprintf(out, size, "--"); return; }
  const bool negative = value < 0; float magnitude = fabsf(value);
  if (decimals > 2) decimals = 2;
  const char prefixes[] = {'\0', 'k', 'M'};
  for (uint8_t scale = 0; scale < 3; ++scale, magnitude /= 1000) {
    for (int8_t precision = decimals; precision >= 0; --precision) {
      const uint16_t divisor = precision == 2 ? 100 : precision == 1 ? 10 : 1;
      if (magnitude * divisor >= 4294967040.0f) continue;
      const uint32_t rounded = uint32_t(magnitude * divisor + 0.5f);
      char number[24];
      int n = snprintf(number, sizeof(number), "%s%lu", negative && rounded ? "-" : "", (unsigned long)(rounded / divisor));
      if (precision) n += snprintf(number + n, sizeof(number) - n, ".%0*u", precision, unsigned(rounded % divisor));
      if (scale) number[n++] = prefixes[scale];
      number[n] = 0;
      if (n + strlen(unit) < size) { strcpy(out, number); strcat(out, unit); return; }
    }
  }
  snprintf(out, size, "OVF");
}
}
