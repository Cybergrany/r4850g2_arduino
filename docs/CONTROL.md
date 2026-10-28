# Enable control, reconciliation and fans

This firmware controls one parallel DC bus. Normal OFF means **zero-current hold
with the PSU awake for cooling**, not hardware standby. Managed units reconcile
this way at boot, on explicit retry, after an observed AC/identity change, and
after readiness loss while delivering current.
Discovery itself remains read-only; unbound, ambiguous and wrong-deployment
devices receive no setting writes.

## Commission and start

1. Follow [GETTING_STARTED.md](GETTING_STARTED.md) to discover, inspect, bind,
   and group actual identities. Binding makes a unit managed: background control
   starts zero-current reconciliation. `bind` itself does not transmit.
2. Run `diag control` and `diag group GROUP1`. Wait for `held-at-zero-awake`,
   `zero-confirmed=1 awake=1` and fresh telemetry. `broadcast-ready=0` can be
   normal while `current-state=zero`; output readiness is a separate check.
3. Stage `set voltage 54` and `set current GROUP1 55` (example values only).
4. Run `autoresume on`, `preview all`, then `apply all`. With no authorized
   profile yet, ON stays blocked until apply authorizes one. If an older profile
   exists, ON can restore it immediately: inspect it first and allow that restore
   to finish before applying new drafts. Use an unloaded output for initial tests.
5. Wait for the complete accepted result. Check `diag control`,
   `diag group GROUP1`, and measured `telemetry GROUP1`, then `save` if this
   configuration and enable preference should survive a reboot.

`autoresume off` is a **live zero-current request**, even during an apply. It
cancels unfinished operating writes, sends zero current to reachable managed
units, and requires new low-current telemetry. A unit already known awake stays
awake: a normal OFF/ON edge sends no standby command. Fan modes, acknowledged
settings and elapsed cooldown survive the edge. Repeating OFF does not reset
faults or restart reconciliation. Authorized charging currents remain intact.

`autoresume on` permits the authorized profile after the installation-wide
release checks; it never applies uncommitted drafts. Online apply while OFF is
blocked; offline defaults are an explicit, separate operation. Enable events
invalidate pending UI edits and serial confirmations.

## Startup and returning units

The optional common AC permit, readiness timing, source-group settling and
remembered absence policy are described in [STARTUP.md](STARTUP.md).

The controller performs these cooperative steps without waiting loops:

1. Corroborate the bound identity and resolve its temporary CAN address.
   Recheck identity immediately before every write.
2. Send **zero online current** (`0x0103`), require its matching ACK, and observe
   a **new** output-current sample within ±0.5 A. The observation window is the
   greater of 5 seconds and three poll periods. Zero requests take priority over
   voltage/wake preparation. An ACK alone does not confirm zero output current.
3. Evaluate cooling and require the requested fan mode ACK (`0x0134`). Set the
   authorized common voltage, or the compiled deployment voltage before a
   profile exists. A staged voltage edit is never substituted here.
4. Clear any inherited hardware standby (`0x0132`, flag 0), only after that
   unit has accepted zero current and voltage and met the fan policy. Require
   a fresh broadcast and a fresh low-current sample after the wake ACK.
   A ready broadcast normally completes this observation. Some G2s report
   not-ready at a zero limit: after max(15 seconds, three poll periods), fresh
   `0x0183` containing only the shutdown/fan-MAX status bits (bits 9/10, or zero)
   can complete the zero hold instead. Missing/stale status or any other set bit
   cannot use this exception. A fresh AC-absent flag waits without a readiness
   fault. Reassert zero/fan/voltage after this startup allowance, then require
   new low-current, broadcast and ready-or-benign-status observations again.
   The unit is held at zero and awake, even if effective enable is OFF.
5. Restore authorized charging current only when enable is ON, the complete
   common-voltage profile is authorized, and **all required participants** pass
   identity, telemetry, verified zero-hold/output-readiness, voltage and fan checks. Every returning
   recipient must have completed the zero/wake observations first.

