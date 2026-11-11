# ILI9341 display and wheel

The local interface targets a **320x240 ILI9341 SPI module (2.8 or 3.2 inches)**, landscape.
It shows three groups per overview page and two PSUs per detail page. The
[screen preview](ui-preview.html) is generated from the implemented layout's
host tests; download/open that HTML file in a browser. Its readings are simulated.

See [touch input, calibration and CAN-free debugging](TOUCH.md) for the optional
XPT2046 interface and fullscreen editor. The wheel controls below remain available.

SSD1306 support is removed. Serial, display and encoder remain build options;
the encoder only controls settings when an enabled display initializes.

## Wiring and first boot

The display now runs at **8 MHz SPI** (`board::displaySpiHz` in
`src/config/BoardConfig.h`). Use `mega2560_wheel_alerts` for the current screen,
wheel and serial setup with touch disabled, CHG_ALLOW D27, WARNING D28, ERROR D29
and PSU_ACTIVE D30, with ENABLE_IN on D26. Connect its latching switch to GND;
see [switch semantics](CONTROL.md#optional-enable_in-switch).
`mega2560` is also touch-disabled, without those optional
outputs. Explicit touch profiles remain available.

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
Touch is used only in touch-enabled builds; SD-card functionality is not used.
With touch disabled, firmware does not drive T_CS. Hold it HIGH at the touch
controller's 3.3 V logic level if the panel's touch SPI pins remain connected.
A bare backlight LED needs a suitable
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
The check reads command `0xD3`, byte indices 2/3. Adafruit ILI9341 1.6.3's
`ILI9341_RDID4` macro is `0xDD`, so it must not be used for this device-code read.
Reads temporarily use the lower of `displaySpiHz` and `displayReadMaxSpiHz`
(default cap 1 MHz), then restore the drawing speed even if the ID is wrong.
This lets drawing run faster without exceeding the stricter read timing.

A missing-members alert offers **Acknowledge** and **Config**. Acknowledge starts selected; Config opens the availability overview with the affected group selected. Release the wheel button, then short-press and
release to dismiss the notification. This does not accept missing PSUs or enable
charging; those decisions remain explicit confirmations in Config or Tools → Startup. A press already in progress
when an alert appears is discarded until the button has been released for 50 ms
and a new press begins.

The wheel uses a [bundled ClickEncoder fix](../src/vendor/ClickEncoder/README.md):
the old pinned driver's double-decrement bug prevented short clicks when
double-click recognition was disabled. The standalone GPIO test could pass
while that library bug left the main UI unable to acknowledge alerts.

Commission the installation using the [serial getting-started guide](GETTING_STARTED.md):
discover devices, bind stable identities/ratings, and create disjoint groups.
The UI deliberately does not adopt replacement identities or edit membership.
An empty installation shows `No groups configured` with a serial setup prompt.

For initial local operation, use the configuration menu to prepare voltage and
current for all required participants. OFF preparation keeps zero hold; the next
explicit ON still requires valid authorization, reconciliation and availability.
Field-only edits do not activate other drafts. See [scoped settings](PROFILES.md).

## Controls

| Screen | Turn | Short press | Hold (about 1.2 seconds) |
| --- | --- | --- | --- |
| Groups | Select group, paging after three | Open its chargers | Five-choice configuration menu |
| Chargers | Select PSU, paging after two | Details / error explanation | Return to groups |
| Details | No action | Return to charger list | Return to groups |
| Configuration menu / selectors | Select scope, group, PSU or profile | Open selection | Back |
| Configuration | Select voltage, current or Tools | Begin editing / open Tools | Return to groups |
| Tools / startup | Select an action | Open / review / confirm | Back |
| Editing | Adjust the flashing value | Apply and save; global edits open a review | Discard uncommitted edit |
| Profile name | Select character or Done/Cancel; adjust selected character | Toggle character editing / Done / Cancel | Discard name edits |
| Profile options / review | Select inclusion, name or review / Cancel or confirm | Activate selection | Back |
| Missing-member warning | Select No / Yes | Confirm choice | Cancel and return to groups |
| Operation/result | No action | Return to configuration when finished | Return to groups; submitted work continues |

The original group selection is retained when returning. Holding does not also
emit a short-click action. Edit steps default to 0.1 V and 0.5 A; wheel adjustment
stops at backend limits, including the exact group capacity for its commissioned
member ratings and equal-share rounding. Values are revalidated before applying.
When no groups exist yet, the menu still opens; the group selector explains
that group creation is a serial operation. Tools remains available from the
overview for startup diagnostics and commissioning power.

Serial remains available throughout navigation. A configuration change during
an edit invalidates it, rather than applying an obsolete value. A confirmation
also expires after 15 seconds or a topology/configuration change.

## Reading the screens

The top bar is **measured V, measured total A, measured total output W** across
all configured slots, not just the visible group/page. Voltage shows the common
fresh reading, or `V:MIX` when readings differ; individual details show each
unit's voltage. It is not a separate bus sensor or the setpoint.
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

The group card reads **Output:**, watts, then Ah. The former `SESSION~` label
was an approximation hint; removing it does not change the counter.

Session Ah is **approximate, volatile and calculated on the Mega** from each
verified bound PSU's current broadcasts, using the existing reference conversion
and 377 ms sampling assumption. Groups sum those per-slot counters. Counters
survive a known device's absence, rediscovery and address changes in RAM; no
missing samples are invented. Reboot, `reset-ah`, rebinding to a different
identity, removing the slot or changing deployment resets the relevant counters.
Moving an identity to a different controller slot starts that slot's count again.
No Ah history is written to EEPROM. The approximation is not a battery fuel gauge.

## Apply, failure and persistence

Long-press a group for global, group, individual, Load Config and Save Config
choices. The editor labels the scope; global current is **TOTAL PER GROUP**.
Mixed values and custom shares are explicit. A field commit changes only that
field. OFF prepares targets for the next ON. Global application and profile
save/load have Cancel-first reviews. See [the settings/profile guide](PROFILES.md).

The UI shows `APPLYING` while the backend verifies identities and waits for ACKs.
After every intended recipient accepts, it snapshots configuration and shows
`SAVING EEPROM`, then `APPLIED AND SAVED`. The save uses the schema-6 controller
journal in the Mega EEPROM and progresses one byte per service step. Serial and CAN can
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

`autoresume on/off` now controls live enable/zero-current hold and `save` persists that
preference. Local apply/save does not bypass a disabled enable request
or alter the PSU's own nonvolatile offline defaults.

## Other builds and future displays

`mega2560_display` omits wheel/touch and cycles overview pages every five seconds.
`mega2560_local` retains editing but omits serial; commission identities/groups
with a serial build first. Headless profiles have no display dependencies.

Backlight dimming is optional. Configure a suitable PWM control pin to enable
the default 60-second idle timeout. The first wheel interaction wakes the screen
and is consumed. Dimming is suppressed during local apply/save operations.

`UiModel` owns navigation and operation state, `UiView` owns layout, and
`Display` is the replaceable hardware interface. The classic font, colours,
text size and control locations are retained. Selection uses a bright cyan outline;
unselected cards and buttons use a dim outline, with dark interiors. Configuration
values show two decimal places; the fullscreen editor shows one, matching its
0.1 V / 0.5 A increments. The shared formatter generates fractional digits
directly because AVR-libc does not support variable-width printf conversions.

`Ili9341Display` draws selection edges separately from text. Moving a selection
recolours only the previous and new outlines, leaving readings and status colours
unchanged. This applies to groups, chargers, settings, menus, alerts, recovery
choices, touch buttons and calibration targets. No selection change accepts an
alert, changes configuration or applies settings.

The two-pixel edges use at most **48 pixels per transaction at 8 MHz**, 24 at
4 MHz and 12 below. Normal text strips retain their four/two/one-row limits.
The **750 us cooperative budget**, eight-transfer ceiling above 2 MHz and
two-transfer ceiling at lower clocks remain unchanged. Pixel preparation takes
place before SPI opens; every transaction closes before yielding so CAN can run.
One work unit or interrupt can overrun the budget; it is not a hard deadline.

Rectangles and text share the existing compact scene cache. Rapid selection
reversals and page changes complete or erase tracked edges, without leaving
fragments behind. Text takes precedence over borders with a small dark clearance
so glyphs and error colours remain legible. Touch targets on a new page arm only
after both its text and outlines finish. Input composes immediately; live data
refreshes every 500 ms through the normal background pipeline. Boot clearing
still happens before CAN starts.

User navigation/highlight changes receive consecutive drawing slices in an
additional **8 ms cooperative window**, with at most twelve extra slices per
main loop. Initial pages and notifications use the same path. The controller
processes up to four incoming CAN frames and temperature protection between
slices, including command acknowledgements. It does not deliberately drop CAN
traffic or send new settings from the renderer. The normal controller pass
retains command scheduling; GPIO OFF/cutoff requests are serviced during drawing.
The window can overrun by a receive/protection work unit or interrupt.

Routine metric updates keep the normal single-slice pipeline. A periodic redraw
finishes before another telemetry composition starts, avoiding an endless
restart when a slow pass exceeds the 500 ms refresh interval.

Use `diag ui` to inspect:

- `render-max-us` / `SPI-max-us`: maximum render-call / transaction duration.
- `scene-ms(last/max)`: latest and maximum completed-scene duration.
- `change-ms`: latest/maximum completed nonperiodic change (input, navigation,
  alerts or results). Periodic telemetry refreshes do not overwrite this pair.
- `phase-us control/storage/serial/ui`: maximum duration of each main-loop
  phase: control/I/O, storage/protection, serial, and UI composition/drawing
  (including CAN/protection service between foreground drawing slices).

All durations include interrupts. Scene/change timing starts after composition
and includes time between render calls, not the earlier physical input delay.
`diag ui reset` clears display and phase timings; it leaves pending drawing,
configuration and CAN counters alone. Let a page finish before resetting, then
move one highlight and capture `diag ui` plus `diag bus`. The drawing clock
remains 8 MHz and display reads remain capped at 1 MHz. `PSU_DISPLAY_SPI_HZ` permits the slower
supported clocks for testing; it does not alter CAN speed. See the
[loaded bench comparison](VALIDATION.md#ui-redraw-bench-comparison).

Application advances EEPROM saves for UI and serial even when the display is
absent. The UI observes the matching completion token and verification result.

## Recovery from notifications and results

The unavailable-members notification has **Acknowledge** and **Config** side by
side. The wheel initially selects Acknowledge. Turning to Config and clicking,
or tapping Config, opens the three-column availability overview with the affected
group selected. Cards show Ready/Absent counts and green Settled, amber Settling
or red Config Required. Open a group for **Include / Exclude / Choose individuals**.
Individual checkboxes mean **checked = excluded, unchecked = included/required**;
the four-row list scrolls with wheel focus or one-row touch Up/Down arrows.
CONFIRM opens a review, with **Remember + save config** checked and **Cancel
selected**. Cancelling review retains the draft; cancelling the checklist discards it.
Available membership changes require OFF and confirmed zero hold first.
Opening Config does not acknowledge the incident, accept an absence or enable
current. Back from the overview returns to that incident; Acknowledge then returns to the
original screen. An interrupted editor retains its value unless configuration
changes invalidate it. Recovery uses one return bookmark; following a further
blocked-operation shortcut keeps that same original return point.

| Incident/result | Shortcut |
| --- | --- |
| Missing, excluded, returned or unstable members | Config → availability overview, affected group selected |
| Active dropout | Config for missing CAN/AC; Details for a responding control fault |
| Setting/fan fault, reconciliation or unconfirmed stop | Details → PSU diagnostics → Retry ALL |
| CAN initialization or latched cutoff | Diagnostics → bus/control → Retry ALL |
| Receive loss, discovery capacity, identity conflict | Diagnostics → bus counts, ages, counters and identity |
| Temperature warning/error | Temps → input/output readings, current factor and hot timer |
| Thermal shutdown / missing temperature | Temps, plus separate guarded Resume / Retry sensors |
| Failed save / earlier snapshot saved | Review save → current RAM drafts/profile → Save settings |
| Missing calibration / calibration save failure | Calibrate → existing workflow |
| Missing voltage profile / apply blocked by OFF | Settings / Enable |
| Unbound units, no group, wrong deployment, unavailable display | Specific help; commissioning and binding stay in serial |

Retry, Save, Enable and sensor retry open a review with **Cancel initially
selected**. Retry explicitly affects **all PSUs**: reconciliation starts at zero
and may restore the authorized profile if enable is ON. Enable does not apply
staged edits. Thermal resume still requires cooling and verified persistence.
Save stores the reviewed RAM configuration without applying it; success appears
only after the asynchronous journal writer verifies its record. Newer drafts are
reported as unsaved. Busy operations retain ownership and show progress.

Reviews expire after 15 seconds. Configuration, discovery/identity, availability,
operation or incident changes invalidate them. An obsolete review returns to
its details with **Changed; review again**. Fresh touch/wheel input is required
after navigation. Alerts continue accumulating in the list during recovery;
protective actions and GPIO states remain independent of navigation and Ack.

Regenerate the preview from real layout tests after installing the display libraries:

```sh
mkdir -p /tmp/r4850-ui
PSU_UI_SNAPSHOTS=/tmp/r4850-ui python3 tools/test_host.py
PSU_UI_SNAPSHOTS=/tmp/r4850-ui python3 tools/test_touch.py
python3 tools/render_ui.py /tmp/r4850-ui docs/ui-preview.html
```

See [validation](VALIDATION.md) for build sizes and the remaining physical bench
checks. Host tests and builds do not establish the chosen module's electrical
compatibility, worst-case interrupt timing, or PSU behavior on a loaded CAN bus.

## Enable and control status

**Tools → Startup** opens the availability overview; touch can also use the status
row. The overview's **Controller** section keeps Stop/zero-current hold, guarded
Enable, diagnostics, commissioning probe and Retry ALL reachable even with no
groups configured. Hard AC cutoff remains accessible when fitted.

Remembered participation is activated only after verified persistence. Session-only
include/exclude remains a runtime override, even after unrelated UI saves. Included
absent units are required; excluded returning units stay excluded until explicitly
changed. Profiles optionally store/restore the same confirmed choices, default No;
unconfirmed checkbox drafts are not profile data. [STARTUP.md](STARTUP.md) explains
the serial commands, legacy permissions, blockers and bench verification.

Groups with accepted exclusions show `ACT n/m` in amber, while genuine faults
keep red error precedence. PSU rows distinguish remembered exclusions, session-only
exclusions and legacy permissions still needing review. Successful confirmation,
failed persistence and saved-but-not-activated snapshots have separate result text.

The spare row below the V/A/W header shows intentional zero-current hold and its winning
control source in amber. Reconciliation and zero-current startup are also
visible. A failed or unknown stop is red and is never labelled confirmed
zero hold. Charger/config title rows show compact OFF/source or fault tags.
PSU details include requested/ACKed fan mode and control failure/register.
Existing per-unit errors and measured telemetry remain visible. SIM pagination
uses the last three cells of the status row and retains the permanent SIM mark.
See [CONTROL.md](CONTROL.md) for examples, diagnostics and enable semantics.

Successful OFF now shows `ZERO A: SERIAL OFF` / `ZERO A: ENABLE_IN OFF` /
`ZERO A: BOOT OFF`; units remain awake for cooling. Hardware standby is only a
latched fault fallback, visible in `diag control`.

## Operator alerts and temperature protection

See [ALERTS.md](ALERTS.md) for the shared warning/error catalogue, GPIO definitions,
thermal percentages, temperature-sensor retry, persistent thermal OFF and the
bench sequence. Serial adds `alerts`, `alerts ack ID|all`, `diag thermal` and
`thermal retry GROUP|slots|all`. Tools → Alerts exposes the same incidents.
Acknowledgement preserves protection and returns to the previous screen; it
does not accept unavailable members or resume charging. Thermal details have
separate Resume/Retry actions. The overview retains a thermal-stop banner after
acknowledgement and a Tools incident-count badge.
