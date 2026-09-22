# Huawei R4850G2 modular CAN controller

Arduino Mega 2560 firmware for one or more Huawei R48xx PSUs on the existing
MCP2515 CAN interface. The screen, rotary encoder, and interactive serial console
are independently optional. There is no required charger UI or ArduinoMenu dependency.

Start with `src/config/BoardConfig.h` (wiring and CAN crystal),
`src/config/ChargerConfig.h` (limits and defaults), and
`src/config/BuildOptions.h` (optional features and maximum PSU slots).

## Build

```sh
pio run -e mega2560
pio run -e mega2560_serial
pio device monitor -b 115200
```

| Environment | Screen | Wheel | Serial console |
| --- | --- | --- | --- |
| `mega2560` (default) | Yes | Yes | Yes |
| `mega2560_serial` | No | No | Yes |
| `mega2560_display` | Yes | No | Yes |
| `mega2560_local` | Yes | Yes | No |
| `mega2560_minimal` | No | No | No |

For upload, use `pio run -e mega2560_serial -t upload --upload-port <port>`.
No hardware has been flashed as part of this refactor.

Dependencies are pinned in `platformio.ini`: Atmel AVR platform 5.3.0,
CAN 0.3.1, SSD1306Ascii 1.3.5, ClickEncoder d6d5738fdf, and TimerOne 1.2.0.
The standard Arduino EEPROM library is supplied by the AVR framework.
Screenless builds need only CAN, SPI, and EEPROM. The verified target is Mega;
the original Nano firmware was already at 99.7% flash before this refactor.

For Arduino IDE, open the root `r4850g2_arduino.ino`, select Mega 2560, install the
libraries enabled by `BuildOptions.h`, and retain the entire `src/` directory.
The sketch marker is intentionally empty; `src/main.cpp` provides `setup`/`loop`.
PlatformIO is the build path used for validation.

## Wiring

Defaults retain MCP2515 CS pin 10, INT pin 2, an **8 MHz crystal**, and 125 kbit/s.
Check the crystal on your actual module and edit `BoardConfig.h` if necessary.
Use the required CAN termination. Mega hardware SPI is MISO 50, MOSI 51, SCK 52
(or the ICSP header). Encoder CLK/DT/SW remain 3/4/5. The optional SSD1306 uses
address `0x3c` and Mega I2C SDA 20 / SCL 21.

The existing hardware PCB and photographs below are for Nano. A Mega requires
appropriate external wiring; it does not fit that PCB's Nano footprint.

## Quick serial session

Use 115200 baud with CR, LF, or CRLF line endings. After initialization, expect
`Startup complete; Enter submits, help lists commands` followed by `> `.
Type `help`; commands are lowercase. Firmware echoes characters by default;
leave terminal local echo disabled, or use `echo off` if your terminal handles it.
Pressing Enter on an empty line prints a fresh prompt. A terminal's
`send_on_enter` filter buffers typing locally, so firmware echo then appears
only after Enter.
Slots are 1-based and do not have to equal their configured CAN addresses.
Start with the actual address mapping of your units.

```text
count 3
config all
set all voltage 54.0
set 1-3 current 5.0
set 2 current 4.0
save
apply all
status all
```

This stages a common voltage, gives each PSU its own current limit, saves the
controller configuration, and queues the CAN writes. It represents 5 A, 4 A,
and 5 A limits across three units, not a 5 A total bank limit. Confirm the
per-unit `status`: `QUEUED` is not a PSU acknowledgement.
Repeat `status all` after the job completes, or use `watch on` to follow it.
Configuration and EEPROM changes return a busy error until the apply finishes.

For independent loads, use single slots or ranges with separate voltage/current
settings. `set 2 address 7` maps slot 2 to the PSU responding at address 7; it
does not change the physical PSU's address. `all` covers configured slots, and
hardware operations skip disabled slots. `enabled 0` stops controller writes and
polling for that slot; it does not switch the PSU's output off.

| Command | Effect |
| --- | --- |
| `help` | Interactive command reference |
| `config [target]` | Staged settings; target defaults to all |
| `status [target]` | Telemetry, approximate session Ah, link state, command outcome, error counters |
| `set <target> voltage <V>` | Stage online voltage, 41.5..58.5 V |
| `set <target> current <A>` | Stage online current, 0..60 A within 120% of rated current |
| `set <target> offline-voltage <V>` | Stage offline voltage, 48.0..58.5 V |
| `set <target> offline-current <A>` | Stage offline current, same current bounds |
| `set <target> rated-current <A>` | Per-PSU current scaling, 50 A initially |
| `set <target> address <1..127>` | Local address mapping; configured addresses must be unique |
| `set <target> enabled <0 or 1>` | Include/exclude slot from polling and writes |
| `count <1..8>` | Number of configured slots, initially one |
| `interval <1000..10000>` | Milliseconds between each PSU's automatic polls |
| `bootapply <0 or 1>` | Stage whether saved online settings are applied once on reboot |
| `apply <target>` | Queue online voltage/current |
| `offline <target>` | Write staged offline defaults into the PSU's own memory |
| `persist <target>` | Queue online settings, then offline defaults |
| `save` / `load` | Save/load controller EEPROM only |
| `defaults` | Restore compiled defaults in RAM only; `save` makes this persistent |
| `poll <target>` | Request fresh telemetry |
| `describe <slot>` | Request and stream that PSU's ASCII description |
| `echo on` / `echo off` | Enable/disable firmware character echo (default on) |
| `watch on` / `watch off` | Repeated status reports |
| `raw on` / `raw off` | Incoming CAN frame trace; also useful for finding addresses |
| `reset-ah <target>` | Reset approximate session Ah counters |

