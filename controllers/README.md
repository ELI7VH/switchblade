# fm1-sb-live — MiniLab 3

Import **fm1-sb-live.minilab3** in Arturia MIDI Control Center, select the User
memory you want, then **Store To**. Select that program on the MiniLab.
The native file imported successfully into MIDI Control Center 1.2.0.
It requires the matching Switch Blade **v0.1-wl-live5 or newer** firmware for master effects.

Connect MiniLab MIDI OUT to FM-1 MIDI IN. Use keyboard MIDI channel **16** to
play the selected FM-1 track; pads stay on channel 4 for the drum track. Keep
FM-1 routing CH1–4 (channel 16 additionally follows the selected track).

| Control | Action | MIDI |
| --- | --- | --- |
| K1–4 | Attack, decay, sustain, release (selected track) | Ch16 CC73,75,79,72 |
| K5 | Master repeat: OFF, 1/8, 1/16, 1/32, 1/64 | Ch16 CC20 |
| K6 | Master drive / bitcrush with RMS-level compensation | Ch16 CC21 |
| K7 | Dub delay throw; turn down to stop feeding, let echoes decay | Ch16 CC22 |
| K8 | Master DJ filter: left LPF, centre OPEN, right HPF | Ch16 CC23 |
| Faders 1–4 | Track 1–4 volume | Ch1–4 CC7 |
| Main encoder | Sound; Shift + encoder: machine; click: next track | CC114,112,115 |
| Pads A1–4 | Kick, snare, clap, rim | Ch4 notes 36,38,39,37 |
| Pads A5–8 | Closed hat, open hat, tom, cymbal | Ch4 notes 42,46,45,49 |
| Pads B1–4 | Low tom, high tom, ride, bell | Ch4 notes 41,43,51,56 |
| B5 / Stop | Stop | Ch16 CC106 gate |
| B6 / Play | Start | Ch16 CC107 gate |
| B7 / Record | Toggle REC on selected track | Ch16 CC108 gate |
| B8 / Tap | Reset master-strip controls | Ch16 CC121 |

K1–4 control the synth amp envelope; use the FM-1 panel for its edit knobs.
DRUM ignores these envelope CCs. NANO 303 uses decay only.

## Before playing

Turn K5–7 to zero; centre K8 (values 63 or 64). These are live controls and
are not saved into song/sound parameters. Channel 16 CC120/121/123 releases
the master controls. MIDI Control Center Store To replaces the chosen User
memory, so choose the slot you intend to use.

K5 bins: 0 OFF, 1–80 1/8, 81–112 1/16, 113–123 1/32, 124–127 1/64.
The last two bands are intentionally narrow. Repeat starts on the next 1/16
while playing, immediately while stopped. At slow tempos, a loop longer than
the existing 371 ms shared repeat buffer is unavailable (1/8 below about 81 BPM).
Repeat borrows the native SLICER recording buffer, following existing FX behavior.

K6 combines soft saturation, symmetric bit reduction and sample holding. Its
block-energy compensation follows the dry level before the final master limiter;
it does not move any volume parameter. Transient shape/perceived loudness can
still change with the timbre. The final limiter remains in place.

K7 uses the native tempo delay bus. The top end lifts feedback just above unity
into its bounded feedback path. Returning to zero stops the added input and
returns feedback to the song's normal setting; the tail remains audible. It does
not add reverb or write track sends/feedback into the song.

## Attribution

The export is adapted from Elijah Lucian / Lucian Labs' existing Felucca MiniLab
preset (ELI7VH/Felucca, codex/standalone-minilab-midi). Firmware extends the
Felucca / Switch Blade PERFORM and MIDI paths; preserve their GPL-3.0-only
headers and credits. New master-strip integration is by Elijah Lucian / Lucian Labs.

MIDI Control Center may move an imported file into its Templates directory. A copy is retained here and in the release bundle.

In **v0.1-wl-hold6**, live-recorded synth releases follow your played hold time;
start quantization and the MiniLab preset stay the same. See
[played holds](../docs/PLAYED-HOLDS.md).
