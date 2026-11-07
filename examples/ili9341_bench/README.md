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

Each pattern stays for four seconds **after** drawing finishes. A full-screen
fill at 500 kHz takes about 2.5 seconds plus software overhead; allow roughly a
minute to observe the sequence. The built-in LED blinks between drawing steps;
full-screen transfers intentionally block this minimal test, so pauses in the
LED and serial command handling during drawing are expected.

Single-character serial commands work without Enter; CR/LF are ignored:

- `r`: restart the pattern sequence, including the ID report.
- `i`: read and print the display ID again.
- `n`: advance to the next pattern immediately.

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

To return to the charger firmware, from the repository root:

```sh
pio run -e mega2560_wheel_alerts -t upload --upload-port COM15
```

The normal firmware loads the previously saved EEPROM profile again, including
its saved enable preference and protection state.