Targets are a slot (`2`), inclusive range (`2-4`), or `all`.
Serial editing supports backspace; overlong or malformed lines are rejected
without executing a truncated command. The console drains up to 32 input bytes
per loop; polling continues while input is incomplete. Use `watch off` and
`raw off` for a quiet interactive session.

For a controller-only check, upload `mega2560_serial`, open the monitor, and
try `help` and `config all`. These work without a PSU or battery attached.
`CAN ready` reports MCP2515 initialization, not the presence of a responding PSU.
CAN transmission waits at most the configured 20 ms before requesting an abort;
a missing display is skipped, with a 25 ms I2C timeout in display-enabled builds.
This keeps missing peripherals from indefinitely blocking serial input. Actual
CAN traffic still needs a powered, correctly wired and terminated bus.

## Reboot and persistence

There are two different memories:

- **Controller EEPROM:** `save` stores configuration within the first 512 bytes,
  using a versioned, checksummed two-slot journal. `load` changes RAM only. The
  journal fits eight independent PSU configs and leaves the rest of Mega EEPROM
  untouched. Save is explicit, not performed on every adjustment.
- **PSU offline defaults:** `offline` or `persist` writes those defaults via CAN.
  These determine the PSU's behaviour at startup or after CAN communication loss.
  They are separate from controller EEPROM and separate from online setpoints.

At boot, valid EEPROM settings are restored to RAM; blank/corrupt/incompatible
EEPROM uses compiled defaults. Boot does not send setpoint writes by default,
matching the original sketch. Use `bootapply 1` followed by `save` to apply saved
online settings once at startup. Verify unit address mapping before enabling
this for independently configured loads: PSU software addresses can change as
units negotiate the bus. There is no automatic replay on PSU reconnection.

For example, to choose offline defaults explicitly:

```text
set all offline-voltage 54.0
set all offline-current 5.0
save
persist all
status all
```

## Wheel and display

Rotate in the status screen to select a PSU. Click to open its settings menu;
rotate to select a field/action; click to edit a field, rotate in 0.1 V/A steps,
and click again to finish editing. The menu contains PSU selection, online V/A,
offline V/A, Apply online, Persist PSU, Save EEPROM, and Back. After ten seconds
without input it returns to status; staged edits remain in RAM. CAN polling
continues throughout. A display without a wheel monitors the first slot; use
serial for full configuration. Both UIs operate on the same staged settings.

## API, parity, and validation

[Architecture and APIs](docs/ARCHITECTURE.md) documents `modifySingle`,
`modifyRange`, asynchronous apply results, addressing, extension points, and the
EEPROM format. [Reference parity](docs/PARITY.md) records the exact upstream
revision and changes, including current scaling, Ah, descriptions, and ACK values.

Run the host tests and all supported feature profiles:

```sh
python3 tools/test_host.py
pio run -e mega2560 -e mega2560_serial -e mega2560_display -e mega2560_local -e mega2560_minimal
```

The host suite exercises real production protocol/controller/storage/parser code
with simulated CAN/EEPROM/serial and AddressSanitizer/UndefinedBehaviorSanitizer.
It checks independent/range controls, telemetry routing, ACK rejection/timeouts,
malformed serial input, and interruption at every EEPROM journal write boundary.
See [validation and bench checks](docs/VALIDATION.md). Compilation and host tests
do not validate electrical wiring, physical CAN delivery, PSU calibration,
address negotiation, display/encoder timing, or physical EEPROM power loss.

Based on [haklein's Arduino firmware](https://github.com/haklein/r4850g2_arduino)
and [Craig Peacock's CAN utility](https://github.com/craigpeacock/Huawei_R4850G2_CAN).
Derived protocol software is GPL-3.0-or-later; see [LICENSE](LICENSE) and the
attribution in `docs/PARITY.md`.

## Original Nano hardware photographs

![image](https://github.com/user-attachments/assets/6b1efe15-7531-4c83-ac09-217468b4d0bf)

![image](https://github.com/user-attachments/assets/ebad8baa-e086-44e9-afd9-ef9afe57da40)

![image](https://github.com/haklein/r4850g2_arduino/assets/4569994/4e9a6961-6cf1-44dc-b249-fee5d6895d06)

![image](https://github.com/haklein/r4850g2_arduino/assets/4569994/0b62c0eb-7f6b-4b83-9882-98f3fad1fb27)

## Historical Nano pinout

  @Roturbo has provided a nice pinout for a nano:

  ![316427349-3ad49603-3def-4ec2-9e40-f8289db90cfa](https://github.com/haklein/r4850g2_arduino/assets/4569994/0a200d5f-f5de-4887-b59d-5bd5942bd7a0)
