# HOME sequence overview — v0.1-wl-home9

HOME uses four plain track rows. Each shows its bank (machine or USER), patch
number and patch name, with the entire recorded pattern underneath. The small
number at the left is the track's sequence length.

The preview fits all active steps into the row, including step 128. Melodic
notes show pitch and held spans; drum hits use eight lanes. Each track has its
own playhead, so different lengths remain visible. Selecting a track highlights
its row; muted tracks dim. Knob controls, sound browsing, song naming and the
footer remain available.

**CLOCK 2/2, K4 METRO:** OFF / REC / ON. It changes the existing metronome
preference, shared with MENU > AUDIO > CLICK and the REC layer. The setting
persists through the normal settings save. REC clicks while armed and playing;
ON clicks whenever the transport runs. Count-in behavior is unchanged.

## Validation

- Full UI suite passes, including final-step visibility, length changes, playhead
  movement and the eighth drum lane at step 128.
- Real encoder simulation selects all three metronome modes, clamps at the ends
  and shares the existing settings value. Existing click/audio tests pass.
- UI rendering: zero lint and alignment failures across all palettes, both styles,
  text sizes, value sweeps and the MIDI controller popup.
- Installed on FM1-TEST on 2026-10-29 after the user stopped playback.
  The current song was restored after reboot; all eight exposed backup objects
  are byte-identical to the fresh pre-install backup. Physical audition remains
  for the user. Receipt: `build/install-home9-2026-10-29.json`.
