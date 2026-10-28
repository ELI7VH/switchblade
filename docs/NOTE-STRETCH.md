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

The build is `v0.1-wl-tie4`. Installation was initially held because the connected
unit was emitting playback clock (144 clock events in three seconds); no flash
or stop command was sent. Confirm the user has stopped before installing.


## Installed after playback stopped — 2026-10-28

Elijah confirmed "Stopped — install it". Installation completed and the unit
restarted as `FM-1_940`; live editor INFO returned `Switch Blade v0.1-wl-tie`
(the 24-byte name limit). The unit initially resumed its autosave, so the fresh
pre-update runtime backup was restored. A second CRC-checked backup confirmed
the complete current project was byte-identical to the pre-update copy.

Code: `49c10dd`. Package SHA-256:
`fb59157c0da6b9aafd8bf4ed612a8d9179df3aa948cd7cf14594fbe919201a58`.
Local receipt: `build/install-tie4-2026-10-28.json`; backups:
`build/backup-pre-tie4-2026-10-28/` and `build/backup-post-tie4-2026-10-28/`.
Physical knob operation still needs the player's hands-on check.
