# Scoped settings and configuration profiles

Voltage and current targets belong to individual, identity-bound PSUs. Groups
organize those units; they do not create separate enable or protection domains.
Global/group/individual updates replace only the selected fields and members.
The most recent accepted update wins, without a stored hierarchy of overrides.
For physically paralleled DC outputs, configure the same voltage on every unit.
Different voltage targets are intended for separate outputs.

## Staging and applying

| Scope | Voltage | Current |
| --- | --- | --- |
| `all` | Same voltage on every configured PSU | **Same total in each group**, split across that group's configured members |
| `GROUP1` | Same voltage on that group's members | Replace individual allocations with deterministic equal shares |
| `psu:3` | Controller slot 3 only | Slot 3's individual current limit |

For example, with two units in GROUP1 and three in GROUP2, `set current all 30`
stages 15 A per GROUP1 member and 10 A per GROUP2 member: 60 A across the installation.
It does not request 30 A across the entire installation. Global current is bounded
by the least-capable group's equal split. Serial rejects invalid changes atomically;
UI adjustments stop at the same centrally defined limits. No silent serial clamping.

```text
set voltage all 54
set current all 20
set voltage GROUP2 56
set current psu:3 5
preview all
apply all
save
```

A group with custom individual currents shows their sum and **CUSTOM SHARES**.
Changing membership preserves each unit's configured current; it neither
redistributes current nor sends setting writes. Changing a group's current later
replaces custom shares. Voltage/current fields are independent:

```text
preview GROUP1 voltage
apply GROUP1 voltage
preview psu:3 current
apply psu:3 current
```

An unqualified `apply GROUP1` now applies **both** that group's voltage and current.
Use `apply GROUP1 current` for the old current-only operation. `set voltage 54`
and `apply voltage` remain global-voltage aliases. `apply GROUP1 partial` remains
the explicit, confirmed current-only exception for missing members; it never
redistributes their shares or accepts an absence for future operations.

While OFF, apply reports **Prepared for next ON**. It authorizes the selected
targets while maintaining zero-current hold. It does not enable, clear faults,
accept missing members or bypass temperature checks. While ON, the scheduler
verifies identity, voltage acknowledgements, readiness and effective current.
Busy operations return Busy. Firmware current limits are allocations, not a
promise that the PSU physically delivers that current.

`diag psu 3` shows configured and authorized V/A, thermal effective current,
acknowledged current, identity/address and control state. `preview` reports each
selected target and precise blocking conditions. Serial staging/apply require an
explicit `save`; ordinary UI edits save after a successful apply/preparation.

## Local controls

Long-press a group to open:

1. Configure global
2. Configure group
3. Configure individual
4. Load Config
5. Save Config

Selectors are paginated. Individual rows show controller slot, current CAN
address or OFF, identity suffix and group. Global current is labelled **TOTAL
PER GROUP**. Mixed voltages/global group totals show **MIXED** and the range.
Entering an editor starts at the lowest value but changes nothing until confirmed.
Global changes show the override warning and a field-specific review with Cancel
selected initially. The same editor handles each scope.

Save Config selects one of eight slots, offers **Store PSU participation?**
(default No), and an optional name editor. Names use up to 12 uppercase letters,
digits, spaces, hyphens or underscores; unnamed slots default to `PROFILE 1` etc.
Turn to select a character, click to adjust it, then click again to move the cursor.
Done and Cancel follow the twelve character positions. Touch has cursor and +/-
controls. Review shows the voltage range, total allocated current and participation
choice. Overwrites require confirmation; Cancel starts selected.

Load Config selects a slot, offers **Restore participation?** (default No), then
reviews the complete installation. This checkbox is unavailable when the profile
does not contain participation. Profiles retain configured targets, including
unapplied edits. Saving a profile does not apply those targets or save unrelated
controller preferences. Loading authorizes them through the normal backend and
saves the resulting boot configuration after success. OFF loads prepare the next
explicit ON; ON loads apply immediately through the normal scheduler.

## Serial profiles

```text
profiles
profile save 1 BENCH LOW
profile inclusion yes
confirm yes
profile show 1
profile load 1
profile inclusion no
confirm yes
```

Every save/load starts a review; `confirm no`, Ctrl-X or an expired confirmation
cancels it. The optional `profile inclusion yes|no` updates that pending review.
Omitting it means No. Wait for verified EEPROM completion. The review expires
in 15 seconds; edits, rebinding, topology/availability changes or an overwritten
profile invalidate it. Reissue the command to review current state.