The unsolicited output-ready flag may toggle while the accepted current limit
is zero. With fresh benign status and confirmed low current, this does not
repeat wake or erase fan cooldown. A stable zero hold can cool to AUTO after
the normal 30-second release interval. Voltage/current refresh continues;
the persistent fan mode is written only when needed.

An online apply or authorized restore sends **the requested current limit**;
there is no extra startup current or hidden redistribution. A nonzero write
from zero starts a bounded max(15 seconds, three poll periods) output-readiness
observation. `current-state=waiting-for-output-ready` and the serial/UI starting
banner distinguish it from confirmed operation. After the matching current ACK,
require **at least three received ready broadcasts spanning two seconds**, with
**no gap over one second** and fresh voltage/current telemetry from the same
verified identity. Each frame is processed in arrival order. One ready pulse,
repeated reads of the same frame, or a burst at one timestamp cannot qualify.
These defaults live in `ControlConfig.h`.

A not-ready frame or excessive gap restarts only the two-second qualification;
it does not re-zero/re-wake the PSU or extend the original startup deadline.
The deadline is fixed at the first nonzero-current transmission, before the ACK;
changing poll timing later cannot extend it. Draft edits remain available, but
another live voltage or positive-current apply affecting a qualifying unit is
blocked with an explanation. A zero-current group apply or `autoresume off`
can stop it. Periodic settings refresh does not resend current while qualifying.
Other units qualify independently; this does not change group membership or
redistribute current.

`diag control` / `diag psu 1` show, for example:

```text
Current accepted; verifying readiness
ready-stable=750/2000ms frames=3/3 startup-remaining=11200ms; output may be live
```

The PSU detail screen shows the same progress and deadline, with `VERIFY` on its
group card. **Current may already flow during qualification.** Measured voltage
need not equal its limit (for example 52.63 V while charging with a 57 V limit),
and measured current need not reach the requested limit. Check those readings
separately; qualification checks the ready indication, not load delivery.

A timeout latches `output readiness timed out` and uses the existing bounded
standby fallback/cutoff. This fault survives CAN loss, AC transitions and address
recovery in the same controller session; explicit `reconcile` is required.
If stop proof was lost with the link, diagnostics show it as unconfirmed; a
verified return attempts the bounded fault stop without restoring current.
An accepted apply report still means command acceptance, not measured delivery;
check `diag control` and measured voltage/current before increasing a load.

A missing member does not prevent reachable units from staying awake at zero
for cooling. Without an explicit absence acceptance, it still blocks initial
charging and automatic current restoration. Accepted identities stay excluded,
including on return, until separately included after reconciliation.
Existing healthy online units keep their shares when another unit disappears;
no load is redistributed. A confirmed `apply GROUP1 partial` remains limited to
already reconciled online recipients and cannot bypass initial charging checks.

## Failures and explicit recovery

Timeouts/transport failures receive at most two attempts for a control command;
rejections latch immediately. A phase change does not reset an in-flight fan
command's retry budget. There is no periodic fault-clearing write loop.

If zero current cannot be confirmed, wake fails, or unexpected current exceeds
±0.5 A while held, the controller makes a **bounded hardware-standby fallback**
(`0x0132`, flag 1). This is a fault path, not the normal OFF behavior. Its ACK
also requires fresh low-current telemetry. Diagnostics keep the original fault
and identify confirmed hardware fallback separately; failed/unobserved stopping
shows `STOP UNCONFIRMED`. A fan/voltage failure while zero is already confirmed
can remain held in a latched fault without a standby request. Cooling policy
continues; a fan-command failure itself suppresses further fan writes until retry.

