# Startup, intermittent AC and unavailable groups

The controller manages stable PSU identities and per-unit voltage/current
targets. A source group selects physical PSUs; it is not a separate enable or
protection domain. Losing a source never redistributes its current share to the others.

## AC cutoff wiring and builds

`PSU_CHG_ALLOW_PIN` defaults to `-1`, disabling the physical output. Use:

```sh
pio run -e mega2560_serial_cutoff
# Serial only; CHG_ALLOW D27. No screen, wheel or switch.
pio run -e mega2560_touch_cutoff
# Touch + serial + ENABLE_IN D26 + fan PWM D6 + CHG_ALLOW D27.
```

CHG_ALLOW is **active HIGH** and controls the installed common AC cutoff. LOW
must remove **all PSU AC feeds**, including separate sources. Power the Mega
upstream. Use the cutoff's suitable isolated logic interface, not a coil driven
directly by GPIO. Hardware must hold LOW during reset, uploading and power loss.
Firmware writes LOW before configuring OUTPUT, as its first application action;
it cannot enforce a level during the bootloader or while unpowered.

`diag startup` shows the **commanded** permit, not contactor feedback. AC cutoff
is not DC/battery isolation. Battery voltage can keep CAN alive without AC;
a common cutoff cannot selectively isolate an excluded unit. Commission each
unit's offline defaults before leaving it attached to a battery. An unresponsive
PSU's output is unknown.

## Boot and reconciliation

The sequence runs cooperatively; CAN, serial, UI and cooling keep being serviced.

1. Assert LOW, validate the saved deployment and initialize CAN. Missing,
   invalid, legacy or wrong-deployment EEPROM, or failed CAN initialization,
   leaves a fitted cutoff inhibited. There is no automatic erase/retry loop.
2. With a trusted deployment and initialized CAN, allow AC after at least
   **2 seconds**. Builds without the output report `not fitted`.
3. Discover bound identities. Observed `30 01` / `30 02` negotiation suspends
   addressed writes and invalidates old routes. After negotiation settles,
   require two fresh INFO samples again. Addresses are neither assigned nor
   persisted by the Mega.
4. For each reachable bound unit, send zero online current, require its ACK
   and fresh measured current within ±0.5 A. Establish fan policy and the
   unit's authorized voltage (compiled voltage if none is authorized), then
   clear inherited standby. Enable OFF still permits awake zero holding.
5. Require fresh broadcast and low-current observations after wake. Allow at least
   **15 seconds**, or three poll periods if longer, for readiness. If a zero-limit
   PSU still reports not-ready after that allowance, fresh benign `0x0183`
   (only bits 9/10, or zero) can establish idle-at-zero instead. A fresh
   `0x0183` bit 29 reports `WAIT AC`; known AC absence waits without producing
   a startup timeout. Missing/stale status means AC unknown, not AC absent.
6. After readiness or verified idle-at-zero, reassert zero, fan mode and voltage,
   then observe ready/benign status and low current again. Pre-startup ACKs alone
   cannot release charging. Zero-limit readiness changes do not repeat wake.
7. Restore the authorized profile only with enable ON and all required
   participants reconciled. Drafts are not charging authorization. Excluded
   identities remain held at zero even when ready.

On a nonzero current write, require a matching ACK followed by at least three
ready broadcasts spanning two seconds, with no inter-frame gap over one second,
within the same 15-second/three-poll allowance. That deadline is fixed at the
first current transmission; readiness flapping resets only the qualification
interval. Only the requested current is sent, and it may already flow while
verification is pending. If qualification does not finish in time, stop and latch
a fault which survives link/address recovery until explicit `reconcile`;
do not restart indefinitely. [CONTROL.md](CONTROL.md) describes the idle-status
guard and diagnostics. AC changes, missing identities, stale measurements,
unknown status bits and unexpected held current retain their checks.

After reconciliation, readiness-triggered recovery has a session-wide bound:
two automatic zero/wake/restores per PSU by default; the third loss latches an
ERROR requiring explicit retry. A brief ready interval, enable switch change,
address reassignment or internal AC-release reconciliation does not renew that
budget. The bound also covers a zero-held unit whose ready indication drops
without satisfying the fresh benign-idle checks; ordinary benign zero-hold
changes consume no retry. Use the alert's **Details → Retry ALL…** review or serial `reconcile`
after inspecting the cause. Acknowledgement has no effect on the fault or budget.

The readiness allowance does not relax the stop deadline. Observed nonzero
current still uses max(5 seconds, three poll periods). A failed INFO query sent
no setting and is not itself proof that a stop failed; freshness expiry can
classify an unavailable member. Rejected/unconfirmed stopping or unexpected
current while held invokes the bounded fault fallback and drops a fitted AC
permit. The cutoff fault latches: ON cannot release it. Correct the cause, then
use `reconcile` or **Controller → Retry ALL PSUs**. Explicit retry also retries CAN initialization.

