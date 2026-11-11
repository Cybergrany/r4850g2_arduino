# Operator alerts and thermal protection

Alerts, thermal decisions and GPIOs share the controller backend. Display and
serial acknowledgement dismiss notifications; they never clear a current limit,
resume charging, accept an absence or include a returned PSU.

## Operator commands

```
alerts
alerts ack 12
alerts ack all
diag thermal
diag psu 1
```

`alerts` prints each incident's ID, severity, slot/group (or system scope), active
or cleared state, acknowledgement, cause, protective action and recovery.
IDs are session-local; serial reconnect, `hello` and Ctrl-X do not reset them.
Serial also broadcasts a compact summary every **five seconds** while alerts
are active, including acknowledged alerts. Each row includes severity, PSU/group,
cause and protective action. `alerts every 10` changes the period to ten seconds;
`alerts every 0` disables automatic broadcasts and `alerts every` reports the
current period. This preference lasts until reboot and survives `hello`/Ctrl-X;
it does not affect UI notifications, GPIOs or protection. The build default is
`PSU_SERIAL_ALERT_INTERVAL_MS` in `src/config/ConsoleConfig.h`.

Errors are printed before warnings. Previously active conditions get a cleared
row once, followed by silence when everything is clear. Newly retained,
unacknowledged incidents that arise and clear between summaries are included too.
Reports coalesce the latest retained state; they are not a lossless event log.
Command entry and requested reports defer automatic summaries. Backpressure
delays them without queuing missed periods or producing catch-up bursts. See
[serial broadcast behaviour](SERIAL.md#periodic-warningerror-broadcasts).

`diag thermal` shows temperature coverage, highest reading, percentage, hot-time
countdown, retained hot contributors, sensor latches, explicit absence waivers,
shutdown/persistence state and logical WARNING/ERROR outputs. Thermal levels are
0 normal, 1 warning and 2 error; stop reasons are 0 none, 1 over-temperature and
2 sustained over-temperature.

PSU diagnostics and the local PSU detail page show **authorized / effective /
acknowledged current limits** separately. The acknowledged value includes Huawei
encoding quantization and means command acceptance, not measured output. Use
`telemetry GROUP1` or `watch on` for actual current and both temperatures.

Recovery actions remain explicit:

- Missing or excluded PSUs: `diag startup`, then `startup accept GROUP1
  [remember]`, `startup exclude GROUP1 [remember]` or
  `startup include GROUP1 [remember]`, followed by `confirm yes/no`. Changes to
  available units require OFF and confirmed zero hold. Included absent units are
  required; excluded units remain excluded on return. The Config shortcut opens
  the availability overview, with the affected group selected. Acknowledgement
  does not approve a decision. See [participation and persistence](STARTUP.md#participation-include-exclude-and-remember).
- Temperature sensor loss: restore both valid readings, confirm the affected PSU
  is stopped, then `thermal retry 1`, `thermal retry GROUP1` or `thermal retry all`.
  If a setting/stop also failed, inspect `diag control`, correct it and
  `reconcile` before retrying the sensors. Ordinary reconciliation does not erase
  sensor latches.
- Thermal shutdown: wait for fresh cool readings, then issue a **new**
  `autoresume on`, use the notification's separate Resume button, or toggle
  ENABLE_IN OFF then ON. The controller remains OFF until its clear-save verifies.
  Inspect `diag thermal`: `resume-pending`, `saving`, `saved` and `failed` distinguish
  the request from successful persistence. An ON request while hot is blocked;
  it is not queued to restart later.
- Persistence failure: inspect configuration/storage and `save` again. Verify the
  reply and `diag thermal` before relying on a reboot. Calibration write failures
  appear on the local-UI source and clear after a successful calibration retry.
- Other control/CAN faults: inspect `diag control`, `diag bus` and raw diagnostic
  status, correct the cause, then explicitly `reconcile`.

A live dropout remains ERROR through zeroing/reconciliation until it recovers or
the operator explicitly accepts its absence. Accepted reduced capacity remains
WARNING. The remaining PSUs retain their allocations; no current is redistributed.

## Definitions and outputs

All thermal thresholds and reductions live in `src/config/AlertConfig.h`:

| Build definition | Default | Meaning |
| --- | ---: | --- |
| `PSU_TEMP_WARNING_C` | 65 | Highest input/output temperature at or above this triggers warning |
| `PSU_TEMP_WARNING_REDUCTION` | 15 | Reduce authorized current by this percentage |
| `PSU_TEMP_ERROR_C` | 70 | At or above this triggers error |
| `PSU_TEMP_ERROR_REDUCTION` | 70 | Reduce authorized current by this percentage |
| `PSU_TEMP_ERROR_MS` | 900000 | Known hot time at/above error threshold before latched OFF |
| `PSU_TEMP_STOP_C` | 75 | Strictly above this immediately latches OFF |
| `PSU_TEMP_CLEAR_C` | 60 | Both relevant readings must be strictly below this to clear reductions |
| `PSU_WARNING_PIN` | -1 | Optional independent active-HIGH warning output |
| `PSU_ERROR_PIN` | -1 | Optional independent active-HIGH error output |

`mega2560_serial_alerts` uses D28 WARNING, D29 ERROR and D30 PSU_ACTIVE with no display, wheel or
switch. `mega2560_touch_alerts` adds these pins to the touch/cutoff/fan/switch
example. Configured peripheral collisions are compile-time errors. Outputs are
Mega logic signals; use suitable input/driver circuitry for the connected device.
They initialize LOW before OUTPUT mode. Both can be HIGH for different active
conditions, and acknowledgement changes neither. In `PSU_UI_DEBUG` they are
logical states only: no physical GPIO, CAN or EEPROM access.

`PSU_ACTIVE_PIN` is defined separately in `ControlConfig.h` (default `-1`). It
indicates enable ON with at least one PSU permitted positive current, independently
of alert severity or acknowledgement. See [PSU_ACTIVE behavior and bench checks](CONTROL.md#optional-psu_active-output).

Every valid bound PSU's input and output temperature is monitored. Either hot
reading acts immediately even if its partner is unavailable. Both must be fresh,
finite and in the existing plausible range before positive current is permitted.
Loss of temperature coverage while the unit is still communicating/charging
stops and latches only that unit; complete CAN disappearance has its own dropout
classification. A sensor latch requires explicit retry even after data recovers.

The strongest triggered level persists until all relevant readings are below
the clear threshold. 20 A authorized becomes 17 A at warning or 6 A at error;
it never becomes 5.1 A by compounding reductions. Staged settings and authorized
allocations are unchanged. Applies, restores, new starts and volatile refreshes
calculate the effective online limit through the same function. Explicit PSU
nonvolatile/offline defaults remain separate configuration, not an operating
thermal limit.

Known readings below the error threshold reset the duration timer only when no
relevant hot/unknown unit remains. Unknown evidence pauses the timer; a fresh hot
peer keeps it counting. Wraparound of `millis()` is supported. A hot PSU that
vanishes cannot prove cooling. A **new explicit absence acceptance** releases
its incident contribution while it remains absent; a previously saved absence
permission cannot release a newly observed hot contribution. A returning unit
is checked again before inclusion.

Reductions use the existing identity-checked, paced scheduler before ordinary
work. Pending values are revalidated before sending. A superseded ACK is retained
as the last accepted value but cannot synchronize the new effective target.
Reductions during output-readiness qualification do not restart its deadline.
Rejected/timed-out protective writes use the existing bounded stop/fault fallback.
Cooling remains active while stopped, and thermal protection overrides manual
fan AUTO with maximum cooling when required.

## Notification behaviour

Tools → **Alerts** opens the list; select an incident for details. The overview's
`!N` badge (touch: `Tools!N`) includes active and cleared-but-unacknowledged incidents. Details
show severity, affected PSU/group or system, cause, action and recovery. Thermal
details add the highest temperature, effective percentage, remaining hot time
and persistence/resume progress. Acknowledged active incidents remain inspectable.

The backend uses fixed reason codes, flash strings and one primary incident per
stable bound slot plus fixed system sources. The most actionable condition wins
within a source; detailed diagnostics retain the other evidence. Duplicate reports
update the incident rather than allocating messages. Source identity changes
reset that slot's incident state.

Ordinary automatic popups are at least 30 seconds apart. The same episode rearms
only after 60 seconds of stable clearance. Severity escalation and a newly latched
shutdown can notify immediately; severity flapping within an already acknowledged
episode does not repeatedly reopen it. A displayed incident stays in place while
others accumulate. Acknowledge returns to the prior page, including an editor,
rather than forcing a modal queue. Incidents that accumulated behind the dismissed
notification remain in the list; a new incident or escalation can notify again.
An acknowledgement aimed at a replaced incident
first refreshes the detail and requires fresh input for the new incident.

Background apply/save work continues. Thermal OFF preserves the editor's unsaved
value; genuinely conflicting configuration changes still invalidate it. Touch
and wheel gestures already underway cannot acknowledge a new popup. Calibration
defers automatic popups. Display-only builds return to the overview after ten
seconds without acknowledging the incident.

Details also offer a condition-specific recovery shortcut: **Config**, **Details**,
**Diagnostics**, **Temps**, **Review save**, **Calibrate**, **Settings** or **Enable**.
Acknowledge remains the default wheel selection. Recovery navigation does not
acknowledge or authorize anything; decisions require their separate review.
See [recovery controls](LOCAL_UI.md#recovery-from-notifications-and-results).

## Catalogue

| Severity | Conditions |
| --- | --- |
| WARNING | Missing members awaiting a decision; accepted exclusions; returned PSUs awaiting inclusion |
| WARNING | Source availability unstable beyond settling; incomplete commissioning/legacy review; requested start without an authorized profile |
| WARNING | Configured display or touch calibration unavailable; save completed with an older snapshot |
| WARNING | Recent receive-buffer/transport overflow or discovery exhaustion; events clear after 30 seconds without further loss, even when diagnostic counters have saturated |
| ERROR | Active CAN/AC/readiness loss; conflicting/changed identities affecting an active PSU |
| ERROR | Rejected/timed-out settings or fan commands; reconciliation, readiness or stop verification failure; unexpected current at zero hold; latched cutoff |
| ERROR | CAN initialization, deployment mismatch or persistence failure |
| WARNING / ERROR | Thermal reductions, thermal shutdown and affected-unit temperature sensor latches |

Normal OFF, ordinary startup, busy responses and invalid syntax are not alerts.
A nonzero Huawei status word alone is not an error; raw words remain in diagnostics.

## Persistence, migration and limits

Schema **5** packs nine PSUs into a **211-byte** charger record, with two 256-byte journal slots,
512-byte budget and calibration records at **216–255 / 472–511**. Byte 27 packs
auto-resume in bit 0 and the thermal stop reason in bits 1–2. A latched record
cannot also authorize auto-resume. Schema 4 retains its thermal latch during migration;
schema 2/3 records load with no thermal latch;
schema-1 migration/review behaviour remains unchanged. Boot does not rewrite old
records merely to upgrade their version.

Automatic protection saves update OFF plus the marker in the **last saved
profile**, preserving unsaved RAM drafts. If no saved record exists, the compiled
template is used, not the unsaved commissioning session. A normal save already
underway finishes its reviewed snapshot first; the protection update follows.
Calibration and charger saves share one writer. Save success is reported only
after journal verification. A new OFF or renewed heat can revise an in-flight
clear-save; its stale completion never enables charging.

Once verified, the stop survives reboot, a held-ON boot switch, `load`, `defaults`
and reconciliation. Clearing the marker persists OFF; the new ON is live. Use
ordinary `save` afterward if that ON preference should survive another boot.
As with the existing journal, power loss during an unfinished write can recover
the previous complete record. Do not treat `saving=1` as durable protection.

Firmware predating schema 5 cannot read the new records. It may select an older
journal containing auto-resume ON or see no valid record. Export, inhibit charging
and recommission deliberately before downgrading; downgrading is not a stop-reset
procedure.

## Bench sequence

Start with the existing [serial commissioning](GETTING_STARTED.md) sequence and
a verified identity/group/profile. For a serial-only bench use
`mega2560_serial_alerts`; leave unused peripheral pins at their defaults.

1. `autoresume off`, `watch off`, `alerts`, `diag thermal`, `diag control`.
   Resolve actual faults. Check D28/D29 against active severities, not the badge
   count. `alerts ack all` must leave active GPIOs/protection unchanged.
2. Stage the suitable common voltage and a small group total, then `apply all`
   after `autoresume on`. Inspect `diag psu 1` for authorized/effective/acknowledged
   limits and `watch on` for measured output and input/output temperatures.
3. Exercise the policy first in `mega2560_touch_debug` (or another SIM profile):

   ```
   ui-debug temperature 65 30
   diag thermal
   alerts
   alerts ack all
   ui-debug temperature 30 70
   diag thermal
   ui-debug temperature 30 66
   ui-debug temperature 30 59
   diag thermal
   ui-debug temperature 76 30
   diag thermal
   ui-debug restart
   diag thermal
   ui-debug temperature 30 30
   autoresume on
   diag thermal
   ```

   Wait for each telemetry cycle and command/save completion. Expected factors
   are 85%, 30%, still 30%, then 100%. Above 75°C requests OFF and saves the
   marker. Restart reloads it; ON succeeds only after fresh cooling and a verified
   clear-save. SIM restart retains RAM storage; actual power loss does not.
   `ui-debug temperature unknown 30` exercises sensor loss while charging;
   restore `30 30`, then `thermal retry all`. These commands affect every simulated
   PSU and never inject fake temperatures into physical-CAN builds.
4. To verify real CAN reductions and physical GPIOs without deliberately heating
   equipment to production thresholds, use a clearly labelled temporary bench
   build with warning/error/stop/clear thresholds chosen around the actual observed
   temperatures and a short `PSU_TEMP_ERROR_MS`. Keep the load/current small.
   Preserve the ordering clear < warning < error < stop. Measure the current,
   inspect accepted limits and raw traffic, and confirm 85% / 30% rather than
   relying on the console alone. Check WARNING only, ERROR only and simultaneous
   outputs using a separate legitimate warning such as an accepted absent slot.
5. Above the temporary stop threshold (or after the hot timer), verify current
   stops, cooling continues, ERROR stays HIGH after acknowledgement, and
   `diag thermal` reaches `saved=1 failed=0`. Reboot the Mega; with ENABLE_IN fitted,
   repeat with the switch held ON. Neither may restart charging. `load` and
   `reconcile` must also leave the thermal latch intact. Cool the units, make a
   fresh ON action, wait for verification, and confirm the authorized shares
   return. A fresh OFF during that save must win.
6. Restore the production threshold build before normal operation. Recheck the
   deployment, profile, temperature coverage and saved enable preference.

Host tests establish scheduler, state, rendering and journal behaviour. They do
not establish real PSU command response, sensor accuracy, actual fan RPM, cutoff
operation, electrical GPIO levels or timing on a loaded physical CAN bus. Record
`diag bus` loop maximum, RX high-water and overflow events during the bench run.
