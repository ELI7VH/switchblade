# fm1-sb-live

Use the [MiniLab preset and control map](../controllers/README.md) with
`v0.1-wl-live5`. It uses channel 16 CC20–23 for the master strip, independently
of track routing, MIDI learn and the panel FX layer. These controls never write
track parameters, master volume or project fields. The controller preset was
imported into MIDI Control Center and its imported file checked against all eight
encoder CC/channel assignments and the eight A-bank drum notes.

K5 repeat uses the existing master PERFORM capture/crossfade path, including a
1/64 override at values 124–127. Same-effect rate changes compare the actual
loop length, allowing 1/32 <-> 1/64 crossfades without changing saved effect IDs.
Existing buffer-size/tempo availability and SLICER ownership rules remain.

K6 combines saturation, symmetric quantization and sample holding. Block-energy
matching follows the dry signal before the final limiter; a finite makeup limit
and dry fallback protect silent/degenerate blocks. Zero eventually bypasses the
stage exactly. This is level compensation, not a claim that every altered timbre
has identical perceived loudness or that the final limiter has no effect.

K7 adds a smoothed mono dry-mix send to the native delay only. Feedback rises
from the song's own setting towards a bounded slightly-above-unity loop at the
end; native 16-bit feedback saturation bounds it. Turning down restores normal
feedback and stops the extra send, retaining the delay return for its tail.
K8 adds the bipolar master filter sweep, with 63/64 neutral and existing smoothing.
The new MASTER knob cards identify these controls and show repeat rate/filter side.

## Read-only diagnostics

Editor SysEx command 82, with no arguments, replies:
`version=1, repeatRaw, dirtRaw, delayRaw, filterV14`.
The final two bytes use the existing biased signed v14 encoding (-100..100).
This is live state; no raw flash address or write operation is exposed.

## Validation — 2026-10-28

- New master control suite: 60 checks, including channel routing, repeat bands,
  1/64, off/release behavior, silent dirt, level comparison and echo-tail state.
- Level comparison: eight sine/stereo input/range cases differed from dry energy
  by less than 0.01 dB. Master-state protocol test confirms values and project isolation.
- Existing MIDI-control integration passes. All 157 audio golden renders unchanged;
  no voice/routing failures or CPU-budget entries over budget in that regression.
- Existing PERFORM functional checks pass; its pre-existing host FLANGER/PHASER
  incremental-cost threshold fails. Neither the existing DSP nor that threshold
  was relaxed. A separate four-drum/four-Juno full-strip probe measured worst
  2168 instructions/sample for 808 and 3259 for 909 (device estimates 36.9%/55.4%,
  not actual on-device timing or listening proof).
- Master-card endpoints, centre and repeat boundaries render with zero lint or
  alignment failures. Firmware build passes memory and interrupt checks.

Hardware MIDI readback/install evidence, when available, is recorded below.
