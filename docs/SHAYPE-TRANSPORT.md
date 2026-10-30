# SHAYPE: change RATE on the transport grid

While PLAY runs, changing SHAYPE's RATE joins the next available slot of the
new rate. The next pattern step follows the current one: no reset to step zero,
no skipped pattern steps to catch up, and no restart of its direction/pass.
Existing notes keep their scheduled gate ends. A changed rate does not replay
the previous step through KEY ATTACH NOW or the late-chord path.

PLAY / MIDI Start starts the pattern and all rate grids from zero. MIDI Continue
keeps their positions. The grids keep running with ARP off or without a chord,
so a later chord follows transport time. External clock advances them using the
same interpolated clock time as the sequencer; a stopped external clock does
not generate new arp slots. The existing stopped, freely played arp still starts
with its first chord. Its notes continue to release normally.

## Implementation

`firmware/src/seq.c` keeps eight small runtime clocks, one per RATE, independent
of each track's pattern index. A rate change moves only the scheduling phase to
the destination clock, preserving `arp_idx` and `arp_ofs`. Swing retains the
existing short/long order, with its parity taken from the transport grid rather
than the pattern index after switching rates. Tempo changes preserve fractional
clock phase; external clock uses its existing Q12 rescaling.

Periods are cached and recomputed on tempo changes. Ordinary blocks add and
compare clock positions; there are no new per-block divisions in that loop.
The runtime state is not serialized: songs, presets and FOR6 storage are unchanged.

## Verification

`tests/arp_seq_test.c` covers all 64 rate transitions at swing 0/50/100 (192
cases), including unchanged-rate playback, step/pass continuity, and balanced
note-offs. Two-track external-clock tests compare a changed track against one
running the destination rate from Start, with steady and wandering tempo. A
clock-wait test checks rate movement, Stop/Continue and Start. Existing
shapeChords parity, KEY ATTACH, releases, recording and transport soaks remain.
The new regression fails on pre-change `seq.c` and passes on the revised code.

Release package: `build/switchblade-0.1-wlgrid13.fwsc`. Hardware installation and
physical audition are separate checks; do not claim installed until verified.

Built as **v0.1-wlgrid13** on 2026-10-29. Package SHA-256:
`acd20a911638eb627f4935a74f173bd582eda2fe244047210a133c0b12580a66`.
Target build and disassembly budgets pass. Focused SHAYPE tests pass normally
and with the repository's UBSan exclusions for intentional DSP wraps. The host
suite's 157 golden renders are unchanged, with zero health, voice/routing or CPU
budget failures; other firmware checks pass. The suite invocation used
`SANITIZE=0` because ASan is not reliable on this host. Its text-spacing check
failed under system Python's Pillow without libraqm; the identical check passes
under `Felucca/.venv-build/bin/python` with libraqm. The full suite script itself
therefore exited 1; the failed environmental check was rerun individually.

## TEST installation — 2026-10-30

Elijah confirmed TEST stopped and approved installation. An unresponsive USB
connection recovered after the user's power cycle; the fresh backup captures
what was loaded after that cycle, without substituting an older song version.

Installed the exact package/hash above, then verified the rebooted serial
version `v0.1-wlgrid13` and editor identity. All 20 exposed objects (8 logical
objects and 12 native setlist slots), plus master-strip state, match the fresh
pre-install backup byte for byte. No runtime restore was needed. Playback is
stopped. SHOW remains glo10; GHOUL was untouched.

Private archive: `~/Documents/FM1 Backups/FM1-TEST-pre-grid13-2026-10-30.zip`.
Receipt: `build/install-grid13-2026-10-30.json`. Post-install readback:
`~/Documents/FM1 Backups/FM1-TEST-post-grid13-2026-10-30/`.
Physical listening and knob-driven rate-change audition remain with Elijah.
