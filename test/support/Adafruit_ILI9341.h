#pragma once
// Header surface only. test_touch supplies an inert display driver; production
// Application/LocalUi/serial/simulator code still runs in the integration tests.
#include <stdint.h>
class Adafruit_GFX {
 public:
  Adafruit_GFX(int16_t,int16_t) {}
  virtual ~Adafruit_GFX() = default;
  virtual void drawPixel(int16_t,int16_t,uint16_t) = 0;
};
class Adafruit_ILI9341 {};