Normal `autoresume off` keeps AC allowed and holds zero for cooling.
`startup stop` immediately commands LOW and latches the cutoff; without a fitted
output it reports invalid instead of implying that a hardware stop occurred.

## First commissioning

Empty EEPROM deliberately leaves AC inhibited. On an unloaded bench:

```text
hello
autoresume off
diag startup
startup probe
confirm yes
discover
diag bus
```

Review the probe warning. Probe forces effective enable OFF, allows AC after
the inhibit interval and grants no charging profile. It saves nothing. A
wrong-deployment record must first be reviewed and replaced with the intended
`defaults`. Continue through [GETTING_STARTED.md](GETTING_STARTED.md): bind actual
identities, create groups, commission compatible offline defaults, apply the
scoped voltage/current targets and save. `save` alone does not release an already
inhibited startup; probe/reboot is explicit.

## Participation: include, exclude and remember

A participation decision changes intent, not current allocation. **Included**
means the PSU is required, even while absent. **Excluded** means it remains out
of service until explicitly included, including when AC/CAN returns. Available
excluded units remain at zero, with monitoring and cooling continuing. Shares
are never redistributed to other units.

Initial absence decisions wait **15 seconds after AC release**, plus a stable
group interval of **max(3 seconds, two poll periods)**. Observed transitions
restart that interval. Repeated changes for 30 seconds require review. Booting,
ambiguous, unbound or faulted units cannot be waived as ordinary absences.

Changing an available PSU's participation requires **controller OFF and confirmed
zero-current hold**. Use `autoresume off` or the UI's Controller → Stop first,
then inspect `diag control`. OFF is a request, not proof of zero current.
Accepting an ordinary settled absence is still possible while other units run.

For an unavailable, commissioned GROUP2:

```text
diag startup
startup accept GROUP2 remember
confirm yes
# Wait for ACTIVE; SAVED AND VERIFIED.
diag startup
diag control
```

`accept` is an absence-only exclusion alias: it selects only missing-CAN/no-AC
members, but does not skip a fault disguised as an absence. `exclude` expresses
exclusion for the entire requested selection. `include` and `require` both mean
included/required intent. They remove affected old absence permissions, so a
future loss cannot silently exclude a deliberately included unit again.

```text
autoresume off
diag control
# Wait for zero hold and settled availability.
startup exclude GROUP2 remember
confirm yes
# Later, deliberately include them again:
startup include GROUP2 remember
confirm yes
# Absent included units remain required; do not enable until ready.
diag startup
diag control
autoresume on
```

Every command accepts `all`, a group, or controller slots such as `3`, `3-4`,
`1,3-4`. Add `remember` to either include or exclude to save the **current full
configuration too**, including drafts, authorized targets and enable preference.
Staged targets remain unapplied. Remembered participation activates only after
journal verification and final revalidation. Omit `remember` for session-only
intent: later unrelated `save` or UI voltage/current saves do not promote it.

The UI defaults **Remember + save config** to checked; serial requires the
explicit `remember` word. Confirmations expire after 15 seconds and are
invalidated by configuration/enable, topology or availability changes. `confirm
no` cancels. A save can finish after conditions change: **saved snapshot, not
activated** is distinct from an active decision or a failed save. Inspect the
result and `diag startup`; do not assume that a stored record was activated.

Schema 6 and the EEPROM/profile layouts are unchanged. Old absence permissions
remain conservative and may request review on return until explicitly replaced.
New decisions use the existing explicit participation mask. Rebinding or group
membership changes clear affected intent. All units can be excluded; this
reports no active units and never claims successful hardware current delivery.

## Availability UI

Open **Tools → Startup**, tap the status row, or use an alert's **Config** shortcut.
The three-column overview preselects the affected group. Each card shows live
Ready/Absent counts and one of:

- **Settled**, green: stable observations, no outstanding availability decision.
- **Config Required**, red: a required absence, legacy return decision, fault or
  commissioning problem. A fault still needs its proper diagnostics/recovery.
- **Settling**, amber: startup, reconciliation or the observation interval.

Opening a group offers **Include**, **Exclude**, **Choose individuals**, and Back.
Bulk actions open a review without changing anything. Individuals use a four-row
scrolling list: **[x] excluded; [ ] included/required**. The wheel scrolls its
focused row into view; touch Up/Down moves one row per tap. CONFIRM opens the
review. Cancel from review preserves the draft and Remember choice; Cancel from
the checklist discards that draft. The review begins on Cancel, never Confirm.

Opening Config does not acknowledge an alert. Back from the overview returns to
the incident, and Acknowledge returns to the original screen. Interrupted drafts
survive unless configuration or identity changes invalidate them. Approved
exclusions do not continually request another decision merely because they return.