Fix the cause, then use **`reconcile`** to cancel an apply and retry from zero
current while retaining the authorized profile and fan hysteresis. ON alone
cannot clear a latched fault or wake a unit from the emergency fallback. A genuine
identity/connection recovery normally creates a new reconciliation attempt, but
does **not** clear an output-readiness timeout or a latched common-cutoff fault.

The output can remain electrically live while held at zero. A zero-current
limit is not isolation. The optional CHG_ALLOW output operates the installation's
common AC cutoff; it has no contactor feedback and does not isolate the DC bus.
Before verified CAN communication, or after communication loss, PSU offline
defaults can govern output. Validate zero acceptance, wake retention, measured
current and fault fallback on the actual G2 hardware without a battery before
loaded commissioning. See [protocol evidence](PARITY.md).

## Optional ENABLE_IN switch

Build with `-DPSU_ENABLE_ENABLE_IN=1`. Defaults in
[`ControlConfig.h`](../src/config/ControlConfig.h): Mega **D26**, active LOW,
internal pull-up, **50 ms debounce**. Connect a latching switch between D26 and
GND: closed = ON, open = OFF. D25 is touch CS and must not be reused.

Overrides: `PSU_ENABLE_IN_PIN`, `PSU_ENABLE_IN_ACTIVE_LOW`, and
`PSU_ENABLE_IN_PULLUP`. Static checks reject conflicts with enabled board I/O.
For example, add the flag to a local environment extending `mega2560_touch`;
retain that environment's existing touch flags. `mega2560_touch_enable` is a
provided example. This input is a software operator control, not a hard safety
interlock.

At boot, **saved autoresume OR the initial debounced switch ON** enables startup.
Thereafter the **latest event wins**, not a continuously evaluated OR:

| Event | Effective request |
| --- | --- |
| Switch changes to ON | ON, source ENABLE_IN |
| `autoresume off` while switch stays ON | OFF, source serial |
| Switch remains ON | Still OFF |
| Switch changes OFF, then ON | ON again, source ENABLE_IN |

A serial command received before the initial debounce finishes wins over that
initial sample. The switch's raw and debounced states remain visible separately
in `diag control`. A held opposite state can therefore legitimately differ from
the effective request.

`save` records the current effective preference using the existing EEPROM
`autoResume` byte. Switch edges and fan decisions never write controller EEPROM
automatically. Power cycling with the switch still ON can enable startup again,
even if a previous serial OFF was saved. `load`/`defaults` request a new held
reconciliation and do not replay a loaded profile; enable explicitly afterwards.

## Fan policy

All constants live in `ControlConfig.h`; all decisions are per PSU, using its
allocated authorized current, never the whole group total:

- MAX when output temperature is **≥40°C**, requested operating current is
  **≥30 A**, or fresh measured output current is **≥30 A**.
- Start at MAX when the physical unit is unknown. MAX when required telemetry
  is missing, stale, nonfinite or implausible. A new high-current apply requires
  the MAX ACK before its current increase can be sent.
- Return to AUTO only after temperature is **≤35°C** and both requested and
  measured current are **≤25 A**, continuously for **30 seconds**. Within the
  hysteresis band retain the previous requested mode.
- While OFF and confirmed stopped, effective requested current is zero while
  the authorized profile remains intact. Enabling anticipates the authorized
  current before release, including while the PSU is still stopped.
- Fan decisions continue in every control phase, including output-control
  faults. In normal policy mode, serial/switch changes and explicit `reconcile` preserve the accepted
  fan mode and elapsed cooldown. They do not force MAX or restart the 30-second
  timer. A real identity/connection reset starts conservatively at MAX again.
  A rejected fan command still latches and requires explicit recovery.

Only mode changes or bounded recovery attempts are transmitted. `0x0134` is
persistent **inside the PSU**, so it is not rewritten on each polling cycle.
We do not probe or use G6-specific duty/status registers `0x0114` / `0x0187`.
An ACK confirms mode acceptance, not fan RPM or physical cooling. A plausibly
stuck temperature sensor is not detectable from that reading alone; the separate
current condition still applies. Fan policy never reduces requested current.
A required fan failure instead produces an explicit latched fault and stop.

