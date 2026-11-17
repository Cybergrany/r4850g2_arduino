# Low-current startup synchronization

Multi-PSU starts prime at **at most 1 A per PSU**, wait for sustained measured
current, then send the authorized limits as a paced current-only batch. This
reduces the difference between units whose hardware takes different amounts of
time to begin delivering current. It does not make the CAN writes simultaneous
or guarantee equal physical ramp rates.

## When it runs

The same backend covers ENABLE_IN, serial/UI enable, boot restoration, profile
loads and ordinary applies. At least two included recipients must be moving
from confirmed zero to a positive encoded current. Single-unit starts and
ordinary positive-to-positive changes remain direct. Excluded units, zero
requests and values that encode to zero do not join the synchronization set.
An already-active unit stays at or below its previous accepted limit while
zero-starting peers prime; increases wait for the final batch. Background
refreshes retain that exact encoded limit without cumulative rounding.

The accepted recipients, identities and targets are frozen for the operation.
Conflicting edits/applies report Busy; they are not queued for later execution.
OFF, diagnostics and explicit controller reconciliation remain available.
Targets in RAM and EEPROM remain the operator's requested/authorized values;
priming is a temporary command limit. Diagnostics distinguish authorized,
effective and acknowledged current.

1. Apply each requested voltage, or reuse a valid authorized-voltage ACK during
   restoration. Identity/AC epoch changes invalidate that proof.
2. Send each new participant `min(1 A, thermally limited authorized current)`.
   Every write still has its own fresh identity check and matched ACK.
3. Require fresh output-current measurements within **±30% of the encoded and
   decoded priming limit**, continuously for **2 seconds per PSU**. Cached data,
   ACKs alone, or output voltage reaching its target cannot qualify it.
4. After all participants qualify and the normal release/readiness guards hold,
   dispatch final currents with **25 ms minimum setting-to-setting spacing**.
   There is still only one outstanding job ACK. No extra voltage sweep precedes
   this batch. Normal voltage/fan/control pacing remains unchanged.
5. Only final ACKs complete the apply. UI/profile automatic saves retain their
   existing completion owner and wait for this result.

The normal output-readiness deadline is separate and unchanged. The sync wait
starts with the first priming write. After **five minutes** it raises one
`Current sync taking too long` WARNING and **keeps the priming limits**, while
continuing to check measurements. A healthy unit at constant voltage, with no
load, or limited by another source may never draw the required current. Choose
whether to wait, stop, or explicitly bypass synchronization.

A fault, rejection, timeout, identity change or missing recipient blocks this
attempt's release. Existing bounded stopping/protection handles the affected
PSU. Healthy primed peers remain capped; peers not yet primed remain at zero.
A spontaneous return does not release a blocked attempt. Fix the cause and use
an explicit retry, which reconciles all units at zero again. There is no silent
exclusion or redistribution. Thermal limiting and its protective corrections
continue during every stage; priming refreshes cannot leak the full target.

## UI and serial

Group cards show amber **SYNC n/m** on the last line, with faults still visible
above it. The apply screen offers **Config** while waiting. Long-press a group,
open the second Configuration menu page and select **Bypass current sync**.
The same screen is linked from the timeout notification. It has live sync
status, **Stop / zero**, and a **Retry ALL...** review when a fault blocks sync.

The checkbox is **global and session-only**, initially OFF at every reboot.
Turning bypass ON opens a Cancel-first review because it can immediately
release authorized currents. Turning it OFF affects future starts; it does not
restart a completed/actively releasing batch. Acknowledging the warning neither
sets bypass nor changes any protection. Bypass never overrides OFF, temperature,
identity, participation, voltage acknowledgement, readiness or fault guards.
It is not part of controller records or named profiles. Console reconnects,
ordinary saves and loads do not reset the session choice.

```text
diag sync
sync bypass on
sync bypass on confirm
sync bypass off
```

The second `on confirm` command must arrive within 15 seconds of the review.
A changed operation, phase, topology, configuration, participation or newer OFF
invalidates it. Use `sync bypass on` again to review the current state. `diag sync`
also prints the ordinary per-PSU diagnostics. `diag control` includes a sync
summary; state/qualification changes emit bounded serial progress messages.

Defaults live in `src/config/CurrentSyncConfig.h`:

| Build definition | Default | Meaning |
|---|---:|---|
| `PSU_CURRENT_SYNC_PRIME_CA` | 100 | Maximum priming current in hundredths of an amp; positive and at most 100 |
| `PSU_CURRENT_SYNC_TOLERANCE_PERCENT` | 30 | Symmetric tolerance around the decoded CAN current limit |
| `PSU_CURRENT_SYNC_STABLE_MS` | 2000 | Fresh in-band measurement duration |
| `PSU_CURRENT_SYNC_TIMEOUT_MS` | 300000 | Warning deadline; never automatic release |
| `PSU_CURRENT_BATCH_GAP_MS` | 25 | Online job setting spacing; supported range 25–250 ms |

## Bench comparison

Use the existing bindings, participation and safe test voltage/current. For the
nine-unit comparison, flash `mega2560_wheel_profile` and start logging before
boot. Use the normal switch for ON/OFF if fitted. Serial OFF remains available.

```text
watch off
raw off
autoresume off
diag control
set current all 3
preview all current
apply all current
save
busprofile on
busprofile mark 300
autoresume on
```

The OFF apply prepares 3 A **per PSU**; it does not deliver current. Wait for
zero hold before ON. All intended units should already have an authorized
voltage profile. During startup, capture `diag sync` and check each group's
SYNC count. Allow the logger to run without repeatedly issuing large reports.
Once all groups show the intended current, record:

```text
busprofile mark 302
diag sync
diag control
diag bus
diag ui
diag memory
```

The capture adds `current-sync` BUS events without changing BUS v1 CSV columns:
value is the participant mask; detail 0 ends/cancels, 1 arms, 3 primes, 4 waits,
5 releases and 6 enables bypass. Unit rows retain measured current and the last
acknowledged raw limit, so priming acceptance, physical ramp and final dispatch
can be compared separately. Setting/ACK timestamps are controller observations,
not wire timestamps. Aim for final-current write spread below one second under
normal nine-unit traffic; verify receive losses, command timeouts and display
responsiveness before claiming hardware performance.

Then turn OFF and verify prompt zero hold. Repeat with a participant that cannot
draw priming current: it must stay capped, warn after five minutes, and keep its
incident ID. Acknowledge, confirm the cap remains, then review/cancel bypass.
Finally confirm bypass, verify only otherwise-eligible units can release, turn
OFF, and reboot: bypass must read 0 again. A fault needs retry rather than bypass.
Capture `diag fault` before retrying a failed attempt.

Software tests and simulated dispatch timing do not establish physical PSU
response, safe load wiring, or measured stack margin.
