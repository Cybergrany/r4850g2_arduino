# Touchscreen and CAN-free UI testing

The supported panel is a **240x320 ILI9341 SPI module mounted in 320x240
landscape**, including 2.8-inch and 3.2-inch versions. Touch uses a separate
**XPT2046 resistive controller** with T_CLK/T_CS/T_DIN/T_DO/T_IRQ pins. The TFT
pins alone do not provide touch input.

## Builds

| Environment | Input | CAN / settings storage |
| --- | --- | --- |
| `mega2560_touch` | Touch | Physical MCP2515 / EEPROM |
| `mega2560_touch_wheel` | Touch and wheel | Physical MCP2515 / EEPROM |
| `mega2560_ui_debug` | Wheel | Simulated PSUs / volatile RAM |
| `mega2560_touch_debug` | Touch | Simulated PSUs / volatile RAM |
| `mega2560_touch_local` | Touch, no serial | Physical MCP2515 / EEPROM |
| `mega2560_touch_debug_local` | Touch, no serial | Simulated PSUs / volatile RAM |

`PSU_ENABLE_TOUCH` and `PSU_UI_DEBUG` default to zero in `BuildOptions.h`.
Touch requires `PSU_ENABLE_DISPLAY=1`. The encoder remains independently
optional. The `_local` profiles disable serial for local-only operation.
Simulation profiles omit the CAN dependency and compile
out `Mcp2515Transport`, while retaining real protocol/controller/UI logic.

Start without PSUs or an MCP2515 module:

```sh
pio run -e mega2560_touch_debug
pio run -e mega2560_touch_debug -t upload --upload-port COM15
pio device monitor -b 115200 --port COM15
```

The startup message explicitly identifies SIM/RAM operation. Every screen has
an amber **SIM** mark. Display and touch hardware are real; all PSU readings,
responses and settings are simulated. Firmware never switches to real CAN at
runtime. Upload a production profile for real control.

## Wiring

Keep the existing TFT wiring in [LOCAL_UI.md](LOCAL_UI.md). Additional defaults:

| XPT2046 pin | Mega connection |
| --- | --- |
| T_CLK | 52 / SCK, shared with TFT and MCP2515 |
| T_DIN | 51 / MOSI, shared |
| T_DO | 50 / MISO, shared |
| T_CS | **25**, separate from TFT CS 22 and CAN CS 10 |
| T_IRQ | **18**, sampled as a digital input; no touch ISR |

Configure these in `src/config/TouchConfig.h`. Generic modules may accept 5 V
power while requiring 3.3 V logic: verify level shifting for **both** controllers.
Deselect any unused SD/touch peripherals and verify each device releases MISO
when deselected. Pin 18 cannot simultaneously serve another peripheral such as
Serial1 TX. TFT SDO/MISO is still required by the default display-ID check.

## Calibration

On first touch boot, the wizard requests five targets. Hold each cross steadily
until it moves, **release**, then press the next cross. Release the fifth target
to finish. Three targets determine the affine transform; two further targets
verify it within 12 pixels. Unstable samples, a degenerate transform or a
60-second inactivity timeout produce a failure screen. Release and tap to retry.
A wheel hold or `touch debug off` can exit an unsuccessful wizard.

Successful calibration saves automatically. Wait for **Calibration saved**.
After a write failure, release and tap to retry, or use the wheel/serial to exit.
Normal touch controls remain unavailable until a valid transform exists. The
previous transform remains usable if recalibration fails. CAN and serial stay
active; calibration cannot replace an active local apply/save operation.

Open Tools > Calibrate touch to recalibrate, or use:

```text
touch calibrate
touch status
touch calibration export
```

`touch status` reports validity, rejected coordinate samples, maximum read time,
raw coordinates/pressure and storage state (`none`, `saving`, `ok` or `failed`). Export prints the six
Q16.16 affine coefficients and hardware profile for inspection/reference; it is
not an import command.

Production calibration is independent of PSU/group/deployment configuration.
Two 40-byte records occupy EEPROM addresses **216–255** and **472–511**, the
unused tails of the existing slots. Version, CRC, sequence, panel profile and
rotation validate the records. The charger record remains schema 2 and at most
216 bytes; old charger records remain readable. Compile-time bounds prevent
future overlap. Both writers share one readiness-aware byte-write scheduler.
This still uses only the first **512 EEPROM bytes**.

Change `touch::profile` when replacing the panel, or explicitly recalibrate.
Changing display rotation invalidates the saved transform. A firmware update
with the same profile/rotation preserves production calibration.

## Touch controls

The dashboard keeps three group columns and the measured V/A/W header. Tap a
group to open its chargers. Previous/Next change pages; Tools opens calibration,
diagnostics and (in debug builds) simulation controls. With a wheel, select
Tools as the third item in configuration. Holding on an empty-groups dashboard
also opens Tools, so empty debug fixtures are recoverable without serial.

Charger pages show two units with Back, Config and paging buttons. Tap a charger
for its explanation. Tap voltage/current in Config to open a fullscreen editor:
large requested value, live measurement, backend-derived range, minus/plus,
Cancel and Apply & save. Voltage is explicitly **ALL GROUPS**; current is the
selected group's total. Repeated labels/gaps are shortened to make room for
48-pixel-high footer controls; status/error text remains explicit.

Minus/plus adjust 0.1 V or 0.5 A per step. Hold repeats after 500 ms, every 120 ms.
The backend supplies exact bounds; wheel and touch share the same edit state.
Changes remain in the editor until Apply. Cancel discards the uncommitted edit.
Apply waits for PSU acknowledgements and then saves through the normal journal.
Missing-member confirmation, partial shares and failure semantics are unchanged.
Serial edits invalidate a stale local edit before it can commit.