### Manual fan tests over serial

Use `fan <all|GROUP|slots> <auto|max|policy>` to change fan control immediately;
no voltage/current edit, `apply`, or `save` is needed. Targets are configured
slots, not temporary CAN addresses. Examples: `fan all max`, `fan GROUP1 auto`,
`fan 1 max`, `fan 1,3-4 policy`.

- `max` holds the selected PSUs' fans in MAX. It works while held or enabled.
- `auto` selects the **PSU's own AUTO mode**, bypassing the Mega's temperature,
  current and cooldown decision for the selected units. This bench override
  requires effective enable OFF and confirmed awake zero-current holding.
  Run `autoresume off`, then wait for `zero-confirmed=1 awake=1` in `diag control`.
  AUTO is cleared if enable turns ON or the zero-hold conditions cease to hold.
- `policy` removes the override and returns to the normal Mega thresholds.
  Leaving a manual override starts conservatively at MAX; the usual 30-second
  cooldown can then release it. It does not necessarily send AUTO immediately.

Read `diag psu 1`, `diag group GROUP1`, or `diag control` after each command.
Check `override`, `fan requested`, `acknowledged`, `control-last`, and `LATCHED`.
An accepted serial request is not a PSU ACK. A changed mode uses the normal
per-write identity check, exact ACK match and bounded retry path. A mode already
ACKed is not rewritten repeatedly. Missing/ambiguous targets block the entire
selection before any override changes; active apply jobs report busy. Fan
failures remain latched and require `reconcile` after fixing the cause.

Overrides are RAM-only and survive a console reconnect without MCU reset.
They clear on `policy`, `reconcile`, configuration load/reset, or that unit's
identity/connection reset; AUTO also clears on enable. `save` does not persist
them. The PSU's fan-mode register itself is persistent, so finish with
`fan all policy` before disconnecting the controller. These commands change no
voltage/current profile and grant no charging authorization.

If external PWM is enabled, it uses the resulting per-unit demands: any manual
MAX holds full duty; with every unit in AUTO, the temperature curve applies.
Missing/invalid telemetry still forces external full duty. Fan mode acceptance
does not measure RPM or guarantee a physical speed change.

## Optional external PWM fan

Set **`PSU_FAN_PWM_PIN=6`** to enable the external fan's control output; `-1`
(default) disables physical output. Use `mega2560_serial_fan` for serial + fan
with no screen, wheel or switch, or `mega2560_touch_fan` for touch + ENABLE_IN +
fan. Existing serial-only builds remain available without the external output.

This is **25 kHz hardware PWM for a four-wire computer fan's control input**.
Supply the fan continuously from its rated supply; connect its ground to Mega
GND and its 5 V-compatible PWM input to D6 (or a suitable logic buffer). The
power and PWM wires are separate: never put the fan's supply voltage on the
PWM input. The tachometer wire is not used by this firmware.