`profile show` lists every saved identity, even when the current configured count
differs. Its participation mask is labelled by saved entry number, not current
controller slot; load reviews map that choice to the current identities.

Profiles follow the **exact stable identity set**, not CAN addresses or slot
numbers. Slot reorderings are mapped by identity. Added, removed or replacement
identities, changed deployment, or now-invalid current ratings block loading.
They contain no groups, ratings, bindings, offline defaults, enable preference,
thermal stop, calibration or saved absence permissions.

## Optional participation

The stored choice is the current included/excluded selection, including decisions
made for this session only. It costs a two-byte identity-indexed mask and one flag
inside each existing 128-byte profile copy; all eight slots remain available.

Restoring a changed selection requires **OFF**, settled availability and the
existing guards. A responding unit must be reconciled and confirmed at zero before
being excluded; returning units must qualify before inclusion. Faulted units
require their normal recovery first. An included unit that is unavailable cannot
be silently included by a profile. Turn OFF, inspect `diag control` and `diag group`,
then review again. A changed topology invalidates confirmation.

Explicitly excluded, responding PSUs remain held at zero after reboot until an
inclusion decision; they are not treated as newly returned units merely because
they were already online. Existing saved absence permissions still govern later
losses. Loading participation does not erase them or authorize a new hot-unit
thermal exclusion. Use the dedicated availability confirmation to release a
missing hot unit's contribution. Healthy units retain their shares.

## EEPROM and migration

| Mega EEPROM addresses | Ownership |
| --- | --- |
| 0–511 | Historical configuration; calibration remains 216–255 and 472–511 |
| 512–1535 | Two 512-byte controller journal slots, schema 6 |
| 1536–3583 | Eight profiles, each with two 128-byte copies |
| 3584–4095 | Reserved |

The nine-unit controller record is 247 bytes. Schemas 2–5 migrate shared voltage
and group totals to per-unit targets, retaining bindings, authorization, permissions
and thermal stops. Schema 1 retains its commissioning/review rules. Boot alone
does not rewrite records. After a new controller record is verified, both old
commit markers are retired; calibration is untouched. **Older firmware cannot
load schema 6 and must not be used as a downgrade or thermal-stop reset path.**
Export the configuration and recommission deliberately before downgrading.

Both journals use generation numbers, CRCs, commit-last writes and readback
verification. A single cooperative writer owns controller saves, profiles,
calibration and protection. Protection can cancel an uncommitted profile copy;
that save is reported cancelled/failed, never successful. Automatic thermal-stop
persistence edits the saved protection state without capturing unsaved RAM drafts.
Later operator edits do not change an in-progress snapshot; its result identifies
an older saved snapshot. `defaults` resets the controller template, not named slots.

## Bench sequence

Use appropriate loads and independent output measurements. Software tests do not
establish PSU delivery, GPIO behavior or physical EEPROM interruption behavior.

1. With charging OFF, confirm stable bindings and grouping using `config`,
   `diag bus`, `diag control`, and `diag group GROUP1`.
2. Stage low test currents with the scoped commands above. Verify each `preview`
   allocation, mixed values and individual override. Try an invalid current and
   confirm that all staged values remain unchanged.
3. `apply all` while OFF; verify “Prepared for next ON” and continued zero hold.
   Save a named profile with participation No. Change a draft, load the profile,
   confirm, and wait for verified save. Check `config` and `diag psu 1`.
4. Enable with `autoresume on`; verify voltage/current ACKs, measured output and
   temperatures. Load a second low-current profile with participation No while ON;
   verify per-unit targets and actual output. A newer OFF must stop the operation.
5. Turn OFF and wait for zero hold. Save/load participation Yes after reviewing
   availability. Verify excluded units remain at zero; loading a changed selection
   while ON must block. Repeat with an unavailable/returned unit and a stale review.
6. Reboot and verify the boot configuration, exclusions and thermal stop state.
   Change temporary CAN addresses without changing identities and repeat load.
   A replacement identity must block the old profile.
7. Under CAN traffic, exercise selectors, names, alert interruptions and all eight
   slots; compare `diag ui`, `diag bus`, and **`diag memory`** before/after. Repeat
   calibration, save failure, protection and reconnect paths. Record the minimum
   `untouched-since-boot`; it is an estimate of exercised stack margin, not proof
   that every runtime path is safe.