**Controller**, below the overview, provides Stop/zero-current hold, Enable,
startup/bus diagnostics, commissioning probe and Retry ALL. Enable, probe and
retry require Cancel-first reviews. Retry affects all PSUs and may restore
previously authorized targets when enable is ON. A fitted hard AC cutoff remains
accessible; boards without it show "AC cutoff not fitted". Binding and group
creation remain explicit serial commands.

## Profiles and thermal exclusions

Profile Store/Restore participation remains independent and defaults **No**.
Saving Yes captures current confirmed intent, including session-only choices;
unconfirmed checkbox drafts are not included. Loading Yes maps stable identities
and replaces old absence permissions for the restored selection. Loading No
leaves participation unchanged. The existing final controller save also stores
restored participation; it activates after verification and revalidation, without
an extra save. Failure or changed conditions are reported explicitly.

Explicitly confirming the exclusion of a currently missing hot unit may release
its thermal contribution; the review warns about this. Remembered exclusions and
profile loads never automatically release a new thermal incident. Returning
units are temperature-checked again. See [PROFILES.md](PROFILES.md) and
[ALERTS.md](ALERTS.md).

## Current intent, ACKs and refresh

For two configured members, `set current GROUP2 55` still means 27.50 A each.
`apply GROUP2` authorizes the latest shares for excluded members too, but sends
no positive current to them before inclusion. One participant still receives
27.50 A, not 55 A. With none participating, apply updates pending intent and
reports zero recipients/zero hardware successes. Voltage follows each PSU's scoped target.

At low priority, volatile V/I settings are refreshed approximately every
10 seconds per unit. This uses authorized voltage/current, or zero while held
or excluded, never staged drafts. Identity/epoch, fan and command-spacing checks
still apply. Zero/fan work and active user jobs take priority. A background INFO
timeout sends no setting; explicit setting faults remain visible. Persistent
defaults and fan mode are not periodically rewritten. An ACK is acceptance,
not setpoint readback, actual current delivery or fan RPM.

## Bench acceptance

Use a suitable low-current test profile and verify physical output separately.
Start with `watch off`, `raw off`, `autoresume off`, then `diag startup`,
`diag control`, `diag memory`, `diag ui` and `diag bus`.

1. With all units available, wait for zero hold/settling. Exclude one using the
   checklist with Remember checked. Confirm only after reviewing its identity.
   Verify **ACTIVE / SAVED AND VERIFIED**, `diag startup`, physical zero output,
   and continued cooling. Reboot: it must stay excluded, including when online.
2. Include it with Remember unchecked. Verify session-only status; make an
   unrelated UI V/A edit/save and reboot. The original remembered exclusion must
   return. Repeat inclusion with Remember checked: reboot must now require it.
3. Remove AC from a required unit. Check Config Required and the correct group
   shortcut. Explicitly accept its absence; confirm no redistribution. Bring it
   back and verify it stays held until included. Check that Ack changes no intent.
4. Save a profile with Store participation Yes, using a session-only exclusion.
   Change participation, then load it with Restore No: intent must remain unchanged.
   Load Yes while OFF/held: verify completion, reboot, and the captured intent.
   An unchecked absent member must stay required, never regain an older waiver.
5. Exercise nine checklist rows, top/bottom scrolling, more than three groups,
   Cancel/review/Remember, and alert interruption of an edit. Confirm Controller,
   V/A settings, profiles, faults, thermal recovery and calibration remain reachable.
6. Re-run `diag memory`, `diag ui`, `diag bus` while scrolling under real CAN
   traffic. Record the stack watermark, redraw timings, RX losses and command
   timeouts. Test cold AC/DC boot, delayed AC, battery-backed CAN, address changes,
   failed stopping and fitted hard-cutoff/retry separately.

Host tests and simulations verify policy, not contactor operation or physical
current/voltage behavior. The timing margin accommodates the documented 3–8 s
turn-on delay. [Huawei R4850G2 manual, page 1](https://www.beyondlogic.org/pdf/Huawei_R4850G2_Rectifier_User_Manual_V1.4.pdf).
DC-powered CAN, automatic address negotiation and volatile-setting fallback are
documented observations with firmware/model caveats.
[R48xx protocol documentation](https://github.com/patagonaa/huawei-r48xx#can-protocol).

## Operator alerts and temperature protection

See [ALERTS.md](ALERTS.md) for the shared warning/error catalogue, GPIO definitions,
thermal percentages, temperature-sensor retry, persistent thermal OFF and the
bench sequence. Serial adds `alerts`, `alerts ack ID|all`, `diag thermal` and
`thermal retry GROUP|slots|all`. Tools → Alerts exposes the same incidents.
Acknowledgement preserves protection and returns to the previous screen; it
does not accept unavailable members or resume charging. Thermal details have
separate Resume/Retry actions. The overview retains a thermal-stop banner after
acknowledgement and a Tools incident-count badge.
