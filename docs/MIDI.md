# VOID FOUR and MIDI

VOID FOUR has four tracks, and each track is one machine: OB-Z, WAVE, ACID and DRIFT.
Each track can play its own machine, drive an external synth, or do both.
The same CC numbers work in both directions. A controller turning CC74 on channel 1 moves track 1's CUT.
When track 1 is set to send, turning track 1's CUT knob sends CC74 on channel 1.

MIDI OUT goes over **USB only**. The TRS jack is a MIDI **input**.

## Channels

By default, track N uses channel N:

| Track | Machine (default) | Channel |
|---|---|---|
| T1 | OB-Z  | 1 |
| T2 | WAVE  | 2 |
| T3 | ACID  | 3 |
| T4 | DRIFT | 4 |

MENU > MIDI > MIDI IN chooses the block of channels: CH1-4 (the default), CH5-8, CH9-12 or CH13-16.
It can also be set to SEL, where every channel plays the selected track, or to TRACK, where you pick a channel for each track in MENU > MIDI > T1..T4 CHANNEL.
MIDI OUT uses the same channel that the track listens on.
With SEL, or with a track set to OMNI or OFF, the track sends on its own channel from CH1-4 (T1 on 1, T2 on 2, and so on).

## MIDI OUT per track: INT, EXT, BOTH

MENU > MIDI OUT > T1..T4 OUT:

| Setting | What it does |
|---|---|
| **INT** (default) | The track plays its machine only and sends nothing. |
| **EXT** | The track drives external gear only. Its own voice stays silent. |
| **BOTH** | The track plays its machine and sends to external gear at the same time. |

In EXT and BOTH, the track sends these on its channel:

- **Notes** from the sequencer, the arpeggiator, the keys and chords. Every note-on is followed by its note-off, even if you switch the track back to INT while the note is held.
- **The EDIT knobs, VOLUME and the other mapped controls**, as CCs from the table below. A CC is sent only when its value changes, at most once per audio block.
- **Program Change** when you load a factory preset on the track.

Two things are never sent back out:

- Notes and CCs that come **in** over MIDI. This means a DAW that echoes MIDI back cannot create a feedback loop.
- When a preset is loaded, its own CUT, RES and other values. The external synth gets the Program Change and keeps the sound that program chose.

Switching a track to EXT or BOTH sends nothing until you change something.

## The CC map (the same in and out)

| Control | CC |
|---|---|
| VOLUME (track level) | 7 |
| CUT (EDIT knob 1) | 74 |
| RES (EDIT knob 2) | 71 |
| ENV, the filter envelope amount (FLT, EDIT knob 3) | 79 |
| MACHINE control (EDIT knob 4) | 70 |
| GLIDE | 5 |
| PAN | 10 |
| ATTACK | 73 |
| DECAY | 75 |
| RELEASE | 72 |
| REVERB send | 91 |
| CHORUS send | 93 |
| DELAY send | 94 |
| Preset | Program Change 0..31 |

EDIT knob 4 is each machine's own control: DTN (detune) on OB-Z, POS (wavetable position) on WAVE, DECY (decay) on ACID and DRV (drive) on DRIFT.

CC values 0..127 span the parameter's whole range. For a bipolar value such as PAN, 64 is the middle.
Program Change 0..31 picks the track machine's preset with that number, in the order the PRESETS list shows them.
A program number past the last preset is ignored.

Other controllers received: CC1 (mod wheel), CC11 (expression), CC64 (sustain), pitch bend with RPN 0 bend range, channel aftertouch, and CC120, CC121 and CC123 (sound off, reset controllers, notes off).
With MENU > CONTROL > SELECT set to MODW, turning SELECT on a track set to EXT or BOTH sends CC1 on that track's channel.

## Clock

MENU > MIDI OUT > CLOCK OUT (default ON) sends MIDI clock over USB while VOID FOUR runs on its own clock (CLK INT):

- **Start (FA)** when you press PLAY, and on a restart (GLO + PLAY).
- **Clock (F8)** at 24 pulses per quarter note, locked to the sequencer.
- **Stop (FC)** when you press STOP.

When VOID FOUR follows an external clock (CLK USB or CLK TRS), it sends no clock. This prevents clock loops.
Clock goes out for the whole device, whatever each track's OUT setting is.

## MIDI LEARN

MIDI LEARN works as before (GLO + D4). A learned CC is yours: on input it overrides the fixed map above.
MIDI OUT always sends the fixed map, not learned CCs.

## Recipe: drive an external synth from track N

1. Connect VOID FOUR to your computer or USB MIDI host. On the external synth, set its MIDI input to receive **channel N** from VOID FOUR's USB port. If MENU > MIDI > MIDI IN is not CH1-4, use the channel shown for track N.
2. Go to **MENU > MIDI OUT > TN OUT** and choose **EXT**, or **BOTH** if you also want VOID FOUR's machine to play.
3. Play the keys or press PLAY. Track N's notes now play the external synth, and EDIT knobs 1..4 move its filter and more on **CC 74, 71, 79 and 70**. Load a preset on track N to send a Program Change. If the synth should follow VOID FOUR's tempo, leave CLOCK OUT on and set the synth to external clock.
