# Standalone ILI9341 bench test

A separate Arduino sketch / PlatformIO project using the same pins, display
library versions, **500 kHz** clock and landscape rotation as
`mega2560_wheel_alerts`. It includes no charger firmware headers, CAN driver,
touch, wheel, scheduler or EEPROM access. The normal firmware is unchanged.

Unlike the charger UI, this sketch **draws even if the display ID cannot be
read**. It reports the ID over serial as a separate diagnostic. This separates
display writing from the production display-presence check; it does not prove
that the display and CAN work together.

## Flash and run

From the repository root, with the serial monitor closed:

```sh
pio run -d examples/ili9341_bench
pio run -d examples/ili9341_bench -t upload --upload-port COM15
pio device monitor -d examples/ili9341_bench --port COM15 -b 115200
```

Alternatively open `ili9341_bench.ino` in Arduino IDE, select **Arduino Mega or
Mega 2560**, and install Adafruit ILI9341 1.6.3, Adafruit GFX Library 1.12.6 and
Adafruit BusIO 1.17.4 through Library Manager.

The sketch replaces the running charger application. It holds **CHG_ALLOW D27,
PSU_ACTIVE D30, WARNING D28 and ERROR D29 LOW** from `setup()` and sends no CAN
commands. These status outputs are inactive test outputs, not charger-health
indicators. Without a fitted AC cutoff, this test does not stop a PSU that was
already running. EEPROM settings and touch calibration are not written.

## Existing wiring

| Signal | Mega pin |
| --- | --- |
| TFT SDO / MISO | D50; used only for diagnostic reads |
| TFT SDI / MOSI | D51 |
| TFT SCK | D52 |
| TFT CS | D22 |
| TFT DC | D23 |
| TFT RESET | D24 |
| MCP2515 CS | D10, held HIGH |
| Touch T_CS | D25, held HIGH if connected through the existing logic interface |

Retain the existing level shifting, common ground, display supply and backlight
power. There is no GPIO-controlled backlight in this test, matching the current
firmware. If touch is physically disconnected, retain its external T_CS pull-up
to 3.3 V; driving an unconnected D25 does not bias the module. D53 is held HIGH
as an output to keep the Mega's hardware SPI in controller mode.

Pins and clock are declared at the top of the sketch, deliberately independent
of the charger configuration files. They match the current wheel/alerts profile.

## Expected behavior

At **115200 baud**, serial reports initialization, the display ID (normally
`0x9341`), then each drawing step. The screen repeats:

1. Solid red.
2. Solid green.
3. Solid blue.
4. Solid white.
5. Solid black (this step is intentionally blank).
6. White text, a white outer border and color bars on black.
7. Black/white checkerboard with 8 x 8 pixel squares.
8. Black/white checkerboard with single-pixel squares.
9. Alternating black/white vertical stripes, one pixel wide.
10. Alternating black/white horizontal stripes, one pixel high.

Each pattern stays for two seconds **after** drawing finishes. A full-screen
fill at 500 kHz takes about 2.5 seconds plus software overhead; allow roughly a
minute to observe the sequence. The checkerboards and stripes cover the whole
screen without overlaid text; serial identifies each pattern. Fine patterns
use a single 640-byte scanline buffer to avoid slow per-pixel drawing calls.
The built-in LED blinks between drawing steps;
full-screen transfers intentionally block this minimal test, so pauses in the
LED and serial command handling during drawing are expected.

Single-character serial commands work without Enter; CR/LF are ignored:

- `r`: restart the pattern sequence, including the ID report.
- `i`: read and print the display ID again.
- `n`: advance to the next pattern immediately.
- `p`: enter the multimeter signal test described below.

If the monitor opens after boot, send `r` to repeat the ID and patterns. No serial
connection is needed for automatic patterns.

