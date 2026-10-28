# Played note lengths

New live recordings keep the time from key-down to release. Start quantization
(`QUANTIZE ON`) still snaps the attack to the nearest step. Release timing is
independent of the global GATE setting. No new control is needed.

- Synth chords share a duration, ending when the last key is released.
- Drum hits stay one-shots. Stopped K1 hold-and-stretch entry stays step based.
- Editing a note's LEN explicitly returns it to manual GATE/TIE timing.
- Existing recordings retain their original timing; record a new take for this behavior.

Duration is stored in 1/256-step units, not whole steps (about 0.49 ms at
120 BPM and 1/16 division). Playback note-offs run at the audio control-block
resolution. The duration follows tempo and division, including external-clock
sequencing. As before, one step holds one chord; a new chord in another step
ends the previous recording group. A subsequent NOTE or REST can interrupt a
hold. STOP finalizes recording and releases playback. The arp receives the
same timed chord; its own pattern/gates and ATTACH setting still govern its notes.

## Project compatibility

FOR6 retains FOR5's 6400-byte size, 128 steps, and flash layout. Each synth
attack uses its previously unused `hit`/`acc` bytes for a little-endian Q8
hold duration, 1..32768; zero keeps legacy GATE/TIE playback. DRUM still uses
those bytes as lane/accents masks. TIE continuation cells cover the actual
swung span; they do not restart the release timer.

FOR4 and FOR5 load through the importer with zero synth duration and their
original timing. New saves use FOR6, which earlier firmware cannot read.
Keep a pre-update backup for rollback. Project restore carries the byte
interpretation into the engine switch; changing a track between DRUM and a
synth clears these engine-specific bytes instead of interpreting them as
another engine's data.

## Verification

`tests/played_hold_test.c` checks fractional releases, quantized starts,
long holds, chords, STOP, swing, pattern wrap, full-pattern limits, arp gates,
tempo scaling, manual LEN override, engine switching and FOR5 migration.
All 26 hold checks, 22 K1 stretch checks, 98 extended-step checks and 33 drum
checks pass. Project, motion, persistence, backup, MIDI, arp and sequence-tool
suites pass. All 157 audio golden renders remain unchanged, with zero health,
routing, voice or CPU-budget failures. Target build: 486156-byte image,
64280/73728 bytes main RAM, 341724/368640 bytes pool.
The existing K1 stretch and drum suites remain separate regression checks.
Hardware installation/listening is pending: no FM-1 MIDI ports were present
when v0.1-wl-hold6 was prepared.
