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
Before installation, all 42 backup objects passed CRC verification in
`build/backup-connected-jiant-2026-10-28.json`.

The installer transferred `build/switchblade-v0.1-wl-drum1.fwsc` (SHA-256
`d2aa73c0d664c757a9c0edd76b8468eb7ab3fefd12a7895b5c354985931bc2a5`), restarted the unit,
and confirmed `FM-1_940`. A fresh editor query returned `Switch Blade v0.1-wl-dru`
(the INFO string is capped at 24 bytes), and four tracks: OB-Z, WAVE, NANO, DRUM.
Track 4 was selected, with kit/preset 2 (909). Raw replies are retained in
`build/install-test-unit-2026-10-28.json`. Physical control use and listening are not
verified by this handshake. The separate main Switch Blade and GHOULBOX units were untouched.
