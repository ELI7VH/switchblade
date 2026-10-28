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
