# Held-note length entry

On a melodic track's STEP piano roll, arm REC with the transport stopped.
Hold a note or chord, then turn K1: right extends its ties, left shortens them.
The cursor stays at the note's start while held. Release every held key to move
to the first step after the full span; a span ending at the pattern boundary
wraps the cursor to step 1. Panel keys and MIDI controllers use the same path.

An existing note ahead blocks extension, and the pattern length is the upper
boundary. No note ahead is overwritten. Ordinary release without a K1 length
gesture retains the existing one-step advance (or no advance in CHORD ENTRY ADD).
An explicit stretch gesture also advances in ADD mode.

With nothing held K1 still moves the cursor. K2–K4 keep their existing held-step
parameter locks; the held-note K1 card shows LEN. Unarmed playing, live recording,
DETAIL and drum one-shots keep their existing controls. K3 edits length when no
key is held as before. The project format and timing resolution are unchanged.

Validation:

- `note_stretch_test.c`: 22 checks pass, including real MIDI queues, chords,
  partial releases, shrinking, collisions, cross-page advance, the 128-step end,
  ADD mode and isolation from unarmed input/drums.
- UI, motion/undo and MIDI-control regression suites pass. Tests for K1 parameter
  locks now use K2 because K1 is deliberately dedicated to held-note length.
- Target build passes memory and interrupt checks.
- Full firmware UI sweep: zero lint findings and zero alignment failures.

The build is ready as `v0.1-wl-tie4`. Installation was held because the connected
unit was emitting playback clock (144 clock events in three seconds); no flash
or stop command was sent. Confirm the user has stopped before installing.
