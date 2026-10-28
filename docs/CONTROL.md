# Enable control, reconciliation and fans

This firmware controls one parallel DC bus. It now requests standby on every
verified **bound** PSU at controller boot, on explicit reconciliation, and when
that unit returns or loses readiness. Discovery itself remains read-only;
unbound, ambiguous and wrong-deployment devices receive no setting writes.

## Commission and start

1. Follow [GETTING_STARTED.md](GETTING_STARTED.md) to discover, inspect, bind,
   and group the actual identities. Binding makes the unit managed: background
   reconciliation will request standby, evaluate the fan policy, set common voltage and zero
   current. `bind` itself does not transmit.
2. Run `diag control` and `diag group GROUP1`. Wait for `held-in-standby`,
   `standby-confirmed=1`, fresh telemetry and broadcasts. A deliberate standby
   can report not-ready in the raw broadcast; it is not itself a fault.
3. Stage `set voltage 54` and `set current GROUP1 55` (example values only).
4. Run `autoresume on`, `preview all`, then `apply all`. With no authorized
   profile yet, ON alone stays blocked; apply authorizes the staged profile.
5. Wait for the complete accepted result. Check `diag control`,
   `diag group GROUP1`, and measured `telemetry GROUP1`, then `save` if this
   configuration and enable preference should survive a reboot.

`autoresume off` is now a **live stop request**, even during an apply. It cancels
unfinished operating writes and requests standby from reachable managed units.
`autoresume on` permits the authorized profile to run after reconciliation; it
never applies uncommitted drafts. `set` remains staging only. Online apply while
OFF is blocked with an enable-control explanation; offline defaults remain an
explicit, separate operation. An enable event invalidates pending UI edits and
serial confirmations so an old Apply cannot defeat a later stop.

## Startup and returning units

The cooperative controller does the following, without blocking CAN reception,
serial, input or drawing:

1. Corroborate stable identity and resolve the temporary CAN address. Recheck
   identity immediately before every write, including standby and fan writes.
2. Request standby (`0x0132`, flag 1), require its matching ACK, and observe a
   **new** output-current sample within ±0.5 A. The measurement window is the
   greater of 5 seconds and three poll periods. Cooling continues while waiting.
   An ACK alone does not establish that output stopped.
3. Evaluate cooling and require its requested fan mode acceptance (`0x0134`);
   prepare the authorized
   common voltage and temporary zero online current while held in standby.
   Before the first authorized profile, use the compiled deployment voltage,
   not a staged edit. Preparation does not replace saved authorized currents.
4. Hold until effective enable is ON, all configured members have verified
   identity/fresh telemetry/broadcasts, all required preparation succeeded,
   and the common voltage is explicitly authorized across the installation.
5. Release standby at zero current. Require a new ready broadcast and a new
   low-current sample after the wake ACK from every returning recipient before
   restoring its authorized profile. No positive-current write precedes this
   barrier. A failed or unconfirmed wake triggers another standby attempt and
   latches the failure.
6. Restore authorized voltage/current through the normal ACK-checked job.
   Existing healthy units keep their shares when another unit disappears;
   returning units reconcile before rejoining. No load is redistributed.

A missing member blocks initial release and automatic recovery. The existing
confirmed `apply GROUP1 partial` override is only for already reconciled online
recipients; it retains their original shares and does not bypass startup.

Timeouts/transport failures receive at most two command attempts in one control
step; rejected settings latch immediately. There is no periodic fault-clearing
write loop. Diagnose the cause, then use **`reconcile`** to cancel an apply,
request standby and retry from the beginning. A genuine identity/connection
recovery also creates a new reconciliation attempt. A fresh unexpected current
while confirmed in standby invalidates that confirmation, requests standby again,
and latches a fault for review.

