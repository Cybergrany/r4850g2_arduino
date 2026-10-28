# Startup, intermittent AC and unavailable groups

This firmware manages one parallel DC bus. A source group selects physical PSU
identities. Losing a source never redistributes its current share to the others.

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
   authorized common voltage (compiled voltage if none is authorized), then
   clear inherited standby. Enable OFF still permits awake zero holding.
5. Require fresh ready and low-current observations after wake. Allow at least
   **15 seconds**, or three poll periods if longer, for readiness. A fresh
   `0x0183` bit 29 reports `WAIT AC`; known AC absence waits without producing
   a startup timeout. Missing/stale status means AC unknown, not AC absent.
6. After readiness, reassert zero, fan mode and voltage, then observe readiness
   and low current again. Pre-ready ACKs alone cannot release charging.
7. Restore the authorized profile only with enable ON and all required
   participants reconciled. Drafts are not charging authorization. Excluded
   identities remain held at zero even when ready.

The readiness allowance does not relax the stop deadline. Observed nonzero
current still uses max(5 seconds, three poll periods). A failed INFO query sent
no setting and is not itself proof that a stop failed; freshness expiry can
classify an unavailable member. Rejected/unconfirmed stopping or unexpected
current while held invokes the bounded fault fallback and drops a fitted AC
permit. The cutoff fault latches: ON cannot release it. Correct the cause, then
use `reconcile` or **Retry startup**. Explicit retry also retries CAN initialization.

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
common voltage/group currents and save. `save` alone does not release an already
inhibited startup; probe/reboot is explicit.

## Accept an unavailable source

Initial absence approval waits **15 seconds after AC release**, plus a stable
group interval of **max(3 seconds, two poll periods)**. An observed transition
restarts that interval. Repeated changes for 30 seconds show `UNSTABLE`; this
never accepts anything automatically. Eight paced reconciliations can take
longer than 15 seconds. Booting, ambiguous, unbound or faulted units cannot be
waived as ordinary absence.

Serial announces `STARTUP REVIEW` when a settled absence or excluded return
needs attention. Inspect `diag startup` for the exact identities and reasons;
the group screen also flags returns that need inclusion.

For an unavailable, commissioned GROUP2:

```text
diag startup
startup accept GROUP2 remember
# Review selected identities, remaining participants and unchanged shares.
confirm yes
# Wait for "Configuration + decision saved".
diag startup
diag control
```

`remember` saves the **current complete configuration too**, including drafts,
authorized profile and effective enable preference. It activates the exception
only after journal verification and another check that the preview is current.
Enable ON may then resume remaining participants; OFF keeps zero holding.
Omit `remember` for this session only. A later ordinary `save` does not silently
persist session waivers.

The exception belongs to bound identities, not a group name or CAN address.
New missing members still block startup/restoration. Binding/membership edits
clear affected exceptions and current authorization. All units may be excluded;
the result explicitly says no active units, not a successful hardware apply.

## Returning groups and individual choices

Accepted identities remain excluded on the next boot even if present. Once
reconciled at zero, explicitly include them:

```text
startup include GROUP2
confirm yes
```

Inclusion lasts this session and preserves the saved absence policy. Losing
the unit again excludes it again. To remove an exception:

```text
startup require GROUP2 remember
confirm yes
```

Requiring a unit never silently includes an excluded unit; include it when
ready. All commands accept `all`, a group, or global slots such as `3`, `3-4`,
`1,3-4`. `include` cannot use `remember`. `confirm no` cancels. Confirmations
expire after 15 seconds and on configuration/enable, topology or availability
changes. A change during save may leave the reviewed snapshot saved but refuse
live continuation; serial/UI distinguish these outcomes.

Display: **Tools → Startup / availability**, or tap the startup status row.
Select a group with Prev/Next. Absent groups offer **Continue without group /
Wait for group / Choose individuals**. Returned groups offer **Include group /
Keep excluded / Choose individuals**. Review slots and allocation, optionally
select **Remember + save config**, then Confirm. Individual mode cycles accept,
include and require. Waiting/keeping excluded grants nothing. Wheel navigation
uses these same backend decisions and screens. Fault details and hard stop
remain accessible. Failed CAN initialization offers **Retry startup** directly.
Builds without the output display **AC cutoff not fitted** and have no hard-stop
button.

## Current intent, ACKs and refresh

For two configured members, `set current GROUP2 55` still means 27.50 A each.
`apply GROUP2` authorizes the latest shares for excluded members too, but sends
no positive current to them before inclusion. One participant still receives
27.50 A, not 55 A. With none participating, apply updates pending intent and
reports zero recipients/zero hardware successes. Voltage remains global.

At low priority, volatile V/I settings are refreshed approximately every
10 seconds per unit. This uses authorized voltage/current, or zero while held
or excluded, never staged drafts. Identity/epoch, fan and command-spacing checks
still apply. Zero/fan work and active user jobs take priority. A background INFO
timeout sends no setting; explicit setting faults remain visible. Persistent
defaults and fan mode are not periodically rewritten. An ACK is acceptance,
not setpoint readback, actual current delivery or fan RPM.

## Bench acceptance

Capture `diag startup`, `diag control`, raw CAN and physical CHG_ALLOW for:

- Cold AC/DC boot; battery-backed CAN before AC; an 8-second start; battery
  disconnection/reconnection with and without AC.
- Whole-source absence, partial return, a new missing unit, address negotiation
  and replacement identity. Verify excluded return stays at zero until included.
- Session/remembered decisions, reboot, interrupted save and a topology/enable
  change during confirmation/save.
- Zero/fan/wake failures, unexpected held current, cutoff/retry and normal OFF
  cooling. Check actual PSU communication-loss defaults and post-ready retention.

Host tests and simulations verify policy, not contactor operation or physical
current/voltage behavior. The timing margin accommodates the documented 3–8 s
turn-on delay. [Huawei R4850G2 manual, page 1](https://www.beyondlogic.org/pdf/Huawei_R4850G2_Rectifier_User_Manual_V1.4.pdf).
DC-powered CAN, automatic address negotiation and volatile-setting fallback are
documented observations with firmware/model caveats.
[R48xx protocol documentation](https://github.com/patagonaa/huawei-r48xx#can-protocol).
