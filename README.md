# Huawei R4850G2 parallel CAN controller

Arduino Mega 2560 firmware for **one parallel DC output bus**, with named groups
of PSUs powered by different input sources. A group current is a **total**, split
equally across its configured members. Voltage is global. Settings are staged,
previewed, and explicitly applied; there is no automatic current redistribution.

**Start here: [serial commissioning and group operation](docs/GETTING_STARTED.md).**
It covers checking a powered PSU without a battery, discovering identities,
creating groups, checking responses and broadcasts, applying settings, saving,
and handling missing or replaced units.

The MCP2515 interface is unchanged. Screen, wheel, and serial support are
independently optional. The existing screen now monitors PSU/group state; the
wheel selects a PSU. Configuration uses serial or the backend API. The old
per-PSU voltage menus and independent-pack controls have been removed.

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
| `mega2560` (default) | Monitor | Select PSU | Yes |
| `mega2560_serial` | No | No | Yes |
| `mega2560_display` | Monitor first slot | No | Yes |
| `mega2560_local` | Monitor | Select PSU | No |
| `mega2560_minimal` | No | No | No |

Commission and save using a serial-enabled build before using a build without
configuration controls. Reboot restoration is separately opt-in. Dependencies
are pinned in `platformio.ini`; EEPROM uses the standard Arduino library.
The supported target is Mega 2560, with a **512-byte EEPROM budget** even though
Mega provides more. The original Nano was already at 99.7% flash.

Defaults: MCP2515 CS 10, INT 2, **8 MHz crystal**, 125 kbit/s; Mega SPI MISO 50,
MOSI 51, SCK 52 (or ICSP). Use correctly terminated CAN wiring. Encoder CLK/DT/SW
are 3/4/5. Optional SSD1306: address `0x3c`, SDA 20, SCL 21. The historical Nano
PCB pictured below does not accept a Mega directly.

For Arduino IDE, retain the complete `src/` tree, open the root `.ino`, select
Mega 2560, and install enabled libraries from `platformio.ini`. The sketch marker
is empty; `src/main.cpp` supplies `setup`/`loop`. Validation uses PlatformIO.

## Configuration files

| File | Purpose |
| --- | --- |
| [`Deployment.h`](src/config/Deployment.h) | Installation ID, starting voltage, slot count, optional group/identity/rating template |
| [`BoardConfig.h`](src/config/BoardConfig.h) | CAN crystal, bitrate, wiring, serial, peripheral timeouts |
| [`ChargerConfig.h`](src/config/ChargerConfig.h) | Configuration types, limits, discovery/apply timing |
| [`BuildOptions.h`](src/config/BuildOptions.h) | Optional peripherals and maximum slots (1–8) |
| [`ConsoleConfig.h`](src/config/ConsoleConfig.h) | Serial input budget and idle expiry |

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
sends the common voltage to all configured slots, waits for acknowledgements,
then sends the current shares. **Wait for the final result**, not just `QUEUED`.
Subsequent `apply GROUP1` sends only that group's currents, using the previously
authorized common voltage; it does not activate a pending voltage edit.

Each slot is bound to a reported identity. Current CAN addresses are learned at
runtime. An unknown replacement is never adopted merely because it occupies an
old address. Every setting write also requires a fresh identity reply. Protocol
observations and remaining physical limitations are documented in
[reference parity](docs/PARITY.md).

A missing member blocks its group's apply. `apply GROUP1 partial` explains the
consequences and requires `confirm yes`; available members retain their original
shares. Global voltage changes cannot use this override. Communication loss
does not prove a PSU is electrically off, and the controller does not switch
outputs off when groups are deleted or members disappear.

Controller EEPROM stores drafts **separately** from the authorized operating
profile. `save` alone cannot activate drafts. A known returning unit can restore
its authorized settings once the full installation is identified and ready.
`autoresume on` followed by `save` explicitly enables this after a controller
reboot. PSU nonvolatile defaults are a separate, explicit `offline all` operation.

Old EEPROM records are preserved for inspection with `legacy`. Compatible
uniform settings migrate to an unbound GROUP1 in RAM with auto-resume disabled;
conflicting per-unit settings require manual review. See the migration section
of the [commissioning guide](docs/GETTING_STARTED.md#upgrading-and-moving-installations).

## Development and validation

[Architecture and public APIs](docs/ARCHITECTURE.md) describe the structured
preview, group diagnostics, discovery, failure reports, transport boundary, and
EEPROM format for future UI implementations. The serial command reference is in
the [commissioning guide](docs/GETTING_STARTED.md#serial-command-reference).

```sh
python3 tools/test_host.py
pio run -e mega2560 -e mega2560_serial -e mega2560_display -e mega2560_local -e mega2560_minimal
```

Host tests run the production code with simulated CAN, serial, and EEPROM under
AddressSanitizer/UndefinedBehaviorSanitizer. See [validation](docs/VALIDATION.md)
for coverage, build sizes, and remaining physical bench checks. No physical
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
