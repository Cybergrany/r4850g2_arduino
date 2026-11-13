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
| `all` | Same voltage on every configured PSU | **Same limit per PSU**, regardless of group sizes |
| `GROUP1` | Same voltage on that group's members | Same limit on every group member; replaces individual overrides |
| `psu:3` | Controller slot 3 only | Slot 3's individual current limit |

For example, with two units in GROUP1 and three in GROUP2, `set current all 30`
stages **30 A on each of the five PSUs**: 150 A across the installation.
Global/group current is bounded by the lowest existing per-PSU limit in the
selection, including absent/excluded members. Serial rejects invalid changes atomically;
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

Different current or voltage targets within a selection show **MIXED** and
the per-PSU minimum/maximum. Group overview readings remain aggregate totals.
Changing membership preserves each unit's configured current; it neither
redistributes current nor sends setting writes. Changing a group's current later
sets the same requested limit on every member. Voltage/current fields are independent:

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
redistributes their current limits or accepts an absence for future operations.

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

## Upgrading from group-total current controls

The normal `set current` commands now mean **amps per PSU**. The syntax is the
same, so update automation that previously supplied a total. For nine units,
`set current all 50` stages 50 A on every unit (450 A configured aggregate).
The physical output still depends on enable, participation, thermal limits and load.

No EEPROM schema or named-profile format changes. Existing configured and
authorized per-unit limits are preserved exactly. An old 30 A total spread over
three units remains 10 A on each after boot or profile load. Unequal old remainder
values may now show MIXED; they change only after an explicit edit. Existing
schema-2–5 migration continues interpreting those old group totals using their
original allocation rule.

The advanced `set offline-current GROUP A` command is still an explicitly separate
**group total** for PSU fallback defaults. It is not part of normal current editing
or named profiles. Its persistence and allocation are unchanged.

## Local controls

Long-press a group to open:

1. Configure global
2. Configure group
3. Configure individual
4. Load Config
5. Save Config

Selectors are paginated. Individual rows show controller slot, current CAN
address or OFF, identity suffix and group. Global/group current is labelled
**CURRENT PER PSU**. The live current beside it is labelled **Total** for
multi-unit selections; it is the measured sum. The global review shows the
per-PSU setting, selected PSU count and resulting configured total.
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

Participation uses the same preview and guards as the availability UI/serial.
Changing an available PSU requires **OFF**, settled availability and confirmed
awake zero hold. Included absent units are **required**, so operation waits for
them; exclusion is never inferred from absence. Faults and ambiguous identities
still block decisions. Inspect `diag startup` and `diag control` before review.

While ON, voltage/current application also checks the currently required members.
If a required absence blocks a profile load, explicitly accept that settled absence
first, or Stop and load while OFF. A load does not waive a missing participant
before its participation save has verified.

Restored participation is written with the profile's single final controller save,
then activated only after verification and revalidation. It replaces affected old
absence permissions: deliberately included units cannot be re-excluded by an older
waiver after reboot. Failed saves or changed conditions are reported; unactivated
intent does not leak into a later unrelated save. Loading with participation No
leaves both runtime choices and saved participation policy untouched.

Excluded units stay excluded when they return, with cooling and monitoring at zero
current. Profiles do not authorize a new hot-unit thermal exclusion: only an explicit
current absence review can release that contribution. Unconfirmed UI checkbox drafts
are never captured. Healthy units retain their configured per-PSU limits.

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
2. Stage `set current all 1`: every configured PSU must preview 1 A, even with
   unequal group sizes. Set `set current GROUP1 2`: every GROUP1 member must now
   preview 2 A. Set `set current psu:1 0.5`: only PSU 1 changes. Confirm MIXED in
   the group/global editor, and that cancelling an edit preserves those values.
   Try a request above a selected member's limit; all staged values must stay
   unchanged. Normal voltage edits must leave current unchanged and vice versa.
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