| Observation | What it establishes |
| --- | --- |
| Patterns visible and ID `0x9341` | Basic initialization, display writes and ID reads work independently of the charger application. |
| Patterns visible but ID differs / is `0x0000` or `0xFFFF` | Writing works; readback or display identification needs investigation. The production UI currently requires ID `0x9341`. |
| Serial cycles through completed draws, but screen stays blank | The problem reproduces without the charger application; power/backlight, wiring, signal levels, controller type and the shared display-library initialization remain candidates. |
| No serial progress | Does not isolate the screen; first confirm that this sketch is running on the intended Mega/port. |

Serial completion means that the MCU issued SPI writes, not that the panel
acknowledged or displayed them. This test does not exercise simultaneous CAN
traffic, the production renderer or touch.

## Multimeter signal test

If supply, backlight and idle CS/reset voltages are correct but there are still
no patterns, send **`p`**. Wait for any current full-screen draw to finish.
The sketch disables hardware SPI, pauses graphics and holds one signal at a
time LOW then HIGH, allowing you to measure the entire path through the level
shifter and wiring. Each state stays fixed until you send **`n`**; there is no
measurement timeout.

Keep the meter's black probe on **display GND** and measure the named signal
at the **display header**, rather than only at the Mega or shifter:

| Order | Signal | Mega pin | LOW at TFT | HIGH at TFT |
| --- | --- | --- | --- | --- |
| 1 | CS | D22 | About 0 V | About 3.3 V |
| 2 | DC | D23 | About 0 V | About 3.3 V |
| 3 | RESET | D24 | About 0 V | About 3.3 V |
| 4 | SDI/MOSI | D51 | About 0 V | About 3.3 V |
| 5 | SCK | D52 | About 0 V | About 3.3 V |

`p` starts at CS LOW; the first `n` selects CS HIGH, the next selects DC LOW,
and so on. After SCK HIGH, one more `n` finishes with devices deselected and
clock/data LOW. `p` can restart the sequence at any time. **Press the Mega's
RESET button to return to the graphics test**; `r`/`i` deliberately perform no
SPI operations while measuring.

CAN CS D10 and touch CS D25 stay HIGH. The MCP2515 can remain powered and
connected. TFT CS stays HIGH while DC, RESET, MOSI or SCK are being tested;
SCK stays LOW while TFT CS is tested. MISO D50 remains an input and is not
driven by this test. CHG_ALLOW and the status outputs remain LOW.

If a LOW/HIGH pair is wrong at the TFT, measure the same signal at the Mega
(expected LOW about 0 V, HIGH near its 5 V rail), then the shifter's input and
output. A correct Mega level but incorrect TFT level narrows the problem to
the shifter/wiring/loading. Check that only the intended line changes when
advancing each pair; swapped signals can otherwise appear healthy individually.
Do not bypass the level shifter with a direct 5 V display-input connection.

Passing this test establishes static levels and signal routing, not the shape
or timing of SPI edges, the MISO return path or the display controller type.
Report the LOW/HIGH readings for each row to choose the next test.

## Testing higher SPI clocks

Change `TFT_SPI_HZ` at the top of the sketch, rebuild and flash for each step.
On a 16 MHz Mega, useful steps are `500000UL`, `1000000UL`, `2000000UL`,
`4000000UL` and `8000000UL`. Hardware SPI tops out at 8 MHz; requests between
supported rates round down, so requesting 6 MHz still runs at 4 MHz. Serial
and on-screen labels show the requested value.

Check several complete cycles for stray colored pixels, missing or shifted
rows/columns, uneven checks/stripes and damaged text. Single-pixel checks can
look grey from a distance; inspect them close up. Start at a known-good speed
and compare the same patterns as you increase it. Visible corruption is a
reason to step back; a clean standalone run still needs confirmation in the
full charger firmware with CAN active.

For a slower graphics experiment use `125000UL`; a full-screen fill then takes
about 10 seconds plus overhead. SPI speed does not affect the held-level
multimeter test. The default remains 500 kHz; changing this sketch does not
change the production firmware's display clock in `src/config/BoardConfig.h`.

To return to the charger firmware, from the repository root:

```sh
pio run -e mega2560_wheel_alerts -t upload --upload-port COM15
```

The normal firmware loads the previously saved EEPROM profile again, including
its saved enable preference and protection state.
