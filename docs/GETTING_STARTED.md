# Serial commissioning and source groups

This controller serves one parallel DC bus. Groups describe input sources, not
separate packs. Each slot belongs to at most one group and has a globally unique
slot number: GROUP1 might contain slots 1 and 2, GROUP2 slots 3 and 4.

The examples use 54 V and two 50 A units to illustrate commands. Choose the
voltage, current limits, and offline behaviour appropriate to the actual pack
and installation. Group current means the sum of requested DC current limits;
it is not an AC input-current setting or a guarantee of measured output current.

## 1. Establish communication without a battery

With the AC-cutoff build, empty/untrusted EEPROM leaves CHG_ALLOW LOW. Start
with `diag startup`, then `startup probe` and review/`confirm yes` on the unloaded
bench. This enables discovery with charging enable OFF. See
[startup commissioning and unavailable sources](STARTUP.md).

Use a powered PSU, the correct enable/slot-detect wiring for it, and the MCP2515
CAN interface. A battery or load is unnecessary for discovery and telemetry.
Check CAN termination at both physical bus ends, H/L wiring, common reference,
125 kbit/s, and the module's crystal. This project's default crystal is 8 MHz.
`CAN ready` only means the MCP2515 initialized successfully.

Open the terminal at 115200 baud, 8-N-1, local echo disabled. Press **Ctrl-X**,
then enter:

```text
hello
watch off
raw off
autoresume off
help diagnostics
diag bus
discover
```

Discovery also starts automatically at boot. `discover` restarts a bounded,
read-only INFO sweep of addresses 1–127. It does not assign addresses or change
PSU settings. Independently, bound units undergo [awake zero-current reconciliation](CONTROL.md),
even with autoresume OFF. Allow roughly 30 seconds for a complete idle sweep; responsive
units are usually found sooner through their unsolicited traffic. Repeat:

```text
diag bus
```

A healthy two-unit bus eventually shows:

```text
configured slots=1 responding addresses=2 verified identities=2 broadcasting=2
addr=1 id=112233445501 verified=1 conflict=0 response-age-ms=... broadcast-age-ms=...
addr=7 id=112233445502 verified=1 conflict=0 response-age-ms=... broadcast-age-ms=...
```

**Those identities are fictional. Copy the identities actually reported by your
units.** Addresses need not be consecutive. `configured slots` is your saved
installation size, not a detected count. `responding addresses` counts fresh
CAN responders; only unique, verified identities establish which units they are.
A cached address row can remain visible after its unit disappears—check ages.

Both response and broadcast ages should repeatedly return to small values.
Identity verification requires two matching INFO samples separated by at least
100 ms, with no duplicate identity at another recent address. `verified=0`,
`conflict=1`, no broadcast, rising `tx-errors`, or rising `rx-drops` need attention.
See the diagnostic table below before applying anything.

For a raw transport check:

```text
raw on
```

Observe for several seconds, then `raw off` (or Ctrl-X). Telemetry replies use
IDs such as `1081407F`; unsolicited current/status frames use `1001117E` for
address 1. Zero output current is normal without a load. `raw on` observes
traffic; it does not enable PSU broadcasting or output. Keep it off during
routine use to reduce serial traffic and receive drops.

## 2. Identify units and create a group

Record the physical unit, input source, reported identity, and intended slot.
When necessary, identify one physical PSU at a time during commissioning; do
not guess the mapping from power-up order. You can request its E-Label using
its current CAN address:

```text
describe 1
describe 7
```

Wait for each description to finish before requesting another. Inspect model
and label information against the hardware. The binding ID is the **12 hex
characters from `diag bus`**, not necessarily the printed barcode. Rating is
explicit; the firmware does not infer model compatibility or current calibration
from that ID.

For two verified 50 A units (replace the example identities):

```text
count 2
bind 1 112233445501 50
bind 2 112233445502 50
group set GROUP1 1-2
config
diag group GROUP1
```