A press is captured to its initial button; dragging away cancels it. Apply
activates once after 40 ms of stable release. A new screen must finish painting and observe a
release before accepting another press. Wheel input consumes a simultaneous
touch gesture. The first touch on a dimmed display only wakes it. Overview
paging is automatic only when both wheel and touch are disabled.

## Touch diagnostics

Tools > Touch diagnostics, or:

```text
touch debug on
touch status
touch debug off
```

The screen shows raw and mapped coordinates, pressure/contact, a marker, target
name and maximum read duration. Test minus/plus targets are inert; **only Back
navigates**. Touching diagnostics cannot change PSU settings. Serial control
continues normally. Diagnostic output is queried explicitly, not streamed on
every sample. IRQ avoids idle SPI reads; active samples are paced at 20 ms.
The bounded SPI conversion sequence is adapted from
[Paul Stoffregen's XPT2046 driver](https://github.com/PaulStoffregen/XPT2046_Touchscreen),
with its MIT notice retained in `Xpt2046Input.cpp`.

## Simulation scenarios

Debug startup seeds eight bound units across four groups (3/2/2/1 members).
It performs normal simulated identity checks and restores the fixture's
operating settings before introducing a scenario. Reconciliation now includes
standby, fan, voltage, zero current and wake checks; allow tens of seconds for
this priming; `ui-debug status` reports it. No physical CAN or EEPROM is used.

Select Tools > SIM scenarios, turn/use Previous/Next, then Run. The final two
choices restart from a RAM save or reset fixtures. Serial equivalents:

```text
ui-debug status
ui-debug scenario missing
ui-debug scenario healthy
ui-debug restart
ui-debug reset
```

Scenario changes wait for an active apply/save to complete. Each scenario
starts from the same commissioned fixture. Names and expected exercises:

| Name | Exercise |
| --- | --- |
| `healthy` | All groups/units, pagination, V/A edits, ACKs and successful save |
| `empty` | No configured groups; Tools remains accessible |
| `missing` | PSU 2 stops responding; wait for expiry, then partial-current confirmation |
| `stale` | PSU 2 retains identity/broadcasts but stops DATA metrics |
| `unbound`, `unassigned` | Missing binding or source-group assignment |
| `conflict`, `replacement` | Duplicated identity or a different identity at PSU 2's address |
| `not-ready` | PSU 2 broadcasts a not-ready status |
| `voltage-unsynced` | Loaded draft with no runtime voltage ACKs; apply global voltage before group current |
| `reject`, `timeout`, `identity-timeout`, `transport` | Rejected settings, missing setting/identity ACKs or failed sends; submit an apply |
| `partial-failure` | PSU 2 rejects while other members can accept |
| `save-failed` | PSUs accept; RAM storage fails verification |
| `changed` | A config revision invalidates a local edit |
| `save-changed` | A config revision changes during saving; the UI reports an earlier snapshot |
| `bus-offline` | Simulated transport initialization fails; UI shows the unavailable bus |
| `extremes` | Large power readings, negative input temperature and an eight-character group name |
| `wrong-deployment` | Fixture belongs to another deployment; writes blocked |
| `identity-pending` | Identity responses cease; wait for the identity lease to expire |
| `standby` | Intentional OFF banner; authorized profile retained |
| `fan-hot`, `fan-cool`, `fan-sensor-fault` | MAX, cooldown to AUTO, or invalid-temperature fallback |
| `fan-reject` | Required MAX rejected; startup blocked with per-unit fault |
| `standby-reject` | Stop not acknowledged; red STOP UNCONFIRMED banner |
| `wake-current` | Nonzero current after wake at zero; standby requested and fault latched |

For `identity-timeout`, submit an apply while the prior verified identity is
still fresh; after its lease expires, preview correctly blocks instead.
`changed` fires once per local edit; `save-changed` fires once per save,
including local Apply & save. There is no automatic rollback of accepted simulated
writes, just as with real PSUs.

`save` and local Apply & save use the real storage manager against a **512-byte
RAM-backed byte device**. `load` and `ui-debug restart` exercise readback;
restart falls back to fixtures if no valid saved configuration exists. `reset`
erases RAM saves and restores fixtures (the current in-memory touch transform
remains usable until hardware reset). Physical reset/power loss discards all
simulated saves, including calibration. Production EEPROM is never read or
written by a debug build. Calibrate again after first switching to production.

## Validation and limits

```sh
python3 tools/test_host.py
python3 tools/test_touch.py
PLATFORMIO_CORE_DIR=../.platformio python3 tools/check_debug_build.py
```

The last command examines all three built debug ELFs for simulator/RAM symbols and
rejects physical MCP2515 or EEPROM access symbols. Host tests cover real
controller/UI behaviour, calibration interruption, gesture ownership, shared
writer gating and simulation faults. They do not establish physical touch
accuracy, stack margin or SPI/CAN timing. On production hardware, hold +/- and
navigate while all PSUs broadcast; inspect `touch status` and the existing
`diag bus` overflow/loop counters. Debug mode cannot measure real CAN contention.

`ui-debug enable-input on/off` feeds a virtual switch through the normal debounce
and latest-event-wins logic. Debug builds never read ENABLE_IN GPIO, including
when its build flag is enabled. See [CONTROL.md](CONTROL.md) for fan thresholds,
startup policy and control diagnostics.
