# Standalone wheel serial test

A separate Mega sketch for checking the wheel wiring before investigating the
charger UI. It uses Arduino GPIO and Serial only: no ClickEncoder, TimerOne,
display, CAN or EEPROM libraries. Both rotation inputs are polled, including
D4; no external-interrupt capability is required on that pin.

## Wiring and flashing

| Wheel connection | Mega connection |
| --- | --- |
| CLK / A | D3 |
| DT / B | D4 |
| SW / push button | D5 |
| Common / GND | GND |

All three inputs use `INPUT_PULLUP`; contacts close to GND. Bare mechanical
contacts need no VCC connection. If using a powered encoder module, retain its
appropriate supply and common ground. A and B may rest HIGH or LOW depending
on wheel position; the released button should read HIGH.

Close the serial monitor, then run from the repository root:

```sh
pio run -d examples/wheel_bench -t upload --upload-port COM15
pio device monitor -d examples/wheel_bench --port COM15 -b 115200
```

Alternatively open `wheel_bench.ino` in Arduino IDE and select **Arduino Mega
or Mega 2560**; no extra libraries are needed.

The sketch replaces the charger application. It holds CHG_ALLOW D27 and status
outputs D28-D30 LOW from `setup()`, and deselects CAN, TFT and touch. It sends
no PSU commands. Without a fitted AC cutoff, this test does not stop a PSU
that was already running. EEPROM configuration/calibration are untouched.

## Reading the output

At **115200 baud**, expect lines like:

```text
A=1 B=1 SW=1 (UP) pos=0 q=0 dir=. edges=0/0 bad=0 press=0 release=0 hold=0
A=1 B=1 SW=1 (UP) pos=1 q=0 dir=+ edges=2/2 bad=0 press=0 release=0 hold=0
A=1 B=1 SW=0 (DOWN) pos=1 q=0 dir=+ edges=2/2 bad=0 press=1 release=0 hold=0
```

Changes are reported at most ten times per second, with an idle status every
second. GPIO sampling continues while serial output drains. Snapshots need not
show every raw state; the edge counters retain observed changes between them.

- `A`, `B`, `SW`: raw D3, D4 and D5 levels (1=HIGH, 0=LOW).
- `pos`: signed position, using four quadrature transitions per notch to match
  the current firmware configuration. `q` is the unfinished portion of a notch.
- `dir`: last complete step direction, `+` or `-`; `.` means no step yet.
  Which sign corresponds to clockwise depends on the wheel's A/B wiring.
- `edges`: observed A/B change counts, including contact bounce.
- `bad`: both A and B changed between samples, so direction was ambiguous.
  This can indicate missed transitions or noisy wiring; it does not alone prove
  a defective wheel. Turn slowly for the initial check.
- `press`, `release`, `hold`: debounced button event counts. Hold increments
  once after one second down; debounce is 20 ms. These are this diagnostic's
  thresholds, independent of the UI library.

Turn slowly several notches in each direction: both edge counts should rise,
and `pos` should move in opposite directions. Some wheels use two transitions
per physical detent, so a different notch-to-count ratio is not necessarily a
fault. Press/release the button, then hold it for over a second: expect DOWN,
UP and HELD, with the corresponding counters incrementing.

Commands need **no Enter**: `h` or `?` repeats help, `s` requests current status,
and `z` clears counters/position without changing the physical button state.
If the serial monitor reconnects without resetting the Mega, counters continue.

If a pin stays HIGH, check the common ground and that pin's connection. For a
direct GPIO check, disconnect the wheel's three signal wires from D3-D5, then
temporarily connect each Mega input to GND in turn: `s` should show that input
as 0. Releasing it should return it to 1. Only do this on these configured
inputs with the external signal wires disconnected.

Passing the test establishes basic wheel/input operation; it does not validate
the production UI navigation or its timer/library integration.

To return to the charger firmware:

```sh
pio run -e mega2560_wheel_alerts -t upload --upload-port COM15
```

The charger then loads its saved configuration, including enable preference
and protection state.
