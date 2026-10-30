# fm1-sb-live

Use the [MiniLab preset and control map](../controllers/README.md) with
`v0.1-wl-live5`. It uses channel 16 CC20–23 for the master strip, independently
of track routing, MIDI learn and the panel FX layer. These controls never write
track parameters, master volume or project fields. The controller preset was
imported into MIDI Control Center and its imported file checked against all eight
encoder CC/channel assignments and the eight A-bank drum notes.

MiniLab K1–4 now send amp ADSR (Ch16 CC73,75,79,72) for the selected track.
This is a controller-preset update; existing firmware already handles these CCs.
Re-import and Store To the MiniLab to apply it. K5–8 keep their master effects.

K5 repeat uses the existing master PERFORM capture/crossfade path, including a
1/64 override at values 124–127. Same-effect rate changes compare the actual
loop length, allowing 1/32 <-> 1/64 crossfades without changing saved effect IDs.
Existing buffer-size/tempo availability and SLICER ownership rules remain.

K6 combines saturation, symmetric quantization and sample holding. Block-energy
matching follows the dry signal before the final limiter; a finite makeup limit
and dry fallback protect silent/degenerate blocks. Zero eventually bypasses the
stage exactly. This is level compensation, not a claim that every altered timbre
has identical perceived loudness or that the final limiter has no effect.

K7 adds a smoothed mono send to the native delay only (post-dirt/stutter in chain14). Feedback rises
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

## Installed test unit

The connected FM1-TEST was observed stopped (CLOCK OUT enabled, no playback or
note events for three seconds), backed up and updated to `v0.1-wl-live5` on
2026-10-28. After reboot, INFO identified `Switch Blade v0.1-wl-liv` (24-byte
limit). The latest pre-update running project was restored instead of the boot
autosave and verified byte-identical by a fresh CRC-checked backup.

The live endpoint returned the four neutral controls. A stopped-transport probe
then sent Ch16 CC20=124, CC21=71, CC22=43, CC23=0 and read back repeat 124, dirt
71, delay 43 and filter -100. All controls were restored to their initial neutral
values. This verifies the shared MIDI handler over USB; the user's physical
MiniLab DIN gestures and listening remain separate.

Local receipt: `build/install-live5-2026-10-28.json`.
Package SHA-256: `f78b6eda9579e75a5b72709556bc1fa3c8950b021eac816f774466770b07771f`.
The user confirmed loading the MiniLab preset and a direct MIDI cable connection.
Backup bundle: `build/fm1-sb-live.zip`, with the preset, firmware, license texts
and pinned corresponding-source link/hash manifest.


## Master signal order (chain14)

The live strip processes **bitcrush/dirt → stutter → delay → DJ filter**.
K6 is captured into K5's repeat, K7 receives the repeated result, and K8 also
filters the delay return. Turning dirt after a loop is captured affects fresh
input, while that frozen loop retains its captured timbre. Existing notes,
controller CC assignments, songs and presets are unchanged.

The live dirt and buffer stages run on the mixed tracks before the delay send;
the delay/reverb/chorus buses then return ahead of the final master filter.
Saved per-track sends remain parallel native buses. Native FX without live
strip controls keep their previous placement. The live path remains selected
until its effect fades/tails finish, avoiding an order change on release.
No extra delay line or audio sample-rate change is introduced. Dirt retains its
block-energy compensation and the final limiter remains in place.

`tests/sb_chain_test.c` checks audio behavior: a captured crushed loop is
unchanged when dirt is released, frozen audio feeds delay with fresh input
silent, echoes remain audible over a silent frozen loop, and the final low-pass
attenuates those echoes. Existing dirt RMS and silence tests remain.


### chain14 build and checks — 2026-10-30

Package: `build/switchblade-0.1-wlchain14.fwsc`, SHA-256
`ff860ea504da43d3db2019706df74436fad011ededbedc0626a24573f502d94a`.
Final order tests pass normally and with repository UBSan DSP-wrap exclusions;
release returns to idle, full strip output stays bounded, and silence stays
silent. Component tests (60 checks) and read-only protocol isolation pass.
The broad host run retains all 157 golden renders with zero sound, health,
voice/routing or regression CPU-budget failures. It is not a clean full-suite
pass: the modulation-matrix incremental-cost threshold fails at about 6.0–6.1%
against its 6% limit, also reproduced on unchanged pre-chain source. That
threshold was not relaxed. An intermediate test-fixture initialization failure
was corrected and the final order test rerun normally and under UBSan.

Correction to the earlier grid13 target-budget report: its `perf_pre` loop was
39 weighted instructions against an existing 26 baseline, so the earlier claim
that every target budget passed was incorrect. chain14 moves the live echo
send into a separate loop, with its enable tested once per block. Native
`perf_pre` now measures 27 (within the original 10% tolerance). Existing budgets
are unchanged; new buffer/send/finish helpers have explicit baselines. All final
target checks pass. These static checks are not live CPU/headroom measurements.

Installed on **FM1-TEST, 2026-10-30**, with explicit stopped/install approval.
Before installation, all 20 musical/settings objects were CRC-validated and raw
storage was read twice identically. After reboot, serial and editor report
`v0.1-wlchain14`; all 8 logical objects and all 12 native song slots match the
fresh backup byte for byte. No runtime restore was needed. Live strip controls
were restored/read back too, including the user's filter value -28. Playback
is stopped. Physical listening remains with Elijah. SHOW and GHOUL were untouched.

Private archive: `~/Documents/FM1 Backups/FM1-TEST-pre-chain14-2026-10-30.zip`.
Receipt: `build/install-chain14-2026-10-30.json`. Post-install readback:
`~/Documents/FM1 Backups/FM1-TEST-post-chain14-2026-10-30/`.
Installed source is `240bd0d`; package checksum above is unchanged.