`bind` confirms that you have checked the unit's compatibility and rating.
Bindings are stable controller slots; temporary addresses are not saved.
Group names are case-insensitive, 1–8 letters/digits/underscores, starting with a
letter; `all`, `voltage`, `current`, and `bus` are reserved. Membership accepts
`1-2`, `1,2`, mixed selections such as `1,3-4`, or `all`. Groups cannot overlap.

Check for `members=1,2 responding=1,2 ready=1,2 missing=-`. Each member must have
fresh telemetry **and** unsolicited current/status broadcasts. Run `diag control`
and wait for `held-at-zero-awake` with
`zero-confirmed=1 awake=1` before the first apply. Normal OFF keeps units awake
with a zero current limit; it does not request hardware standby.

On some G2s `broadcast-ready=0` is expected at a zero limit. The controller can
establish `current-state=zero` from fresh low-current and benign PSU status
after the startup allowance; it must then stay in hold without repeated
zero/fan/voltage/wake cycles. A subsequent requested nonzero current starts a
bounded output-readiness check. See [CONTROL.md](CONTROL.md).

For a serial-only single-PSU bench test, keep using `mega2560_serial`. Before
reflashing, `autoresume off` then `save` retains the binding/group with boot
enable OFF; wait for `EEPROM OK`. After flashing, check `config`, `diag bus`
and `diag control`. With the configured load and voltage suitable for the test,
send each command separately:

```text
set voltage 54
set current GROUP1 1
autoresume on
preview all
apply all
```

Wait for accepted commands and `current-state=active`, then inspect
`diag control`, `telemetry GROUP1` and optionally `watch on`. A 1 A request is
a current **limit**; the load may draw less. If a previous authorized profile
exists, ON can restore it first: inspect it before enabling and let the restore
finish before applying new drafts. Finish with `watch off`, `autoresume off`,
and verify stable zero hold; `save` retains the final OFF preference.

### Test the fans without charging

Keep `autoresume off` and wait for the confirmed awake zero hold above. Then:

```text
fan all max
diag control
```

Wait for `fan requested=MAX acknowledged=MAX`. To test the PSU's internal
automatic fan control:

```text
fan all auto
diag control
```

Wait for `fan requested=AUTO acknowledged=AUTO`. No voltage/current edit or
`apply` is required. Replace `all` with a group name, a slot such as `1`, or a
selection such as `1,3-4`. AUTO testing requires OFF/confirmed zero hold; it
bypasses the Mega thresholds while active and clears when charging is enabled.

