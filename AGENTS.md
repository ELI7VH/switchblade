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
