// Standalone Mega wheel diagnostic. No charger, display, CAN, ClickEncoder,
// TimerOne or EEPROM code. Open in Arduino IDE or use the adjacent PIO project.
#include <Arduino.h>
#include <stdio.h>
#include <string.h>

constexpr uint8_t WHEEL_CLK = 3, WHEEL_DT = 4, WHEEL_SW = 5;
constexpr int8_t TRANSITIONS_PER_NOTCH = 4;
constexpr uint32_t BUTTON_DEBOUNCE_MS = 20, BUTTON_HOLD_MS = 1000;
constexpr uint32_t REPORT_MIN_MS = 100, REPORT_IDLE_MS = 1000;

uint8_t ab = 0;
bool rawPressed = false, pressed = false, holdReported = false;
uint32_t buttonChangedAt = 0, pressedAt = 0, reportAt = 0, heartbeatAt = 0;
uint32_t clockEdges = 0, dataEdges = 0, invalidTransitions = 0;
uint32_t pressCount = 0, releaseCount = 0, holdCount = 0;
int32_t position = 0;
int8_t quarterSteps = 0;
char lastDirection = '.';
bool dirty = true, statusRequested = true, helpRequested = true, heartbeat = false;

// Drain serial cooperatively so a full UART buffer does not stall pin polling.
char output[240];
size_t outputLength = 0, outputSent = 0;

void setOutput(uint8_t pin, uint8_t level) {
  digitalWrite(pin, level);
  pinMode(pin, OUTPUT);
}

uint8_t readAB() {
  return (digitalRead(WHEEL_CLK) == HIGH ? 2 : 0) |
         (digitalRead(WHEEL_DT) == HIGH ? 1 : 0);
}

void sampleInputs(uint32_t now) {
  const uint8_t next = readAB(), changed = ab ^ next;
  if (changed) {
    if (changed & 2) ++clockEdges;
    if (changed & 1) ++dataEdges;
    if (changed == 3) {
      // Both bits changed between samples: direction is ambiguous. Resync.
      ++invalidTransitions;
      quarterSteps = 0;
    } else {
      quarterSteps += ((ab & 1) ^ (next >> 1)) ? 1 : -1;
      if (quarterSteps >= TRANSITIONS_PER_NOTCH) {
        ++position; quarterSteps = 0; lastDirection = '+';
      } else if (quarterSteps <= -TRANSITIONS_PER_NOTCH) {
        --position; quarterSteps = 0; lastDirection = '-';
      }
    }
    ab = next;
    dirty = true;
  }

  const bool nextPressed = digitalRead(WHEEL_SW) == LOW;
  if (nextPressed != rawPressed) {
    rawPressed = nextPressed;
    buttonChangedAt = now;
    dirty = true;
  }
  if (pressed != rawPressed && uint32_t(now - buttonChangedAt) >= BUTTON_DEBOUNCE_MS) {
    pressed = rawPressed;
    if (pressed) { ++pressCount; pressedAt = now; holdReported = false; }
    else { ++releaseCount; holdReported = false; }
    dirty = true;
  }
  if (pressed && rawPressed && !holdReported && uint32_t(now - pressedAt) >= BUTTON_HOLD_MS) {
    ++holdCount;
    holdReported = true;
    dirty = true;
  }
}

void clearCounters() {
  clockEdges = dataEdges = invalidTransitions = 0;
  pressCount = releaseCount = holdCount = 0;
  position = 0; quarterSteps = 0; lastDirection = '.';
  statusRequested = true;
}

void serviceSerial(uint32_t now) {
  if (Serial.available()) {
    switch (Serial.read()) {
      case 'h': case 'H': case '?': helpRequested = true; break;
      case 's': case 'S': statusRequested = true; break;
      case 'z': case 'Z': clearCounters(); break;
      default: break; // Commands need no Enter; CR/LF are ignored.
    }
  }

  if (outputSent == outputLength) {
    if (helpRequested) {
      strncpy_P(output, PSTR(
          "Standalone wheel test: CLK=D3 DT=D4 SW=D5; INPUT_PULLUP, contacts to GND.\r\n"
          "CHG_ALLOW/status outputs LOW; no CAN/display/EEPROM.\r\n"
          "h/?=help  s=status  z=zero counters; no Enter needed.\r\n"
          "dir +/- is relative; 4 transitions/notch.\r\n"), sizeof(output) - 1);
      output[sizeof(output) - 1] = '\0';
      helpRequested = false;
    } else if (statusRequested || (dirty && uint32_t(now - reportAt) >= REPORT_MIN_MS) ||
               uint32_t(now - reportAt) >= REPORT_IDLE_MS) {
      snprintf(output, sizeof(output),
          "A=%u B=%u SW=%u (%s) pos=%ld q=%d dir=%c edges=%lu/%lu bad=%lu press=%lu release=%lu hold=%lu\r\n",
          unsigned(ab >> 1), unsigned(ab & 1), unsigned(!rawPressed),
          pressed ? (holdReported ? "HELD" : "DOWN") : "UP",
          long(position), int(quarterSteps), lastDirection,
          (unsigned long)clockEdges, (unsigned long)dataEdges, (unsigned long)invalidTransitions,
          (unsigned long)pressCount, (unsigned long)releaseCount, (unsigned long)holdCount);
      statusRequested = false; dirty = false; reportAt = now;
    } else return;
    outputLength = strlen(output);
    outputSent = 0;
  }

  const int freeBytes = Serial.availableForWrite();
  if (freeBytes > 0) {
    size_t count = outputLength - outputSent;
    if (count > size_t(freeBytes)) count = size_t(freeBytes);
    if (count > 16) count = 16;
    outputSent += Serial.write(reinterpret_cast<const uint8_t*>(output + outputSent), count);
  }
}

void setup() {
  // Match the wheel/alerts bench wiring while keeping other devices inactive.
  setOutput(27, LOW); // CHG_ALLOW
  setOutput(28, LOW); // WARNING
  setOutput(29, LOW); // ERROR
  setOutput(30, LOW); // PSU_ACTIVE
  setOutput(10, HIGH); // MCP2515 CS
  setOutput(22, HIGH); // TFT CS
  setOutput(24, HIGH); // TFT RESET
  setOutput(25, HIGH); // Touch CS, if wired through the existing logic interface
  setOutput(53, HIGH); // Hardware SS
  setOutput(LED_BUILTIN, LOW);
  pinMode(WHEEL_CLK, INPUT_PULLUP);
  pinMode(WHEEL_DT, INPUT_PULLUP);
  pinMode(WHEEL_SW, INPUT_PULLUP);
  ab = readAB();
  rawPressed = digitalRead(WHEEL_SW) == LOW;
  buttonChangedAt = millis();
  Serial.begin(115200); // No wait for the monitor or any peripheral.
}

void loop() {
  const uint32_t now = millis();
  sampleInputs(now);
  serviceSerial(now);
  if (uint32_t(now - heartbeatAt) >= 500) {
    heartbeatAt = now;
    heartbeat = !heartbeat;
    digitalWrite(LED_BUILTIN, heartbeat ? HIGH : LOW);
  }
}
