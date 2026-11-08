// Standalone Mega display test: no charger headers, CAN, touch, wheel or EEPROM.
// Open this sketch directly in Arduino IDE, or use the adjacent PlatformIO project.
#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>

// Match mega2560_wheel_alerts wiring. Hardware SPI: MISO 50, MOSI 51, SCK 52.
constexpr uint8_t TFT_CS = 22;
constexpr uint8_t TFT_DC = 23;
constexpr uint8_t TFT_RST = 24;
constexpr uint8_t CAN_CS = 10;
constexpr uint8_t TOUCH_CS = 25;
constexpr uint8_t CHG_ALLOW = 27;
constexpr uint8_t WARNING_OUT = 28;
constexpr uint8_t ERROR_OUT = 29;
constexpr uint8_t PSU_ACTIVE = 30;
constexpr uint32_t TFT_SPI_HZ = 500000UL;
constexpr uint32_t PATTERN_HOLD_MS = 2000UL;
constexpr uint8_t PATTERN_COUNT = 10;

Adafruit_ILI9341 tft(TFT_CS, TFT_DC, TFT_RST);
uint8_t pattern = 0;
uint32_t drawnAt = 0;
uint32_t heartbeatAt = 0;
bool heartbeat = false;
bool pinTest = false;
uint8_t pinTestStep = 0;

void setOutput(uint8_t pin, uint8_t level) {
  digitalWrite(pin, level);
  pinMode(pin, OUTPUT);
}

void printHexByte(uint8_t value) {
  if (value < 16) Serial.print('0');
  Serial.print(value, HEX);
}

void reportId() {
  const uint8_t high = tft.readcommand8(ILI9341_RDID4, 2);
  const uint8_t low = tft.readcommand8(ILI9341_RDID4, 3);
  Serial.print(F("Display ID = 0x"));
  printHexByte(high);
  printHexByte(low);
  Serial.println(F("; expected 0x9341. Diagnostic only; drawing is never blocked."));
}

void pinTestIdle() {
  setOutput(CAN_CS, HIGH);
  setOutput(TOUCH_CS, HIGH);
  // Deselect the display before changing its clock/data/reset lines.
  setOutput(TFT_CS, HIGH);
  setOutput(TFT_DC, LOW);
  setOutput(MOSI, LOW);
  setOutput(SCK, LOW);
  setOutput(TFT_RST, HIGH);
}

void showPinTestStep() {
  pinTestIdle();
  if (pinTestStep >= 10) {
    Serial.println(F("PIN TEST complete. p repeats; reset the Mega to resume graphics."));
    return;
  }
  const uint8_t pins[] = {TFT_CS, TFT_DC, TFT_RST, MOSI, SCK};
  const uint8_t index = pinTestStep / 2;
  const uint8_t level = pinTestStep % 2 ? HIGH : LOW;
  digitalWrite(pins[index], level);
  Serial.print(F("PIN TEST "));
  switch (index) {
    case 0: Serial.print(F("CS")); break;
    case 1: Serial.print(F("DC")); break;
    case 2: Serial.print(F("RESET")); break;
    case 3: Serial.print(F("SDI/MOSI")); break;
    case 4: Serial.print(F("SCK")); break;
  }
  Serial.print(F(" on Mega D")); Serial.print(pins[index]);
  if (level == HIGH)
    Serial.println(F(" = HIGH: expect ~5 V at Mega, ~3.3 V at TFT after level shifting."));
  else
    Serial.println(F(" = LOW: expect ~0 V at both Mega and TFT."));
  Serial.println(F("Held until n. Measure at TFT header relative to TFT GND. p restarts."));
}

void beginPinTest() {
  // No display SPI calls are allowed again until a board reset. In particular,
  // CS stays HIGH while SCK/MOSI are tested; CAN CS stays HIGH throughout.
  SPI.end();
  pinTest = true; pinTestStep = 0;
  Serial.println(F("PIN TEST: graphics paused; CAN/touch deselected; MISO remains an input."));
  showPinTestStep();
}

void drawAlternatingPattern(uint8_t cellPixels, bool columns, bool rows) {
  // One landscape scanline, not a framebuffer. Stream the whole pattern once
  // so fine checks do not incur a separate address-window command per pixel.
  uint16_t pixels[ILI9341_TFTHEIGHT];
  const uint16_t width = tft.width(), height = tft.height();
  tft.startWrite();
  tft.setAddrWindow(0, 0, width, height);
  for (uint16_t y = 0; y < height; ++y) {
    if (y == 0 || (rows && y % cellPixels == 0)) {
      const uint16_t row = rows ? y / cellPixels : 0;
      for (uint16_t x = 0; x < width; ++x) {
        const uint16_t column = columns ? x / cellPixels : 0;
        pixels[x] = (row + column) % 2 ? ILI9341_BLACK : ILI9341_WHITE;
      }
    }
    tft.writePixels(pixels, width);
  }
  tft.endWrite();
}