Default `PSU_FAN_PWM_INVERTED=0` makes HIGH/100% at the fan input full speed.
Use `=1` only if an external buffer inverts that signal. Verify the selected
fan's pinout and input specification. Noctua documents 25 kHz, an internally
pulled-up PWM input, and CMOS/push-pull drive; it cautions that open-collector
waveforms can be degraded by weak pull-ups. Its low-duty behavior is model
specific: **0% duty does not guarantee that every fan stops**. The firmware
requests zero below the curve's lower endpoint; use a fan supporting zero-RPM
operation if a physical stop is required. [Fan interface reference](https://cdn.noctua.at/media/Noctua_PWM_specifications_white_paper.pdf).

Supported pins are **D6/D7/D8 (Timer4)** and **D44/D45/D46 (Timer5)**. The chosen
timer is dedicated to this output; backlight PWM on any channel of that timer
is rejected at compile time. CAN/display/touch/switch pin conflicts are also
rejected. Timer0 (`millis`/`micros`) and Timer1 (wheel) are untouched. Mode 10,
ICR TOP=320, clock=16 MHz and prescaler=1 produce 25 kHz without an ISR; compare
values 0/TOP produce constant LOW/HIGH at the endpoints. [ATmega2560 timer documentation](https://ww1.microchip.com/downloads/en/devicedoc/atmega640-1280-1281-2560-2561-datasheet-ds40002211a.pdf).

The backend computes one demand across **all configured PSU members**:

- Use the maximum fresh output temperature, not an average.
- Duty is 100% if any PSU requests MAX under the existing temperature/current
  policy, including its 30-second cooldown hold. The current threshold remains
  **per PSU**, not the sum of group/bus current.
- Missing/unverified units, invalid/stale temperature or current, and CAN
  initialization failure request 100%. Missing hardware cannot silently lower
  cooling demand. Unmanaged discovery-only units do not contribute.
- Once all units permit AUTO, use smoothstep `x²(3−2x)`, where
  `x = clamp((temperature−20)/(40−20), 0, 1)`. The upper endpoint uses
  `fanOutputTempThreshold`, so changing that single constant updates both
  internal fan policy and this curve. The off endpoint is `externalFanOffTemp`.

| Maximum output temperature | Duty after any MAX hold clears |
| --- | ---: |
| ≤20°C | 0% |
| 25°C | ~16% |
| 30°C | ~50% |
| 35°C | ~84% |
| ≥40°C | 100% |

The implementation uses fixed-point integer smoothstep, no allocation or
transcendental math. The output starts at full duty before display/CAN setup,
re-evaluates at most every 100 ms, and updates the timer compare only when duty
changes. Enable events do not turn it off. `diag control` shows whether a physical pin is enabled,
logical demand `/255`, maximum temperature and the demand reason. Demand is a
command, not measured fan speed. Debug builds compute/display it without
accessing the fan GPIO, even if the pin is configured. No EEPROM layout changes.

## Operator and UI hooks

`diag control` shows effective enable, winning source, raw/debounced switch,
thresholds, per-unit phase, zero confirmation, awake state, last control result/register,
latched failure/register, requested fan mode, ACKed mode and decision reason.
`diag group NAME` and `diag psu N` include the per-unit fields. Serial announces
changes in global control state without continuous telemetry spam.

The group overview keeps the V/A/W header and three columns. Its spare row shows
amber intentional zero-hold/reconciliation messages or red faults. Labels include
`ZERO A: SERIAL OFF` (the `autoresume` command), `ZERO A: ENABLE_IN OFF`, `STOPPING...`,
`STOP UNCONFIRMED`, and `START BLOCKED` with the affected PSU. Charger/config
screens share their title line with a compact source/status tag. Details show
fan request/ACK and the exact failed control register. Fresh measurements keep
updating while held. A stop request is never presented as confirmed zero hold.
Fault standby is explicit in diagnostics.

Future UIs can consume `enableControl()`, `controlStatus(now)`,
`unitControl(slot)`, and `externalFanStatus(now)` directly; policy belongs to the
backend. External fan status includes logical duty, maximum temperature,
telemetry completeness, and the PSU/reason causing a full-duty override.

`PSU_UI_DEBUG` models zero holding, inherited/fault standby, fan acceptance and
wake observations, with no physical CAN, EEPROM, ENABLE_IN or fan GPIO access.
Scenarios include `zero-hold`, `fan-hot`, `fan-cool`, `fan-sensor-fault`,
`fan-reject`, `zero-reject`, `stop-unconfirmed`, and `wake-current`. Use
`ui-debug enable-input on/off` to exercise normal debounce/latest-wins handling virtually. Scenario priming now
includes the entire multi-unit reconciliation and may take tens of seconds.
