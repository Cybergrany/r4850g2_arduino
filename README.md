# Huawei R4850G2 CAN controller

Arduino Mega 2560 firmware for up to **nine identity-bound PSUs**, with global,
group and individual voltage/current targets and **eight named configuration
profiles**. Global current means the same total in each group; group current is
split across its configured members. Individual targets can then override those
shares. Settings are staged, previewed and explicitly applied.

See [scoped settings, profiles and optional participation](docs/PROFILES.md) for
commands, the five-option configuration UI, migration and a bench sequence.
For physically paralleled outputs, use matching voltages. Separate-output targets
share the controller's enable and thermal-protection domain.

**Start here: [serial commissioning and group operation](docs/GETTING_STARTED.md).**
It covers checking a powered PSU without a battery, discovering identities,
creating groups, checking responses and broadcasts, applying settings, saving,
and handling missing or replaced units.

For intermittent AC and battery-connected PSUs, see [startup, AC cutoff and
remembered group absences](docs/STARTUP.md). Serial and local UI can accept
unavailable groups and include returning identities without redistributing current.

The MCP2515 interface is unchanged. Screen, wheel, and serial support are
optional. The local UI targets a **320x240 landscape ILI9341 SPI display (2.8 or 3.2 inches)** with
three group columns, two chargers per detail page, and optional wheel/touch voltage/current
editing. [Touch, calibration and CAN-free UI debug builds](docs/TOUCH.md) are available. SSD1306 support has been removed. A display is required for local edits;
serial-only operation remains supported.

See [local UI, wiring and controls](docs/LOCAL_UI.md) and the
[generated screen preview](docs/ui-preview.html) (open the HTML file in a browser).

## Build and wiring

```sh
pio run -e mega2560_serial
pio run -e mega2560_serial -t upload --upload-port COM15
pio device monitor -b 115200 --port COM15
```

Replace the port as appropriate. Use 115200, 8-N-1, no flow control, terminal
local echo off. Press **Ctrl-X** on reconnect, then `hello` or `help`.
See [serial connection behaviour](docs/SERIAL.md).

| Environment | Screen | Wheel | Serial |
| --- | --- | --- | --- |
| `mega2560` (default) | ILI9341 dashboard/config | Navigate/edit | Yes |
| `mega2560_wheel_alerts` | 8 MHz display, touch disabled; D26 enable, D27–D30 outputs | Navigate/edit | Yes |
| `mega2560_serial` | No | No | Yes |
| `mega2560_display` | Group overview, automatic paging | No | Yes |
| `mega2560_local` | ILI9341 dashboard/config | Navigate/edit | No |
| `mega2560_minimal` | No | No | No |
| `mega2560_touch` | Touch dashboard/editor | No | Yes |
| `mega2560_touch_wheel` | Touch dashboard/editor | Navigate/edit | Yes |
| `mega2560_ui_debug` | SIM dashboard/editor; no CAN | Navigate/edit | Yes |
| `mega2560_touch_debug` | SIM touch/editor; no CAN | No | Yes |
| `mega2560_touch_local` | Touch dashboard/editor | No | No |
| `mega2560_touch_debug_local` | SIM touch/editor; no CAN | No | No |

Optional four-wire external fan: use `mega2560_serial_fan` for serial-only
control with 25 kHz PWM on D6. Normal OFF holds zero current while keeping PSUs
awake for cooling. See [wiring, zero-hold confirmation and fault fallback](docs/CONTROL.md).

`mega2560_serial_cutoff` adds active-HIGH CHG_ALLOW on D27 with no screen,
wheel or switch. `mega2560_touch_cutoff` combines touch, serial, ENABLE_IN D26,
fan PWM D6 and CHG_ALLOW D27. Hardware must hold the permit LOW during reset;
the pin is disabled in other production builds by default.