void drawPattern() {
  const uint16_t colors[] = {
    ILI9341_RED, ILI9341_GREEN, ILI9341_BLUE, ILI9341_WHITE, ILI9341_BLACK
  };
  Serial.print(F("Drawing "));
  switch (pattern) {
    case 0: Serial.println(F("RED")); break;
    case 1: Serial.println(F("GREEN")); break;
    case 2: Serial.println(F("BLUE")); break;
    case 3: Serial.println(F("WHITE")); break;
    case 4: Serial.println(F("BLACK (intentionally blank)")); break;
    case 5: Serial.println(F("TEXT / BORDER / COLOR BARS")); break;
    case 6: Serial.println(F("CHECKERBOARD (8 x 8 pixels)")); break;
    case 7: Serial.println(F("CHECKERBOARD (1 x 1 pixel)")); break;
    case 8: Serial.println(F("VERTICAL STRIPES (1 pixel wide)")); break;
    case 9: Serial.println(F("HORIZONTAL STRIPES (1 pixel high)")); break;
  }
  Serial.flush(); // Show progress before the deliberately slow full-screen fill.
  const uint32_t started = millis();
  if (pattern < 5) tft.fillScreen(colors[pattern]);
  else if (pattern == 5) {
    tft.fillScreen(ILI9341_BLACK);
    tft.drawRect(0, 0, tft.width(), tft.height(), ILI9341_WHITE);
    tft.setTextWrap(false);
    tft.setTextSize(2);
    tft.setTextColor(ILI9341_WHITE);
    tft.setCursor(12, 12);
    tft.println(F("ILI9341 STANDALONE TEST"));
    tft.setCursor(12, 44);
    tft.print(F("MEGA / ")); tft.print(TFT_SPI_HZ / 1000UL); tft.println(F(" kHz SPI"));
    tft.setCursor(12, 76);
    tft.println(F("CS22 DC23 RESET24"));
    tft.setCursor(12, 108);
    tft.println(F("MISO50 MOSI51 SCK52"));
    tft.setCursor(12, 140);
    tft.println(F("No CAN or touch"));
    for (uint8_t i = 0; i < 5; ++i)
      tft.fillRect(10 + i * 60, 182, 60, 42, colors[i]);
    tft.drawRect(10, 182, 300, 42, ILI9341_WHITE);
  } else {
    switch (pattern) {
      case 6: drawAlternatingPattern(8, true, true); break;
      case 7: drawAlternatingPattern(1, true, true); break;
      case 8: drawAlternatingPattern(1, true, false); break;
      case 9: drawAlternatingPattern(1, false, true); break;
    }
  }
  drawnAt = millis();
  Serial.print(F("Draw complete in "));
  Serial.print(uint32_t(drawnAt - started));
  Serial.print(F(" ms; holding for "));
  Serial.print(PATTERN_HOLD_MS);
  Serial.println(F(" ms."));
}

void initialiseDisplay() {
  Serial.print(F("Initializing ILI9341 at ")); Serial.print(TFT_SPI_HZ);
  Serial.println(F(" Hz, landscape rotation 1..."));
  Serial.flush();
  tft.begin(TFT_SPI_HZ);
  tft.setRotation(1);
  reportId();
  pattern = 0;
  drawPattern();
}

void setup() {
  // This sketch does not manage a PSU. Keep the fitted AC permissive/status LOW.
  setOutput(CHG_ALLOW, LOW);
  setOutput(PSU_ACTIVE, LOW);
  setOutput(WARNING_OUT, LOW);
  setOutput(ERROR_OUT, LOW);
  setOutput(CAN_CS, HIGH);
  setOutput(TOUCH_CS, HIGH); // If wired, use the existing 3.3 V logic interface.
  setOutput(TFT_CS, HIGH);
  setOutput(SS, HIGH); // Mega D53 must remain an output for hardware SPI master.
  setOutput(LED_BUILTIN, LOW);

  Serial.begin(115200); // Never wait for the serial monitor.
  Serial.println(F("Standalone ILI9341 bench test; CHG_ALLOW LOW; EEPROM untouched."));
  Serial.println(F("Commands: r = restart patterns, i = read ID, n = next pattern."));
  Serial.println(F("p = multimeter pin test; n advances held levels; board reset exits."));
  initialiseDisplay();
}

void loop() {
  if (Serial.available()) {
    const char command = Serial.read();
    if (command == 'p' || command == 'P') beginPinTest();
    else if (pinTest) {
      if ((command == 'n' || command == 'N') && pinTestStep < 10) {
        ++pinTestStep; showPinTestStep();
      } else if (command == 'i' || command == 'I' || command == 'r' || command == 'R') {
        Serial.println(F("PIN TEST active: n advances, p restarts; reset the Mega to resume graphics."));
      }
    } else if (command == 'r' || command == 'R') {
      reportId();
      pattern = 0;
      drawPattern();
    } else if (command == 'i' || command == 'I') reportId();
    else if (command == 'n' || command == 'N') {
      pattern = (pattern + 1) % PATTERN_COUNT;
      drawPattern();
    }
    // CR/LF and other characters are ignored; commands need no line ending.
  }
  const uint32_t now = millis();
  if (uint32_t(now - heartbeatAt) >= 500UL) {
    heartbeatAt = now;
    heartbeat = !heartbeat;
    digitalWrite(LED_BUILTIN, heartbeat ? HIGH : LOW);
  }
  if (!pinTest && uint32_t(now - drawnAt) >= PATTERN_HOLD_MS) {
    pattern = (pattern + 1) % PATTERN_COUNT;
    drawPattern();
  }
}
