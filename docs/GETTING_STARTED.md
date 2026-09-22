# Serial commissioning and source groups

This controller serves one parallel DC bus. Groups describe input sources, not
separate packs. Each slot belongs to at most one group and has a globally unique
slot number: GROUP1 might contain slots 1 and 2, GROUP2 slots 3 and 4.

The examples use 54 V and two 50 A units to illustrate commands. Choose the
voltage, current limits, and offline behaviour appropriate to the actual pack
and installation. Group current means the sum of requested DC current limits;
it is not an AC input-current setting or a guarantee of measured output current.

## 1. Establish communication without a battery

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
help diagnostics
diag bus
discover
```

Discovery also starts automatically at boot. `discover` restarts a bounded,
read-only INFO sweep of addresses 1–127. It does not assign addresses or change
PSU settings. Allow roughly 30 seconds for a complete idle sweep; responsive
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
fresh telemetry **and** unsolicited current/status broadcasts and report ready.
`voltage-acked=0` is expected before the first online apply.

## 3. Stage and apply voltage and current

```text
set voltage 54
set current GROUP1 55
preview all
```

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
succeed before the current phase begins. Configuration changes and `save` are
blocked while a job runs.

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

This requires **all configured slots** to be identified and ready; no partial
voltage override exists. It preserves authorized current limits. If the voltage
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

Controller EEPROM stores identities, ratings, groups, drafts, and a separate
**authorized operating profile**. Saving new drafts does not authorize them:
if a known unit reconnects, it restores its last authorized voltage/current,
not a later staged edit. `config` and `diag group` distinguish these values.

During the same controller session, a previously authorized unit may restore
once its identity, ready status, telemetry, and broadcasting are verified again.
Recovery waits until the whole configured installation is ready, then sends
common voltage before authorized currents. A failed recovery is reported and
is not retried repeatedly while the same readiness/identity state persists.
An explicit apply or a genuine loss/recovery transition can start another attempt.
Unaffected PSUs keep running; their current is not increased to compensate.
Serial announces `RESTORE authorized profile` with recipient slots and labels
the final result as `operation=restore-authorized-profile`.

Controller reboot restoration is **off by default**. To opt in:

```text
autoresume on
save
```

Now boot can restore the saved authorized profile after all required units pass
verification. A saved draft still remains a draft. `autoresume off` then `save`
disables restoration after the next controller reboot. `load` changes RAM only
and never starts a replay, even if the record enables boot restoration.

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
settings. This firmware has no contactor or hardware output-disable interlock;
zero current, missing CAN, or removal from a group is not electrical isolation.

## Missing members and replacements

If slot 2 disappears, `apply GROUP1` is blocked. First inspect:

```text
diag bus
diag group GROUP1
```

When operating only the reachable members is intentional:

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
| `common voltage not synchronized` | Complete `apply voltage` or `apply all` with all configured units ready |
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

EEPROM schema 2 uses two 256-byte journal slots, with a 216-byte record at eight
PSUs. The remainder of Mega EEPROM is untouched. A save commits last with CRC
verification; interrupted saves preserve the previous complete record.

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
| `poll all`, `poll NAME` | Request fresh data from known members |
| `describe ADDRESS` | Stream E-Label by current CAN address, including unbound devices |
| `interval MS` | Target telemetry request interval, 1000–10000 ms; identity refresh is independent |
| `reset-ah` | Reset session estimates for all discovered devices |
| `save`, `load`, `defaults`, `legacy` | Controller EEPROM/template/migration operations |
| `autoresume on/off` | Stage policy for restoration after controller reboot; save explicitly |
