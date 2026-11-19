#include "ui/WheelInput.h"
#include <Arduino.h>
#include <TimerOne.h>
#include <iostream>
#include <stdexcept>
#include <string>

Timer Timer1;
namespace {
uint32_t timeNow = 0;
// Constant initialization precedes the driver's global constructor.
uint8_t pins[] = {HIGH, HIGH, HIGH, HIGH, HIGH, HIGH};
uint8_t configured = 0;
unsigned checks = 0;
void check(bool ok, const char* expression, unsigned line) {
  ++checks;
  if (!ok) throw std::runtime_error("wheel:" + std::to_string(line) + " " + expression);
}
#define CHECK(v) check((v), #v, __LINE__)
struct Events { int movement = 0; unsigned clicks = 0, holds = 0, resets = 0; };
void advance(psu::WheelInput& wheel, unsigned milliseconds, Events& events, bool read = true) {
  for (unsigned i = 0; i < milliseconds; ++i) {
    ++timeNow;
    Timer1.callback(); // The same callback registered by WheelInput::begin.
    if (!read) continue;
    const auto event = wheel.read();
    events.movement += event.delta;
    events.clicks += event.clicked;
    events.holds += event.held;
    events.resets += wheel.takeResetRequest();
  }
}
void click(psu::WheelInput& wheel, Events& events) {
  pins[5] = LOW; advance(wheel, 150, events);
  pins[5] = HIGH; advance(wheel, 60, events);
}
void rotate(psu::WheelInput& wheel, bool reverse, Events& events) {
  const uint8_t forward[] = {1, 0, 2, 3}, backward[] = {2, 0, 1, 3};
  for (uint8_t i = 0; i < 4; ++i) {
    const uint8_t state = reverse ? backward[i] : forward[i];
    pins[3] = state >> 1; pins[4] = state & 1;
    advance(wheel, 5, events);
  }
}
}
uint32_t millis() { return timeNow; }
int digitalRead(uint8_t pin) { CHECK(pin >= 3 && pin <= 5); return pins[pin]; }
void pinMode(uint8_t pin, uint8_t mode) {
  CHECK(pin >= 3 && pin <= 5 && mode == INPUT_PULLUP);
  configured |= 1U << (pin - 3);
}

