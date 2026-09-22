# ILI9341 display and wheel

The local interface targets a **3.2-inch, 320x240 ILI9341 SPI module**, landscape.
It shows three groups per overview page and two PSUs per detail page. The
[screen preview](ui-preview.html) is generated from the implemented layout's
host tests; download/open that HTML file in a browser. Its readings are simulated.

SSD1306 support is removed. Serial, display and encoder remain build options;
the encoder only controls settings when an enabled display initializes.

## Wiring and first boot

Use a module with **5 V compatible logic inputs**, or appropriate level shifting.
A board accepting 5 V power is not necessarily compatible with 5 V SPI logic.
The [Adafruit breakout documentation](https://learn.adafruit.com/adafruit-2-8-and-3-2-color-tft-touchscreen-breakout-v2/pinouts)
describes a suitable interface. Check your exact module's pinout and backlight
circuit. This firmware uses SPI; an 8/16-bit-only parallel shield is not supported.

| Signal | Mega pin / connection |
| --- | --- |
| TFT SCK / CLK | 52, shared with MCP2515 SCK |
| TFT SDI / MOSI | 51, shared with MCP2515 MOSI |
| TFT SDO / MISO | 50, shared with MCP2515 MISO; needed for display ID check |
| TFT CS | **22**, separate from CAN CS **10** |
| TFT DC / RS | 23 |
| TFT RST | 24 |
| TFT GND | Common ground with Mega and CAN interface |
| TFT supply / backlight power | According to the module's documentation |
| Optional backlight PWM logic input | Disabled by default; configure e.g. pin 6 |
| Encoder CLK / DT / SW | 3 / 4 / 5, switches to ground; internal pullups |
| MCP2515 CS / INT | 10 / 2, unchanged |

SPI is also available on the Mega ICSP header. Deselect any unused SD-card or
touch controller sharing the bus, using that module's documented CS pin/pullup.
No touch or SD-card functionality is used. A bare backlight LED needs a suitable
driver; do not connect its load directly to a Mega GPIO. Many shields hardwire
TFT CS to pin 10, which conflicts with the existing CAN wiring.

Board wiring, SPI clock, landscape rotation (1 or 3), display ID verification
and optional backlight pin are in `src/config/BoardConfig.h`. UI timings and
step sizes are in `src/config/UiConfig.h`. Setpoint bounds remain in the backend.

```sh
pio run -e mega2560
pio run -e mega2560 -t upload --upload-port COM15
pio device monitor -b 115200 --port COM15
```

Replace the port for your machine. Startup reports `ILI9341 ready`, or
`ILI9341 unavailable; local controls disabled`. Missing display hardware does
not prevent CAN or serial operation. The ID check expects ILI9341 ID `0x9341`;
connect SDO/MISO. Disable `displayVerifyId` only after verifying a write-only
module and its wiring: that removes automatic detection of an absent display.
Display detection happens at boot; reset after connecting/replacing the display.

Commission the installation using the [serial getting-started guide](GETTING_STARTED.md):
discover devices, bind stable identities/ratings, and create disjoint groups.
The UI deliberately does not adopt replacement identities or edit membership.
An empty installation shows `No groups configured` with a serial setup prompt.

For initial local operation, enter a group's configuration and apply **bus
voltage first**, with every configured PSU ready. Then apply that group's total
current. Alternatively, complete the initial `apply all` using serial. Applying
bus voltage alone never activates staged currents from other groups.

## Controls

| Screen | Turn | Short press | Hold (about 1.2 seconds) |
| --- | --- | --- | --- |
| Groups | Select group, paging after three | Open its chargers | Group configuration |
| Chargers | Select PSU, paging after two | Details / error explanation | Return to groups |
| Details | No action | Return to charger list | Return to groups |
| Configuration | Select bus voltage or group total current | Begin editing | Return to groups |
| Editing | Adjust the flashing value | Apply and save | Discard uncommitted edit; return to groups |
| Missing-member warning | Select No / Yes | Confirm choice | Cancel and return to groups |
| Operation/result | No action | Return to configuration when finished | Return to groups; submitted work continues |

The original group selection is retained when returning. Holding does not also
emit a short-click action. Edit steps default to 0.1 V and 0.5 A; wheel adjustment
stops at backend limits, including the exact group capacity for its commissioned
member ratings and equal-share rounding. Values are revalidated before applying.

Serial remains available throughout navigation. A configuration change during
an edit invalidates it, rather than applying an obsolete value. A confirmation
also expires after 15 seconds or a topology/configuration change.

## Reading the screens

The top bar is **measured V, measured total A, measured total output W** across
all configured slots, not just the visible group/page. Voltage is the mean of
fresh PSU output-voltage readings, not a separate bus sensor or the setpoint.
Current and output power are sums of their respective telemetry registers;
power is not reconstructed from V x A. Unconfigured/unbound responders are not
silently added to these totals; use `diag bus` when commissioning.

- `*` means the aggregate is incomplete; unavailable members are not treated as
  measured zero. `--` means there are no fresh usable readings for that field.
  Each field expires independently even if other telemetry continues arriving.
- Overview current is live / authorized requested total. Charger current is
  live / that unit's authorized request. `PENDING` means staged settings differ,
  authorization is absent, or runtime voltage/current ACKs are missing. An old
  EEPROM profile is not evidence of an apply after this boot.
- Group temperature is the maximum fresh output temperature. Charger
  temperatures are **input/output**, e.g. `40C/55C`; the same row shows Ah and W.
  The next row contains input V, Hz and A in fixed columns.
- Green `OK` means ready with fresh core output measurements and no known command
  failure. `ERR 1/2` means one of two members has a known problem, not that the
  entire group has stopped. Short codes identify missing/stale/unready devices
  and rejected/timed-out writes. Full hardware alarm decoding is not implemented.
  Details show the explanation, last known CAN address, response age and raw
  alarm value when available.
- Long text is abbreviated with `~`. Numbers retain their units and digits:
  precision is reduced or k/M units are used as needed; unrepresentable values
  show `OVF` rather than a misleading truncated number.

Session Ah is **approximate, volatile and calculated on the Mega** from each
verified bound PSU's current broadcasts, using the existing reference conversion
and 377 ms sampling assumption. Groups sum those per-slot counters. Counters
survive a known device's absence, rediscovery and address changes in RAM; no
missing samples are invented. Reboot, `reset-ah`, rebinding to a different
identity, removing the slot or changing deployment resets the relevant counters.
Moving an identity to a different controller slot starts that slot's count again.
No Ah history is written to EEPROM. The approximation is not a battery fuel gauge.

## Apply, failure and persistence

The editor explicitly labels **BUS VOLTAGE - ALL GROUPS**. A voltage commit sends
the shared voltage to every configured PSU; it has no missing-member override.
A current commit stages the group's total and applies its original equal shares.

The UI shows `APPLYING` while the backend verifies identities and waits for ACKs.
After every intended recipient accepts, it snapshots configuration and shows
`SAVING EEPROM`, then `APPLIED AND SAVED`. The save uses the existing 512-byte
journal/schema and progresses one byte per service step. Serial and CAN can
continue between writes. It saves the whole configuration, including other
staged drafts; that does not apply those drafts. Serial `apply` still requires a
separate serial `save`.

An apply failure shows **not saved** and retains member results for inspection.
Some earlier writes may already have succeeded; there is no automatic rollback.
A save failure instead says **APPLIED / EEPROM FAILED**, since the live writes
were accepted. If serial changes RAM during an incremental save, the result
reports that the earlier snapshot was saved and current edits remain unsaved.
The completion/result screen appears even if you navigated away from progress.

A missing group member blocks the ordinary apply. The UI can offer an explicit
**No / Yes, apply partial** warning, defaulting to No, which lists missing slots
and explains that their outputs may still be on. Confirmation sends only the
responders' original shares. It neither changes membership nor redistributes
current. Other faults remain blocking. A partial success is labelled
`PARTIAL APPLY SAVED`, never complete group success. Cancelling after that
warning retains the staged draft in RAM without issuing the partial writes.

Reboot restoration remains the existing separate policy: `autoresume on` and
`save` opt in after commissioning. Local apply/save does not enable auto-resume
or alter the PSU's own nonvolatile offline defaults.

## Other builds and future displays

`mega2560_display` omits the wheel and cycles overview pages every five seconds.
`mega2560_local` retains editing but omits serial; commission identities/groups
with a serial build first. Headless profiles have no display dependencies.

Backlight dimming is optional. Configure a suitable PWM control pin to enable
the default 60-second idle timeout. The first wheel interaction wakes the screen
and is consumed. Dimming is suppressed during local apply/save operations.

`UiModel` owns input/navigation/operation state, `UiView` owns layout, and
`Display` is the replaceable hardware interface. `Ili9341Display` draws changed
cells through short SPI transactions (24 pixels at most; four transactions per
tick). It uses character/style caches and one glyph raster, not a full pixel
framebuffer. Initialization and its full clear happen before CAN reception starts.

Regenerate the preview from real layout tests after installing the display libraries:

```sh
mkdir -p /tmp/r4850-ui
PSU_UI_SNAPSHOTS=/tmp/r4850-ui python3 tools/test_host.py
python3 tools/render_ui.py /tmp/r4850-ui docs/ui-preview.html
```

See [validation](VALIDATION.md) for build sizes and the remaining physical bench
checks. Host tests and builds do not establish the chosen module's electrical
compatibility, worst-case interrupt timing, or PSU behavior on a loaded CAN bus.
