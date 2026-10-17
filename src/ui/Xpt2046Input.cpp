/*
 * Conversion transaction adapted from Paul Stoffregen's XPT2046_Touchscreen:
 * https://github.com/PaulStoffregen/XPT2046_Touchscreen
 * Copyright (c) 2015, Paul Stoffregen, paul@pjrc.com
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice, development funding notice, and this permission
 * notice shall be included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include "Xpt2046Input.h"
#if PSU_ENABLE_TOUCH
#include <Arduino.h>
#include <SPI.h>
namespace psu {
void Xpt2046Input::begin() {
  pinMode(touch::chipSelect, OUTPUT); digitalWrite(touch::chipSelect, HIGH);
  pinMode(touch::interruptPin, INPUT); SPI.begin();
}
bool Xpt2046Input::read(TouchSample& s, uint32_t now) {
  if (uint32_t(now-lastRead_) < touch::sampleMs) return false;
  lastRead_ = now; s = {0,0,0,false};
  if (!contact_ && digitalRead(touch::interruptPin)) return true;
  const uint32_t started = micros();
  // Pipelined 12-bit differential conversions: Z1, Z2, discard first X,
  // then three X/Y pairs. Last Y powers down the ADC to restore PENIRQ.
  // No touch ISR or wait-for-release loop; SPI masks CAN RX only for this burst.
  SPI.beginTransaction(SPISettings(touch::spiHz, MSBFIRST, SPI_MODE0));
  digitalWrite(touch::chipSelect, LOW);
  SPI.transfer(0xb1);
  const int16_t z1 = SPI.transfer16(0xc1) >> 3;
  const int16_t z2 = SPI.transfer16(0x91) >> 3;
  SPI.transfer16(0x91);
  const uint16_t x1 = SPI.transfer16(0xd1) >> 3, y1 = SPI.transfer16(0x91) >> 3;
  const uint16_t x2 = SPI.transfer16(0xd1) >> 3, y2 = SPI.transfer16(0x91) >> 3;
  const uint16_t x3 = SPI.transfer16(0xd0) >> 3, y3 = SPI.transfer16(0) >> 3;
  digitalWrite(touch::chipSelect, HIGH); SPI.endTransaction();
  s = {int16_t((x1+x2+x3)/3), int16_t((y1+y2+y3)/3), int16_t(z1+4095-z2), false};
  s.down = s.pressure >= int16_t(touch::pressureMinimum);
  contact_ = s.down;
  const uint32_t elapsed = micros()-started; if (elapsed > maximumMicros_) maximumMicros_ = elapsed;
  return true;
}
}
#endif
