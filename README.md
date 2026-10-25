# Switch Blade

[![License: GPL-3.0-only](https://img.shields.io/badge/license-GPL--3.0--only-blue.svg)](LICENSE)

## WaveLoop build

This fork (branch `waveloop/main` of [ELI7VH/switchblade](https://github.com/ELI7VH/switchblade)) plays Switch Blade as one
synth from an **M-Audio Oxygen Pro 61** set to the preset of the WaveLoop Melodee fork
(`controllers/Melodee.OxygenPro61UserPreset` there; programmed in its slot 16). Set the keyboard's channel to 16:

- **Channel 16 follows the FM-1.** With MIDI IN on CH1-4 (also CH5-8, CH9-12), channel 16 plays and edits the selected
  track. Notes held when you change track end where they started. Keep MENU > MIDI > AUTO SELECT off (the default), or a
  pad (channel 4) selects track 4 and the keyboard follows it there.
- **The knob card.** Turn any MIDI knob, fader or the mod wheel, or an FM-1 knob on HOME, and a card names the track and
  machine, what the knob does and its value, with a bar. It goes 0.8 s after the last move.
- **The mod wheel is a vibrato** on every machine: about 5.4 Hz, up to half a semitone at the top, the wheel squared so
  the bottom is gentle. It adds to any MOD slot that uses MODW. Pitch bend is unchanged.
- **Aftertouch** (channel pressure, or poly pressure as the hardest key) adds the same vibrato as the wheel (the two
  together go no deeper than the wheel at the top) and opens the filter a little: up to 12 steps of CUTOFF at full
  pressure, about an octave, gliding so it never zips. With no pressure the sound is exactly as before. On channel 16
  it follows the selected track, and the knob card shows it as AFTERTOUCH.

| Oxygen control | Sends | Does (every machine) |
| --- | --- | --- |
| Knob 1 | CC74 | CUTOFF of the machine (OB-Z, WAVE, NANO, JUNO) |
| Knob 2 | CC71 | RESONANCE |
| Knob 3 | CC5 | GLIDE |
| Knob 4 | CC10 | PAN |
| Knob 5 | CC76 | LFO RATE |
| Knob 6 | CC77 | LFO > FILTER (middle = none) |
| Knob 7 | CC78 | LFO > AMP (tremolo) |
| Knob 8 | CC70 | LFO WAVE (SIN TRI SAW SQR S&H) |
| Faders 1-4 | CC7, channels 1-4 | VOLUME of tracks 1-4 |
| Faders 5-8 | CC73 75 79 72 | ATTACK, DECAY, SUSTAIN, RELEASE of the amp envelope. NANO's 303 (sounds 1-2): fader 6 is its DECAY, the others do nothing |
| Fader 9 | CC8 | nothing (Switch Blade does not play the computer's audio) |
| Encoder turn | CC114 (relative) | next / previous sound on the selected track, on into the next machine's sounds after the last (PRESETS' list) |
| Fader buttons 1 / 2 | CC112 (63 / 65) | previous / next machine on the selected track (its first sound) |
| Encoder press | CC115 | next track (after T4, T1) |
| Pads | notes, channel 4 | track 4 |
| Mod wheel | CC1 | vibrato |
| Aftertouch | channel / poly pressure | vibrato and a little more cutoff |

EDIT's knob 3 (ENV) and knob 4 (the machine's own control) moved from CC79 / CC70 to CC16 / CC17, in and out
([docs/MIDI.md](docs/MIDI.md)). Nothing new is stored: the card, the wheel and the encoder are live controls.

### The ARP is SHAYPE

The arpeggiator is WaveLoop's Shape Arp. A pattern is notes on a grid: steps across, rows up. A row is a place in the
chord you hold, counted from its lowest note. Each pass through the steps moves every row one place (DIR), so the shape
climbs, falls, swings or jumps through the chord. Hold a chord (keys, the Oxygen, MIDI IN) and the track plays the pattern
over it, on its own clock or the external one, as the old ARP did. Each track has its own pattern.

**The sequencer plays through the ARP.** With ARP on, the track's sequence does not sound its notes itself: each chord it
plays becomes the chord the ARP plays, as if you held those keys for as long as the step's notes last (GATE, and TIEs or
a step's LEN for longer chords). Sequence chords, one per step or tied over several, and SHAYPE plays them. Keys and MIDI
notes you hold join the sequenced chord, and HOLD keeps them as usual. When a chord ends and nothing is held, the ARP
stops; nothing is left sounding. With ARP off the sequence plays as before. Nothing new is stored: ARP on is the switch,
and the chords are the pattern's steps, saved with the project as ever.

While the transport plays (the FM-1's clock or an external one), the ARP steps on the song's grid from PLAY: chord or
none, its step is where the song is, so it stays in time with the sequence. A new chord starts on the next ARP step, or
at once if it comes in the first half of a step that started with nothing to play. A chord that changes mid-pass changes
the notes at the next step; the pass's DIR offset changes at the next pass. Stopped, a chord from nothing starts the
pattern at its first step, as before. This is the toy's offline form `shapeChords` live: `tests/arp_seq_test.c` plays
chords through the sequencer and wants exactly the notes `shapeChords` gives for them (`tests/arp_chords_vectors.h`,
`node tools/arp_vectors.mjs --chords <sequence.mjs>`).

**Recording with the ARP on records the chords you play**, not the ARP's notes, so the take plays back through the ARP.

### Recording from MIDI

MIDI notes (USB and DIN) record exactly as the FM-1's keys, on the track their channel plays: MENU > MIDI > MIDI IN
CH1-4 (and CH5-8 ...) its four channels, channel 16 the selected track, SEL every channel the selected track.

- **Live:** arm REC and play. Notes go into the nearest step as REC always quantised them (QUANTIZE, NUDGE), held notes
  as TIEs.
- **Step:** on SEQ > STEP, arm REC with the transport stopped (KEYS WRITE STEPS). Notes for the selected track write the
  cursor step: a chord played is one step, and letting go of every note (keys and MIDI) moves the cursor on. CHORD ENTRY
  ADD works the same. Another track's channel only plays its track.

The ARP button opens three pages, all showing the grid: 16 steps at a time around the cursor, the step playing lit.

- **Move the cursor** with PRESETS (it goes round the pattern's STEPS).
- **Write a note:** tap a row key at the cursor. The row keys are the white keys F3 to F4 (F3 is row 1, F4 is row 8).
  Tap the key again to take the note away. On these pages the row keys and the black keys between them make no sound.
  The keys above F4 still play, so you can hold a chord there while you edit.
- **Set a note's length:** hold its row key and turn any knob.
- **EDIT** clears every note that starts on the cursor step.

| Page | Knob 1 | Knob 2 | Knob 3 | Knob 4 |
| --- | --- | --- | --- | --- |
| ARP | ARP (on / off) | STEPS (1-64) | RATE (1/2 to 1/32, triplets included) | DIR (UP, DOWN, PEND, RAND) |
| ARP 2 | BOUND (WRAP, OCT) | GATE (10-100 %) | HOLD | ROOT (OFF, C to B) |
| ARP 3 | PAT (factory pattern) | SWING | | |

- **BOUND:** when a row goes past the top of the chord, WRAP starts again from its lowest note. OCT carries on an
  octave up (or down).
- **GATE:** each note sounds for its length times the step times GATE.
- **HOLD:** the chord stays after you let go. The next chord you press replaces it.
- **ROOT:** the held note with that name becomes the lowest note. The notes below it move up an octave.
- **PAT** loads a pattern as you turn to it, with its STEPS and BOUND: SHAYPE (the default), UP 4, UPDOWN, STABS, OCT RUN,
  PEDAL, DRONE. Editing the pattern makes it USER.
- **STEPS** made shorter removes the notes past the new end.
- **ZOOM:** hold OP1 and press OCT+ to zoom in: STEPS and RATE double, and every note's step and length double with
  them, so the pattern sounds the same with twice the steps to draw in (16 steps at 1/16 become 32 at 1/32). OCT- zooms
  out: half the steps at half the rate. The cursor stays on the same place in the music, a playing arp carries on where
  it is, and the screen shows the result (ZOOM 32 STEPS 1/32). It works on all three ARP pages, as often as you like
  while OP1 stays down. When it can't, nothing changes and the screen says why: 64 STEPS MAX, 1/32 IS THE FASTEST,
  1/2 IS THE SLOWEST, NO 1/32T RATE (from 1/16T), NO 1/2T RATE (from 1/4T), STEPS ARE ODD, or NOTE BETWEEN STEPS (a
  note starts or ends on an odd step, so joining pairs would move it). This is the toy's x2 and /2 (`subdivide`);
  `tests/arp_shape_test.c` holds it to `tests/arp_zoom_vectors.h` (`node tools/arp_vectors.mjs --zoom <sequence.mjs>`),
  the toy's refusals included. The toy goes on to 1/64; RATE here stops at 1/32.

**The OP keys are SHIFT.** The stock FM-1 prints OP1 to OP4 on the four buttons Switch Blade calls FX, SCL, ENV and
LFO (left to right), so OP1 is FX, OP2 SCL, OP3 ENV, OP4 LFO. That is an assumption from the stock panel: if yours
differs, it is one table (`OP_BTN` in `firmware/src/ui_layer.c`). Hold an OP and press another button for a shift
function: that press belongs to the shift, so neither the OP's own action (its page, its quick layer) nor the other
button's (OCT's octave) happens. Tap an OP alone and it does what it always did. OP1 + OCT+ / OCT- on the ARP pages is
ZOOM (HELP shows it on ARP 2 as FX + OCT ZOOM); OP2 to OP4 are free for more.

The pattern and every ARP setting are saved with the project and the autosave, and come back exactly; a zoom is an
edit like any other (PAT becomes USER). A project saved before this gets SHAYPE on every track. Its ARP stays on or off, and its rate, gate, HOLD and SWING carry over. The rest
starts at the defaults. The core is a port of the web toy's `sequence.mjs`. `tests/arp_shape_test.c` checks it against
`tests/arp_vectors.h`, which `node tools/arp_vectors.mjs <path to sequence.mjs>` writes from the toy itself.

---

Free firmware for the **M-VAVE FM-1** that packs four synth machines into the box: one per track, eight voices shared.
No drums, on purpose: it is built to be a synth expansion, for example for an Elektron Syntakt with MIDI channels 1-4
playing the four machines.

- **OB-Z**: a big, brassy poly
- **WAVE**: the voice of [Void Wave](https://ollienicklin.com/projects/void-synth.html), a wavetable synth
- **NANO**: a 303 (presets 1-2) plus a polyphonic subtractive synth (presets 3-32)
- **JUNO**: a warm poly with a Juno-60-style chorus

From the HOME screen, knobs 1-4 play cutoff, resonance, envelope and one parameter per machine; SELECT is the track's
volume. Every track also has a drive, insert effects and the shared chorus, delay and reverb.

**Install, try it in your browser, read more:** https://ollienicklin.com/switchblade/
(install from desktop Chrome or Edge over USB in about two minutes; the flasher also returns the FM-1 to the official
firmware).

## Building

The firmware builds with the JieLi toolchain in Docker: see [BUILDING.md](BUILDING.md). Host tests: `sh tests/run_tests.sh`.
The browser version is `web/emu/` (`sh web/emu/build.sh`, needs emsdk).

## Built on Felucca

Switch Blade is a fork of [Felucca](https://hugelton.github.io/Felucca/) by Leo Kuroshita
([@kurogedelic](https://github.com/kurogedelic), [Hügelton Instruments](https://hugelton.com)), the open FM-1 firmware.
Felucca's own README is kept in [docs/FELUCCA_README.md](docs/FELUCCA_README.md). Every other credit and licence:
[CREDITS.md](CREDITS.md), [LICENSING.md](LICENSING.md).

Switch Blade is an independent project, not affiliated with or endorsed by M-VAVE. Product names belong to their owners.
Install at your own risk.

## Licence

GPL-3.0-only, as Felucca: [LICENSE](LICENSE).