Finish with `fan all policy` to restore the Mega's normal temperature/current
rules. Its usual MAX/cooldown can apply before AUTO returns. Overrides are
temporary and are not included in `save`; `reconcile` also clears them.
See [fan test behavior and failures](CONTROL.md#manual-fan-tests-over-serial).

## 3. Stage and apply voltage and current

```text
set voltage 54
set current GROUP1 55
autoresume on
preview all
```

`autoresume on` is a live enable request. Without an authorized profile, units
remain held until the following apply. It does not commit drafts itself. If an
older authorized profile exists, ON can restore it immediately; inspect it and
let that restore finish before applying new drafts. Use an unloaded output for
the first bench test.

The preview should show the common 54 V and **27.50 A per member**. `set` and
`preview` send no settings. Oversized shares are rejected with the limiting
slot and configured maximum; the firmware does not silently clamp or rebalance.
A remainder of one centiamp is assigned deterministically to lower slot numbers.
The wire representation has its own small quantization, shown in decoded ACKs.

```text
apply all
```

Wait for `RESULT accepted slots=1,2 failed=- incomplete/skipped=-`. `OK` and
`QUEUED` mean the job was accepted, not that a PSU accepted its settings. Every
write first rechecks identity at the destination address. All voltage ACKs must
succeed before the current phase begins. Initial startup also requires
zero-current/fan/voltage/wake reconciliation across required participants. Configuration
changes and `save` are blocked while a job runs; `autoresume off` can always
interrupt it and request zero-current holding.

Then inspect:

```text
diag group GROUP1
telemetry GROUP1
```

Expect ready members, `voltage-acked=1`, and `last=accepted`. Compare measured
voltage/current with the installation's expectations; current can be near zero
without load. A setting ACK does not prove actual current delivery. The PSU's
own capability, temperature, input supply, voltage, and parallel load sharing
can constrain measured current.

For ongoing observation:

```text
watch on
```

`watch off` stops repeated telemetry. No battery is needed to prove that each
unit is identified, responding, and broadcasting. Loaded current calibration
and power sharing need a separate controlled bench test.

## 4. Add another source group and make routine changes

For four units, commission slots 3 and 4 using their own identities and ratings,
then define the second group:

```text
count 4
bind 3 112233445503 50
bind 4 112233445504 50
group set GROUP2 3-4
set current GROUP2 30
preview all
apply all
```

Wait for all four final results. GROUP1 retains its staged 55 A total; GROUP2
gets 15 A per member. There is no hidden shared-current ceiling.

Once common voltage has been applied across the installation, a group current
change affects only that group's members:

```text
set current GROUP1 40
preview GROUP1
apply GROUP1
```

This sends 20 A to each GROUP1 member. Other groups keep their settings.
`apply GROUP1` never applies a pending global voltage draft. To change voltage:

```text
set voltage 55
preview voltage
apply voltage
```

This requires **all participating slots** to be identified and ready; there is
no one-operation partial voltage override. Explicitly accepted absences are
excluded from transmission but retain the new common-voltage intent for approved
return. It preserves authorized current limits. If the voltage
operation fails partway, inspect the per-unit result and complete a successful
`apply voltage`/`apply all` before issuing another group current apply. There
is no atomic CAN transaction and no pretend rollback of already accepted writes.

Editing membership preserves the group's requested total but invalidates current
authorization for affected members. Nothing is redistributed live. Inspect the
new preview and apply explicitly. To move a slot between groups, remove it from
the old membership first. `group delete NAME` removes membership; it does not
turn physical outputs off.

## 5. Save and choose restart behaviour

After a completed apply:

```text
save
```

The first response is `EEPROM save started`. Wait for **`EEPROM OK`** before
powering off; it means the journal record was committed and verified. CAN,
display and serial continue running during the save. If you edit configuration
while it runs, the console reports that newer RAM changes remain unsaved.
An overlapping save, load, defaults or explicit apply returns Busy.

Controller EEPROM stores identities, ratings, groups, drafts, and a separate
**authorized operating profile**. Saving new drafts does not authorize them:
if a known unit reconnects, it restores its last authorized voltage/current,
not a later staged edit. `config` and `diag group` distinguish these values.

During the same controller session, returning authorized units reconcile through
zero current, checked fan mode, common voltage and verified wake before
restoring their shares. New/unaccepted missing members block charging-current release;
unaffected healthy PSUs keep their shares. No current is increased to compensate for absence. Control
failures latch visibly; inspect `diag control`, fix the cause, and use `reconcile`
to request a new attempt. Reconciliation never applies later staged drafts.

For an intentionally unavailable source, use `startup accept GROUP2 remember`,
review the identities/allocation and `confirm yes`. Wait for verified save
completion. Returning excluded members require `startup include GROUP2` and
confirmation. Use slot selections for partial groups. See [STARTUP.md](STARTUP.md)
for session-only choices, removing exceptions, settling and the UI equivalent.

Enable is OFF by default. **`autoresume on/off` now controls live operation**, as
well as the preference saved for next boot. `autoresume off` requests zero current
immediately, even during an apply; wait for `ZERO CURRENT HOLD / AWAKE` and
`zero-confirmed=1 awake=1`, or inspect the reported failure. Cooling continues.
Hardware standby is reserved for a latched fault fallback if holding fails.
`save` persists the effective preference. A saved ON preference
allows the authorized profile to start only after successful reconciliation.

Optional `ENABLE_IN` uses a debounced latching switch with latest-event-wins
semantics relative to serial commands. At boot only, saved ON or switch ON
permits startup. `load`/`defaults` initiate held reconciliation without replaying
a profile; enable explicitly afterwards. See [control behavior, wiring, fan
thresholds and persistence](CONTROL.md).

The authorized profile records explicitly accepted **apply intent**; a failed
or interrupted CAN operation may not have reached every PSU. Check final
results before saving/enabling reboot restoration. Save after a successful
apply if that intent should survive a power cycle. A reboot can interrupt an
unfinished operation; hardware may retain settings accepted before it.

### PSU offline defaults are separate

The PSU can start or fall back to its own nonvolatile defaults when the
controller is unavailable. They are independent of controller `save`.
Choose a common offline voltage and explicit group totals:

```text
set offline-voltage 54
set offline-current GROUP1 10
set offline-current GROUP2 10
config
offline all
```

Omit GROUP2 if it does not exist. `offline all` writes only PSU nonvolatile
voltage/current defaults across the complete installation, with identity and
ACK checks. It never happens automatically on discovery, recovery, or boot.
Wait for the final result and verify the chosen fallback on hardware, then
`save` to retain those draft values in controller EEPROM too.

Commission a new unit's defaults before attaching it to a live parallel pack.
An unbound/replacement PSU can already energize its output using its own saved
settings. The optional CHG_ALLOW pin commands a common AC cutoff without contactor
feedback or DC isolation. Zero current, missing CAN, or removal from a group is
not electrical isolation. See [STARTUP.md](STARTUP.md).

## Missing members and replacements

If slot 2 disappears, `apply GROUP1` is blocked. First inspect:

```text
diag bus
diag group GROUP1
```

When changing the current of already reconciled online members is intentional
(the override cannot bypass initial startup):

```text
apply GROUP1 partial
```

Read the plan and the **ARE YOU SURE?** warning, then within 15 seconds:

```text
confirm yes
```

`confirm no` or Ctrl-X cancels the pending confirmation. Configuration changes,
address/identity changes, expired confirmation, or newly changed recipients
invalidate it; request a new plan. A not-ready, unverified, unbound, conflicting,
or voltage-unsynchronized member cannot be overridden as merely missing.

If 55 A was staged for two members, the available member still gets **27.50 A**.
The missing member receives nothing; it may still be producing power. If the
request changed since the last full apply, the missing unit retains its previous
authorized share for recovery. Diagnostics show the resulting authorized total;
the new group request is not fully configured until an explicit complete apply.
There is no inferred total-output guarantee and no automatic redistribution.

A known identity appearing at a new address is rediscovered automatically. A
brief conflict is possible while the old address's 15-second identity lease
expires. Persistent duplicate identities or alternating identities at one
address require wiring/physical investigation, not manual address reassignment.

A different unit at an old address is a replacement. Inspect its model/rating,
input source, defaults, identity, and physical slot, then explicitly adopt it:

```text
adopt 2 112233445599 50
preview all
apply all
```

Use the new actual identity. `adopt` invalidates that slot's previous current
and voltage authorization and common-voltage synchronization; it sends no setting itself.
Saving at this point cannot give the replacement the old unit's boot permission.
An explicit complete voltage/all apply is required before automatic restoration
can proceed for the installation again.
Wait for successful results before `save`. Discovery never silently adopts an
unknown unit or guesses its rating.

`unbind N` explicitly releases a retired slot's identity and authorization,
including bindings retained outside the current active count. Use it before
moving an existing identity to another slot. Active unbound slots block applies
until recommissioned. Unbinding does not switch the old unit's output off.

## Diagnostic meanings

| Message or field | Meaning / next step |
| --- | --- |
| `CAN ready` / `CAN init=1` | Adapter initialized; inspect responding/broadcasting counts next |
| `tx-errors` rising | CAN transmission failed: check power, termination, crystal, bitrate, wiring |
| `rx-drops` rising | Receive queue overflow: stop raw/watch and excessive command traffic; inspect bus load |
| `discovery-overflow` | More than the bounded registry can retain; inspect unexpected devices/addresses |
| `unbound` | Use `diag bus`, inspect hardware, then `bind` |
| `no source group` | Assign the slot with `group set` |
| `identity needs two fresh INFO replies` | Wait for INFO corroboration; unsupported identity responses block control |
| `identity at multiple live addresses` | Ambiguous routing; wait for old lease expiry or isolate conflicting units |
| `missing; output state unknown` | No fresh matching device; inspect hardware or explicitly choose partial current apply |
| `waiting for fresh telemetry AND current broadcast` | A response alone is insufficient; `poll`, inspect raw traffic and PSU state |
| `PSU reports not ready` | Check input power/protection and raw alarm/status; no speculative automatic fault reset |
| `common voltage not synchronized` | Complete `apply voltage` or `apply all` with required participants ready; review accepted absences with `diag startup` |
| `identity query timeout; setting not sent` | Fresh pre-write identity check failed; inspect INFO response traffic |
| `ACK timeout` | Setting delivery/outcome uncertain; inspect PSU and retry explicitly |
| `PSU rejected` | PSU rejected the value; inspect model, rating, range, decoded ACK |
| `identity/address/readiness changed` | Job's original recipient state changed; inspect and create a fresh apply |
| `incomplete/skipped` in final result | Some requested work did not finish; previously accepted writes remain in effect |
| `incomplete; current phase not sent` | This unit accepted voltage, but the operation stopped before its current setting |
| `alarm-raw` | Last raw `0x0183` status; `unknown` means absent, not no alarms. No complete alarm decoder |

Freshness is `max(5 seconds, 3 × poll interval)`; identity expires after 15 seconds
without a matching INFO reply. `diag psu N` shows response, data and broadcast
ages, last command outcome, staged/authorized shares, and voltage ACK state.
The ready interpretation is based on observed R48xx traffic, not a universal
vendor safety status. See [protocol evidence and limits](PARITY.md).

## Upgrading and moving installations

The serial command syntax intentionally changes from the earlier multi-pack
firmware. Per-unit voltages, manual address mappings, enabled-slot skipping,
`bootapply`, `modifySingle`/`modifyRange`, and the combined `persist` command are
removed. Use global voltage, named groups, identity bindings, `autoresume`, and
explicit online/offline operations. After serial commissioning, the
[ILI9341/wheel UI](LOCAL_UI.md) monitors groups and edits global voltage/group
current through the same backend. A successful local apply also saves controller
EEPROM; serial `apply` and `save` remain separate operations.

EEPROM schema 3 uses two 256-byte journal slots, with a 216-byte record at eight
PSUs. The remainder of Mega EEPROM is untouched. A save commits last with CRC
verification; interrupted saves preserve the previous complete record.

Schema 2 loads with no accepted-absence exceptions. Schema 3 uses reserved byte
28 for remembered identity-bound exceptions; touch calibration retains its two
40-byte journal tails. Older firmware does not understand schema 3 and may load
an older record or none, so export and recommission before downgrading.

Schema 1 records are never rewritten just by boot/load:

- Uniform enabled units with matching online/offline voltages and per-unit
  currents migrate to RAM GROUP1, retaining totals and ratings. Bind their
  identities, review/apply, then save. Auto-resume is disabled.
- Different per-unit values or disabled units require review. `legacy` prints
  the preserved values; RAM is left unchanged. Export that output before
  constructing a parallel configuration. `save` explicitly creates its new
  journal record; subsequent saves can overwrite the old legacy backup.

For a different physical pack/installation, change
[`deployment::id`](../src/config/Deployment.h) and any compiled template values.
If saved EEPROM belongs elsewhere, boot reports `BLOCKED wrong deployment`.
Inspect/export `config` (and `legacy` if relevant), then use `defaults` to select
the new template. Discover and bind the actual units, create/review groups,
apply, and save. Merely changing IDs or loading a profile does not prove that
physical connections and existing PSU nonvolatile defaults match it.

## Serial command reference

Commands are lowercase; group names are case-insensitive. Slot numbers here are
1-based. Backend API slots and group indexes are 0-based.

| Command | Purpose |
| --- | --- |
| `hello`, `help`, `help diagnostics` | Console recovery greeting and help |
| `discover`, `diag bus` | Read-only sweep and responder/address/identity diagnostics |
| `diag group NAME`, `diag psu N` | Group/member readiness, freshness, requested/authorized settings, failures |
| `config`, `groups` | Deployment, global drafts, operating voltage, groups, bindings, ratings |
| `count N` | Set configured slot count, 1–8; cannot shrink through existing membership |
| `bind N ID RATING`, `adopt N ID RATING` | Explicit identity binding/replacement; rating in amps |
| `unbind N` | Release an active or retained inactive binding; no physical output-disable command |
| `group set NAME MEMBERS`, `group delete NAME` | Define disjoint source-group membership |
| `set voltage V`, `set offline-voltage V` | Global drafts (online 41.5–58.5 V; offline 48–58.5 V) |
| `set current NAME A`, `set offline-current NAME A` | Group total drafts; split against configured members/ratings |
| `preview all`, `preview voltage`, `preview NAME [partial]` | Inspect proposed recipients, shares and blockers without applying |
| `apply all`, `apply voltage`, `apply NAME [partial]` | Queue global or group online operations |
| `confirm yes`, `confirm no` | Resolve the current missing-member confirmation |
| `offline all` | Explicit PSU nonvolatile voltage/current writes |
| `status [all\|NAME]`, `telemetry [all\|NAME]` | Member summary or all decoded metrics and approximate Ah |
| `watch on/off`, `raw on/off`, `echo on/off` | Console output preferences (not persistent) |
| `poll all`, `poll NAME` | Queue paced data requests for known members; inspect subsequent replies/errors |
| `describe ADDRESS` | Queue E-Label request by current address, including unbound devices |
| `interval MS` | Target telemetry request interval, 1000–10000 ms; identity refresh is independent |
| `reset-ah` | Reset session estimates for all discovered devices |
| `save`, `load`, `defaults`, `legacy` | Controller EEPROM/template/migration operations |
| `autoresume on/off` | Live enable/zero-current hold request and boot preference; latest switch edge/serial wins; save explicitly |
| `diag control` | Enable source/switch, reconciliation phases, zero/awake/fan ACKs and faults |
| `reconcile` | Cancel an apply, request zero current and retry latched control faults |
| `fan all/GROUP/slots auto/max/policy` | Immediate fan override; AUTO requires OFF/confirmed zero hold; policy restores normal rules |

## Checking scheduling under load

Run `diag bus` before and after exercising page changes, wheel edits, `watch on`,
brief `raw on`, `poll all`, and a controller `save`, with all intended PSUs on:

- `loop-max-us`: longest complete application loop since boot (startup excluded).
- `rx-high-water=N/15`: largest observed software receive queue occupancy.
- `rx-drops`: software receive overflow, saturating at 255.
- `hw-overflow-events`: observed MCP2515 RX overflow flags, saturating at 65535;
  this counts latched events, not the precise number of lost frames.
- `trace-drops`: console trace/description entries omitted under output load;
  these do not mean the controller missed those CAN frames or their ACKs.
- `output-overruns`: unexpected console formatting overflow; should remain zero.
- `queued-read-failures`: manual poll/description transmission failures or poll
  members that disappeared before dispatch. Serial also prints an error.

All counters reset on reboot. Expect no growing CAN receive-drop/overflow counts
on a healthy installation. A high queue watermark or loop maximum helps locate
timing pressure; use a logic analyser for actual SPI/interrupt timing. Runtime
metrics supplement the [bench checks](VALIDATION.md); they are not a hardware
timing guarantee. `poll`/`describe` saying queued is not proof of a PSU response.
