# Switch Blade

Read `../lucian-utils/AGENTS.md` for shared instructions and `../waveloop/fm1/README.md`
for unit ownership, firmware safety and the FM-1 handoff queue.

- This fork uses `waveloop/main`, remote `fork` (ELI7VH/switchblade). Never push to upstream `origin`.
- Build and test instructions: `BUILDING.md`. Build before running `tests/run_tests.sh`.
- The DRUM step grid lives in `firmware/src/ui_drum.c`, with drawing in `ui_graph.c`.
  It uses FOR4's existing step `hit`/`acc` bytes. Older note-based patterns remain valid.
- `tests/drum_seq_test.c` covers real panel input, sequencer and project paths;
  `tests/ui_render.c` renders/lints the drum grid for 808, CR78 and 909.
- Do not flash while the user is playing. Identify the unit and back up its songs first.
  The GHOULBOX is excluded; JIANT is parked while this drum-sequencer work is active.

- 128-step support uses FOR5 (6400 bytes), reading FOR4 (3840) through migration. Read `docs/128-STEPS.md` before changing storage, automation or the editor protocol.
- Motion places are 9-bit (`STEP_BITS`/`STEP_MASK`); never narrow to a byte. Single melodic UI holds use step+1, not a 64-bit mask.
- New boundary suites: `seq_128_test.c` and `storage_128_test.c`; the fixture is synthetic, not a device backup.

- Stopped, armed melodic step entry reserves held-note K1 for tie length. Release after stretching advances by note_len; other held knobs keep parameter locks. See `docs/NOTE-STRETCH.md`.

- `fm1-sb-live` master controls reserve Ch16 CC20–23 (repeat/dirt/delay/filter). The preset is `controllers/fm1-sb-live.minilab3`; see `docs/FM1-SB-LIVE.md`. Read-only editor command 82 exposes their live values.

- Live-recorded synth holds use FOR6 Q8 durations in the synth-only hit/acc bytes; FOR4/FOR5 import retains old timing. Read `docs/PLAYED-HOLDS.md` before changing gate/tie playback or project encoding.

- Target storage guards and host NOR tests share `hal/fm1_flash_regions.h`. Extended saves require B8000..D9FFF; never widen the entire retired-sample range. SAVE > SONG NAME debounces for 3 s; see `docs/SONG-NAMING.md`.

- Song switching/copying requires physical GLOBAL + OCT; plain HOME OCT changes pitch. See `docs/GLOBAL-SONGS.md`; capture song gestures before SET-layer UNDO, ARP OP shifts, action taps and pitch handlers.

- SHAYPE RATE changes join transport clocks without resetting pattern/pass position. Runtime-only clocks live in `seq.c`; see `docs/SHAYPE-TRANSPORT.md` and the 192 transition / external-clock cases in `tests/arp_seq_test.c`.
