# Drum sequencer implementation and validation

Build: `v0.1-wl-drum1`. User controls are in the README's **Drum step sequencer** section.

## Storage and playback

- The eight canonical kit notes use `step_t.hit` and `step_t.acc`. These already occupy
  two bytes of each nine-byte FOR4 step, previously written zero. Project size, flash
  layout and all existing engine identifiers stay unchanged.
- Only DRUM tracks serialize lane bits. Exact legacy/variant MIDI notes remain in
  `note[4]`; duplicates between a note and its canonical lane play once.
- Playback and recording skip buffers accommodate four exact notes plus eight lanes.
  Drum recordings never write ties. Quantisation, swing, chance/conditions, ratchets,
  nudge, MIDI routing and note-offs use the existing sequencer paths.
- Step velocity is shared by unaccented hits; each new lane bit can accent independently.
  The old whole-step accent flag still applies to old note-based steps.
- Projects, autosave, set-list songs and complete project backups carry the grid.
  Old firmware ignores the lane bytes: back up before downgrading. The existing web
  step editor has no lane-grid UI; this change targets the FM-1 controls.

## Verification (2026-10-28)

- `tests/drum_seq_test.c`: 33 checks pass normally and under UBSan: panel gestures, eight simultaneous triggers, lane-only deletion,
  held velocity/accent, page/length bounds, legacy 909 pitches, FOR4/runtime round trips,
  old note-only playback, duplicate suppression, eight-pad overdub, no recorded ties,
  chance/ratchets, transport loop timing, recorded-ahead trigger suppression, kit
  changes retaining beats, synth step-entry separation and MIDI-follow gesture lock.
- Firmware build checked against the JieLi memory and interrupt-code limits.
- Real UI renderer: dedicated 808/CR78/909 scenes, held-hit cards and both label sizes;
  full layout sweep has zero lint findings and zero alignment failures.
- Existing project, motion and MIDI suites rerun after compatibility fixes.
- Full regression: all 157 audio golden renders unchanged; all 168 engine-switch
  sequences unchanged. No voice/routing or CPU-budget regressions in those renders.
- Two pre-existing host performance assertions fail on both the untouched base and
  this build: MOD matrix's 6% incremental-cost ceiling and PERFORM's 2.5% phaser ceiling.
  Their thresholds and sound/CPU baselines were not relaxed.
- ASan is unavailable on this host: even the suite's empty `int main(void){return 0;}`
  probe spun without exiting and was terminated. The separate UBSan drum run passes.
- Device probe: no MIDI inputs; no FM-1 available. Firmware has **not** been installed,
  listened to on hardware or tested with physical buttons/controllers. Identify the
  Switch Blade unit, back up its songs, and ensure the user is not playing before flashing.

## Test-unit installation (2026-10-28)

Elijah confirmed the connected FM-1 was his test unit and authorized replacing JIANT.
Before installation, all 43 backup objects passed CRC verification in
`build/backup-connected-jiant-2026-10-28.json`.

The installer transferred `build/switchblade-v0.1-wl-drum1.fwsc` (SHA-256
`d2aa73c0d664c757a9c0edd76b8468eb7ab3fefd12a7895b5c354985931bc2a5`), restarted the unit,
and confirmed `FM-1_940`. A fresh editor query returned `Switch Blade v0.1-wl-dru`
(the INFO string is capped at 24 bytes), and four tracks: OB-Z, WAVE, NANO, DRUM.
Track 4 was selected, with kit/preset 2 (909). Raw replies are retained in
`build/install-test-unit-2026-10-28.json`. Physical control use and listening are not
verified by this handshake. The separate main Switch Blade and GHOULBOX units were untouched.

## Sequence-length update installed (2026-10-28)

Installed `v0.1-wl-len2` on the connected Switch Blade test unit. Both length knobs
now offer 2, 4, 8, 16, 32, 48, 64 steps (feature commit `0f6e28c`). The focused
length suite passes 26 checks; the drum suite passes 33. Target build passes memory
and interrupt checks. Original drum synthesis is unchanged.

Before installation, all eight objects exposed by the backup API passed CRC checks
in `build/backup-pre-lengths-2026-10-28/`; this API does not expose separate setlist
song objects. The same eight objects passed after reboot, and the current project
is byte-identical. Installation confirmed identity `FM-1_940`; live editor INFO
reports `Switch Blade v0.1-wl-len` (24-byte name limit).

Package SHA-256: `16fb97678847ac3badac7bf16e559e1366ec5e82bf5e4a9d47ab25d85eb6f4c3`.
Local receipt: `build/install-lengths-2026-10-28.json`. Physical knob operation
has not been verified by this software handshake.

## Direct step-division control

`v0.1-wl-div3` puts DIV on the drum grid's K4 (no step held). It uses the
native musical division order, from 4BAR through 1/32 including 8T/16T.
Holding steps keeps K4's accent gesture. Swing remains hold SEQ + K3;
regular tracks keep hold SEQ + K2 for division. Old songs retain their stored
rate, and changing division leaves the notes, hits and swing intact.

64 steps span 4 bars at 1/16, 8 bars at 1/8, or 16 bars at 1/4 in 4/4.
This trades step resolution for phrase duration; it does not add steps.
The combined length/division host test passes 35 checks and the drum suite
passes 33. Target build passes memory and interrupt checks.

Installed on the connected test unit on 2026-10-28; reboot and editor INFO
confirm `Switch Blade v0.1-wl-div` (24-byte limit). The unit initially resumed
an older autosave; restoring current-project object 0 from the fresh pre-update
backup recovered all edits, verified byte-for-byte by another CRC-checked backup.
The other seven exposed backup objects also passed CRC verification.
Local receipt: `build/install-div3-2026-10-28.json`. Package SHA-256:
`d03e3867590c81f2015b238a5eb2031491686b66ff60a6a54e218017dd488848`. Physical knob use remains unverified.