A controller cannot prevent output before it obtains a verified address and
successfully communicates. Standby is volatile and applies while CAN-connected;
PSU offline defaults still govern PSU startup and communication-loss fallback.
This is not a hardware interlock, output contactor, or electrical isolation.
Validate standby/zero/wake ordering on the actual G2 firmware without a battery
before loaded commissioning. See [protocol evidence](PARITY.md).

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
  is missing, stale, nonfinite or implausible. A new high-current apply requires the MAX ACK
  before its current increase can be sent.
- Return to AUTO only after temperature is **≤35°C** and both requested and
  measured current are **≤25 A**, continuously for **30 seconds**. Within the
  hysteresis band retain the previous requested mode.
- While OFF and confirmed stopped, effective requested current is zero while
  the authorized profile remains intact. Enabling anticipates the authorized
  current before release, including while the PSU is still stopped.
- Fan decisions continue in every control phase, including output-control
  faults. Serial/switch changes and explicit `reconcile` preserve the accepted
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

## Optional external PWM fan

Set **`PSU_FAN_PWM_PIN=6`** to enable a fan-driver output on Mega D6; `-1`
(default) disables the physical output. `mega2560_touch_fan` supplies an example
touch + ENABLE_IN + external fan build. Keep the other flags when extending a
different environment. `PSU_FAN_PWM_INVERTED=1` supports an inverting driver;
default 0 means a higher electrical duty requests more cooling.

Use a suitable powered fan driver with a common ground. The GPIO is a logic
signal, not a fan power supply. This adapter uses ordinary Arduino `analogWrite`
(approximately 490 Hz on D6), not the 25 kHz control waveform expected by many
four-wire PC fans. Check the selected driver's input requirements. The
[Arduino AVR Mega mapping](https://github.com/arduino/ArduinoCore-avr/blob/master/variants/mega/pins_arduino.h)
places D6 on Timer4; the adapter does not reconfigure timers or add interrupts.
Compile-time checks reject conflicts with CAN, display, touch, ENABLE_IN, wheel
pins, and the wheel's Timer1 PWM channels D11/D12.

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
re-evaluates at most every 100 ms, and writes GPIO only when duty changes. Enable
events do not turn it off. `diag control` shows whether a physical pin is enabled,
logical demand `/255`, maximum temperature and the demand reason. Demand is a
command, not measured fan speed. Debug builds compute/display it without
accessing the fan GPIO, even if the pin is configured. No EEPROM layout changes.

## Operator and UI hooks

`diag control` shows effective enable, winning source, raw/debounced switch,
thresholds, per-unit phase, standby confirmation, last control result/register,
latched failure/register, requested fan mode, ACKed mode and decision reason.
`diag group NAME` and `diag psu N` include the per-unit fields. Serial announces
changes in global control state without continuous telemetry spam.

The group overview keeps the V/A/W header and three columns. Its spare row shows
amber intentional standby/reconciliation messages or red faults. Labels include
`STANDBY: SERIAL OFF` (the `autoresume` command), `STANDBY: ENABLE_IN OFF`, `STOPPING...`,
`STOP UNCONFIRMED`, and `START BLOCKED` with the affected PSU. Charger/config
screens share their title line with a compact source/status tag. Details show
fan request/ACK and the exact failed control register. Fresh measurements keep
updating while held. A stop request is never presented as confirmed standby.

Future UIs can consume `enableControl()`, `controlStatus(now)`,
`unitControl(slot)`, and `externalFanStatus(now)` directly; policy belongs to the
backend. External fan status includes logical duty, maximum temperature,
telemetry completeness, and the PSU/reason causing a full-duty override.

`PSU_UI_DEBUG` models standby, fan acceptance and zero-current wake, with no
physical CAN, EEPROM or ENABLE_IN GPIO access. Additional scenarios are
`standby`, `fan-hot`, `fan-cool`, `fan-sensor-fault`, `fan-reject`,
`standby-reject`, and `wake-current`. Use `ui-debug enable-input on/off` to
exercise normal debounce/latest-wins handling virtually. Scenario priming now
includes the entire multi-unit reconciliation and may take tens of seconds.