int main() {
  try {
    psu::WheelInput wheel; wheel.begin();
    CHECK(configured == 7 && Timer1.period == 1000 && Timer1.callback);
    Events idle; advance(wheel, 100, idle);
    CHECK(!idle.clicks && !idle.holds && !idle.movement);
    Events shortClick; click(wheel, shortClick);
    CHECK(shortClick.clicks == 1 && shortClick.holds == 0);
    Events rapid; click(wheel, rapid); click(wheel, rapid);
    CHECK(rapid.clicks == 2 && !rapid.holds);

    Events hold;
    pins[5] = LOW; advance(wheel, 1400, hold); advance(wheel, 1000, hold);
    CHECK(hold.holds == 0 && !hold.clicks);
    pins[5] = HIGH; advance(wheel, 60, hold);
    CHECK(hold.holds == 1 && !hold.clicks);
    Events forward, backward;
    rotate(wheel, false, forward); rotate(wheel, true, backward);
    CHECK((forward.movement == 1 || forward.movement == -1) && backward.movement == -forward.movement);

    // Popup interrupts a press. Its eventual hold/release cannot dismiss it.
    Events cancelled;
    pins[5] = LOW; advance(wheel, 100, cancelled); wheel.cancelGesture();
    advance(wheel, 1400, cancelled);
    pins[5] = HIGH; advance(wheel, 70, cancelled);
    CHECK(!cancelled.clicks && !cancelled.holds);
    Events guardRotation; rotate(wheel, false, guardRotation);
    CHECK(guardRotation.movement == forward.movement);
    Events fresh; click(wheel, fresh); CHECK(fresh.clicks == 1 && !fresh.holds);

    // An already queued Clicked event must be consumed when a popup appears.
    Events queued;
    pins[5] = LOW; advance(wheel, 150, queued, false);
    pins[5] = HIGH; advance(wheel, 20, queued, false);
    wheel.cancelGesture(); advance(wheel, 70, queued);
    CHECK(!queued.clicks && !queued.holds);
    click(wheel, queued); CHECK(queued.clicks == 1 && !queued.holds);

    // A brief release does not qualify an old gesture as a new press.
    Events bounce; wheel.cancelGesture(); advance(wheel, 10, bounce);
    pins[5] = LOW; advance(wheel, 20, bounce);
    pins[5] = HIGH; advance(wheel, 20, bounce);
    pins[5] = LOW; advance(wheel, 1400, bounce);
    CHECK(!bounce.clicks && !bounce.holds);
    pins[5] = HIGH; advance(wheel, 70, bounce);
    click(wheel, bounce); CHECK(bounce.clicks == 1 && !bounce.holds);

    Events rollover;
    timeNow = UINT32_MAX - 20; wheel.cancelGesture(); advance(wheel, 70, rollover);
    click(wheel, rollover); CHECK(rollover.clicks == 1 && !rollover.holds);
    // The recovery hold performs no shorter hold action and consumes rotation.
    Events recovery; pins[5]=LOW; advance(wheel,9999,recovery);
    CHECK(!recovery.holds && !recovery.clicks && !recovery.resets);
    advance(wheel,2,recovery); CHECK(recovery.resets==1);
    advance(wheel,12000,recovery); rotate(wheel,false,recovery);
    CHECK(recovery.resets==1 && !recovery.holds && !recovery.clicks && !recovery.movement);
    // Release bounce and queued Held/Released events cannot become clicks.
    pins[5]=HIGH; advance(wheel,20,recovery); pins[5]=LOW; advance(wheel,30,recovery);
    pins[5]=HIGH; advance(wheel,100,recovery);
    CHECK(recovery.resets==1 && !recovery.holds && !recovery.clicks && !recovery.movement);
    click(wheel,recovery); CHECK(recovery.clicks==1 && !recovery.holds && recovery.resets==1);

    Events shorter; pins[5]=LOW; advance(wheel,9900,shorter);
    CHECK(!shorter.clicks && !shorter.holds && !shorter.resets);
    pins[5]=HIGH; advance(wheel,100,shorter);
    CHECK(shorter.holds==1 && !shorter.clicks && !shorter.resets);

    // A popup cancels ordinary input, but cannot make display recovery
    // unreachable by continually resetting its independent physical timer.
    Events interrupted; pins[5]=LOW; advance(wheel,2000,interrupted);
    wheel.cancelGesture(); advance(wheel,8002,interrupted);
    CHECK(interrupted.resets==1 && !interrupted.clicks && !interrupted.holds);
    pins[5]=HIGH; advance(wheel,100,interrupted);
    CHECK(!interrupted.clicks && !interrupted.holds);

    Events wrapReset; timeNow=UINT32_MAX-5000; pins[5]=LOW;
    advance(wheel,10002,wrapReset); CHECK(wrapReset.resets==1 && !wrapReset.holds && !wrapReset.clicks);
    pins[5]=HIGH; advance(wheel,100,wrapReset);
    CHECK(!wrapReset.holds && !wrapReset.clicks);
    click(wheel,wrapReset); CHECK(wrapReset.clicks==1 && wrapReset.resets==1);
    Events serialReset; pins[5]=LOW; advance(wheel,2000,serialReset);
    wheel.consumeResetGesture(); advance(wheel,15000,serialReset);
    pins[5]=HIGH; advance(wheel,100,serialReset);
    CHECK(!serialReset.resets && !serialReset.holds && !serialReset.clicks);
    click(wheel,serialReset); CHECK(serialReset.clicks==1);
    std::cout << "PASS: real ClickEncoder single/repeated clicks, hold/release, rotation, popup guard, queued click, bounce and rollover ("
              << checks << " checks)\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n'; return 1;
  }
}