`mega2560_serial_alerts` adds WARNING D28, ERROR D29 and **PSU_ACTIVE D30** to the
serial-only build. `mega2560_touch_alerts` combines these with the touch/cutoff
example. `PSU_ACTIVE_PIN` defaults to `-1` elsewhere; HIGH means enable is ON and
at least one PSU is permitted a positive current limit. Inspect it using
`diag control`. See [status output behavior and wiring](docs/CONTROL.md#optional-psu_active-output).

For the current display/wheel wiring, use `mega2560_wheel_alerts`: screen at
**8 MHz SPI**, touch disabled, serial and wheel enabled, plus CHG_ALLOW D27,
WARNING D28, ERROR D29, PSU_ACTIVE D30 and the ENABLE_IN switch on D26:

```sh
pio run -e mega2560_wheel_alerts
pio run -e mega2560_wheel_alerts -t upload --upload-port COM15
```

The ordinary `mega2560` profile also disables touch and uses 8 MHz, with optional
outputs disabled. Touch remains an explicit choice in the `*_touch*` profiles.
When leaving the panel's touch controller unused, hold T_CS HIGH on its 3.3 V
side so it cannot interfere with shared SPI. See [display wiring](docs/LOCAL_UI.md).

For a blank screen, the [standalone ILI9341 bench sketch](examples/ili9341_bench/README.md)
uses the same pins and 8 MHz clock, cycles test patterns and reports display ID
over serial without loading the charger application or changing EEPROM.

For an unresponsive wheel, the [standalone wheel serial test](examples/wheel_bench/README.md)
reports raw D3/D4/D5 levels, rotation and button events without the display,
CAN, ClickEncoder or TimerOne. It uses the same wheel pins as `mega2560_wheel_alerts`.

Commission identities/groups using a serial-enabled build before using
`mega2560_local`; voltage/current editing is then available on the display. Live enable/zero-current hold and saved restart policy are described in [CONTROL.md](docs/CONTROL.md). Dependencies
are pinned in `platformio.ini`; EEPROM uses the standard Arduino library.
The supported target is Mega 2560, using its **4 KiB EEPROM** for controller
configuration and eight profiles. Production builds support **nine PSU slots and
eight groups**; AVR UI-debug builds simulate four units to leave RAM for profiles. The original Nano was already at 99.7% flash.

Defaults: MCP2515 CS 10, INT 2, **8 MHz crystal**, 125 kbit/s; Mega SPI MISO 50,
MOSI 51, SCK 52 (or ICSP). Use correctly terminated CAN wiring. Encoder CLK/DT/SW
are 3/4/5. ILI9341 SCK/MOSI/MISO share 52/51/50, with **CS 22, DC 23, RST 24**.
Use a module with suitable level shifting for Mega logic. Backlight PWM is
optional and disabled by default. See the wiring guide before connecting a
module; the historical Nano PCB pictured below does not accept a Mega directly.

For Arduino IDE, retain the complete `src/` tree, open the root `.ino`, select
Mega 2560, and install enabled libraries from `platformio.ini`. The sketch marker
is empty; `src/main.cpp` supplies `setup`/`loop`. Validation uses PlatformIO.
The [patched ClickEncoder driver](src/vendor/ClickEncoder/README.md) is bundled
under `src/`; it is built automatically and needs no separate installation.

## Configuration files

| File | Purpose |
| --- | --- |
| [`Deployment.h`](src/config/Deployment.h) | Installation ID, starting voltage, slot count, optional group/identity/rating template |
| [`BoardConfig.h`](src/config/BoardConfig.h) | CAN crystal, bitrate, wiring, serial, peripheral timeouts |
| [`ControlConfig.h`](src/config/ControlConfig.h) | Enable input, AC cutoff, PSU_ACTIVE pin, fan policy and reconciliation timing |
| [`ChargerConfig.h`](src/config/ChargerConfig.h) | Configuration types, limits, discovery/apply timing |
| [`TouchConfig.h`](src/config/TouchConfig.h) | XPT2046 wiring, calibration profile, sampling and gesture timings |
| [`ProfileConfig.h`](src/config/ProfileConfig.h) | Eight profile slots, 12-character names and EEPROM locations |
| [`BuildOptions.h`](src/config/BuildOptions.h) | Optional peripherals and maximum slots (1–9) |
| [`ConsoleConfig.h`](src/config/ConsoleConfig.h) | Serial input budget and idle expiry |
| [`UiConfig.h`](src/config/UiConfig.h) | UI timing, edit increments, paging, dimming and drawing budget |

Change the deployment ID when moving to another physical installation. EEPROM
for another ID stays inspectable but cannot authorize writes. `defaults` selects
the compiled template in RAM; commission the new installation before `save`.

## Operating model

```text
set voltage 54
set current GROUP1 55
preview all
apply all
```

With two already commissioned members, GROUP1 receives 27.50 A per PSU. `apply all`
sends the common voltage to participating slots, waits for acknowledgements,
then sends the current shares. **Wait for the final result**, not just `QUEUED`.
Subsequent `apply GROUP1` applies both that group's voltage and current targets.
Use `apply GROUP1 current` to apply only current. While OFF, apply prepares the
next explicit ON instead of sending positive current.

Each slot is bound to a reported identity. Current CAN addresses are learned at
runtime. An unknown replacement is never adopted merely because it occupies an
old address. Every setting write also requires a fresh identity reply. Protocol
observations and remaining physical limitations are documented in
[reference parity](docs/PARITY.md).

A new, unaccepted missing member blocks its group's apply. Use
`startup accept GROUP1 [remember]` and review/confirm to accept absence for
startup and future operations; returned units still require inclusion.
The older one-operation `apply GROUP1 partial` explains the
consequences and requires `confirm yes`; available members retain their original
shares. Global voltage changes cannot use this override. Communication loss
does not prove a PSU is electrically off, and the controller does not switch
outputs off when groups are deleted or members disappear.

Controller EEPROM stores drafts **separately** from the authorized operating
profile. `save` alone cannot activate drafts. A known returning unit can restore
its authorized settings once required participants are identified and ready;
previously excluded identities require explicit `startup include` approval.
`autoresume on/off` controls live enable/zero-current hold; `save` persists the preference.
Boot and returning units reconcile through zero current, checked fan mode, per-PSU authorized
voltage and verified wake before restoring authorized current. Optional
ENABLE_IN is a latching switch with latest-event-wins semantics. See
[control policy and wiring](docs/CONTROL.md). PSU nonvolatile defaults are a separate, explicit `offline all` operation.

Existing parallel-controller records (schemas 2–5) load with their bindings,
groups, drafts and authorized profile intact; schema-4 thermal stops are retained.
The next verified save writes schema 6 and retires legacy commit markers. Original per-unit schema-1 records remain
inspectable with `legacy`: compatible uniform settings migrate to an unbound
GROUP1 with auto-resume disabled; conflicting per-unit settings require review. See the migration section
of the [commissioning guide](docs/GETTING_STARTED.md#upgrading-and-moving-installations).

## Development and validation

[Architecture and public APIs](docs/ARCHITECTURE.md) describe the structured
preview, group diagnostics, discovery, failure reports, transport boundary, and
EEPROM format and the replaceable display boundary. The serial command reference is in
the [commissioning guide](docs/GETTING_STARTED.md#serial-command-reference).

```sh
python3 tools/test_host.py
python3 tools/test_route_budget.py
pio run -e mega2560 -e mega2560_serial -e mega2560_display -e mega2560_local -e mega2560_minimal
```

UI selection uses high-contrast outlines across cards, menus and buttons. See
[the UI preview](docs/ui-preview.html) and [redraw measurements](docs/VALIDATION.md#outline-selection-throughout-the-ui-2026-11-11).

Host tests run the production code with simulated CAN, serial, and EEPROM under
AddressSanitizer/UndefinedBehaviorSanitizer. See [validation](docs/VALIDATION.md)
for coverage, build sizes, the optional `tools/test_avr_render.py` complete-redraw
AVR timing regression, and remaining physical bench checks. No physical
multi-PSU validation or upload is implied by these tests.

Based on [haklein's Arduino firmware](https://github.com/haklein/r4850g2_arduino)
and [Craig Peacock's CAN utility](https://github.com/craigpeacock/Huawei_R4850G2_CAN).
Derived protocol software is GPL-3.0-or-later; see [LICENSE](LICENSE) and
[attribution](docs/PARITY.md).

## Original Nano hardware photographs

![image](https://github.com/user-attachments/assets/6b1efe15-7531-4c83-ac09-217468b4d0bf)

![image](https://github.com/user-attachments/assets/ebad8baa-e086-44e9-afd9-ef9afe57da40)

![image](https://github.com/haklein/r4850g2_arduino/assets/4569994/4e9a6961-6cf1-44dc-b249-fee5d6895d06)

![image](https://github.com/haklein/r4850g2_arduino/assets/4569994/0b62c0eb-7f6b-4b83-9882-98f3fad1fb27)

## Historical Nano pinout

  @Roturbo has provided a nice pinout for a nano:

  ![316427349-3ad49603-3def-4ec2-9e40-f8289db90cfa](https://github.com/haklein/r4850g2_arduino/assets/4569994/0a200d5f-f5de-4887-b59d-5bd5942bd7a0)

Operator protection: [alerts, thermal limits, GPIOs and bench procedure](docs/ALERTS.md).
The new serial/touch alert example profiles use D28 WARNING and D29 ERROR; pins
are otherwise disabled by default. Schema 6 keeps calibration at its original addresses; see the full EEPROM map in [PROFILES.md](docs/PROFILES.md).
