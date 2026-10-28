# 128-step sequences

128 steps at 1/16 cover eight bars in 4/4, with the same step resolution as the
old four-bar maximum. Both melodic and drum tracks support it.

- Length choices: 2, 4, 8, 16, 32, 48, 64, 128.
- Melodic length: hold SEQ + K1. Drum length: K3 with no step held.
- Eight pages of sixteen steps. Drum K2 selects the page; melodic cursor navigation
  moves through all 128 steps.
- Division remains drum K4 or hold SEQ + K2. Hold SEQ + K3 controls swing.
- For longer arrangements, SONG chains the patterns in project slots A–D.
- Quantization OFF keeps the existing per-step nudge; individual timing per drum
  hit is a separate feature. This update increases pattern length, not timing precision.

## Save format and compatibility

FOR5 is 6400 bytes. It stores 128 nine-byte steps per track, 112 parameters,
128 four-byte automation/lock records, song sections, MIDI routing and the
existing per-track SHAYPE/drum state. Length is encoded as `length - 1` within
its biased parameter byte, supporting 128 without widening all parameters.
Automation places use nine bits: `track << 7 | step`.

Legacy FOR4 (3840 bytes) loads with its existing first 64 steps, timing, sounds,
drum masks and converted automation addresses; upper steps initialize empty.
New saves use FOR5. Older firmware cannot read FOR5: export a backup before a
downgrade. No automatic conversion back to a shorter format is provided.

The four RAM project caches move from retained NOINIT to zeroed POOL and reload
from flash at startup. The default flash-backed build preserves saved projects;
a RAM-only build no longer retains these caches across resets.

## Flash layout

Existing A/B base sectors remain in place. Extended project records use an
additional 4 KiB sector per A/B copy, in retired sample storage:

| Object | Extension range |
| --- | --- |
| Four project slots | `0xB8000..0xBFFFF` |
| Autosave | `0xC0000..0xC1FFF` |
| Twelve setlist songs | `0xC2000..0xD9FFF` |

This map is specific to this Switch Blade fork. User preset banks at
`0xDC000..0xDFFFF`, settings and all old song base sectors retain their addresses.
The inactive base and extension are erased, payload pages written, and the base
header committed last. One CRC covers both parts; a damaged/incomplete extension
falls back to the previous complete A/B copy. Small settings/preset objects keep
their original one-sector payload limit.

## Editor protocol

The old INFO step-count field remains 64 for existing clients. A new trailing
`58 01 00 01` tag advertises the 128-step capacity as a little-endian u14.
Existing step commands address indices 0–127 with their existing seven-bit index.
The legacy MOTION clear-locks operation 6 keeps `127 = all`; operation 9 clears
only the explicit step, including 127. Older browser editors remain limited to
the first 64 steps; use the hardware for the extended pages. Backup LIST/GET/PUT
report and carry the full 6400-byte project.

## Verification

- End-to-end host tests: all 128 steps play once, final-step recording/editing,
  accents, automation, old-format import, doubling, chain preparation and long
  arrangement-length metadata.
- Extended storage: 34 distinct extension sectors, every one of 6432 possible
  byte cuts preserves the previous valid copy; corrupt-extension CRC fallback.
- Standard project, motion, MIDI, editor, backup, persistence and sequence-tool
  tests cover the new wire size and addressing. No assertions are removed.
- Firmware UI renders include page 8/8, length 128 and held-step cards in both
  text sizes: zero lint or alignment failures.
- Core/storage boundary tests run under UBSan. ASan is excluded on this host,
  where its empty runtime probe was previously observed hanging.
- All 157 existing audio goldens and all 168 engine-switch sequences are unchanged.
- The full-suite run exposed old 64-step/3840-byte test assumptions, corrected
  and rerun individually. Font metrics were rerun using the same RAQM-capable
  Python environment that generated the fonts. The existing host MOD incremental
  cost threshold still fails; its DSP and threshold were not changed.

## Test-unit installation — 2026-10-28

Installed `v0.1-wl-128a` on the connected FM1-TEST. The installer confirmed
`FM-1_940` after reboot, and editor INFO returned `Switch Blade v0.1-wl-128`
(the 24-byte name limit). All four tracks were set to 128 steps at 1/16,
read back, and accepted final-step (index 127) queries. The fresh 3840-byte
running-project backup restored through the legacy importer before those edits.
A subsequent backup returned FOR5, 6400 bytes, with a valid CRC.

Code commit: `201d86a`. Package SHA-256: `aad3594265cb10ae124cf94b6f8a6de91d7dc9733844b8ca909f76a6aa139f13`.
Local receipt: `build/install-128-2026-10-28.json`; backup folders:
`build/backup-pre-128-2026-10-28/` and `build/backup-post-128-2026-10-28/`.
Software verification does not substitute for physical button use or listening.
