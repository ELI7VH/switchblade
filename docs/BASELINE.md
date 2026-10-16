# FM1-Four baseline (2026-10-16)

Fork point: Felucca 1.6 + VOID engine 14, copied from `../Felucca-main` (commit e73bcfe here).

## Build
- `./build.sh` (Docker Desktop must be running; docker CLI at /Applications/Docker.app/Contents/Resources/bin;
  Python deps in /tmp/x0xvenv: `source /tmp/x0xvenv/bin/activate`).
- app image 566,888 B of APP_SLOT 581,564 B (14,676 B free); package identity FM-1_900.

## R0 re-measurement (2026-10-16, this Mac, at adffc6e)

Confirms the handover's figures before the Phase 4-6 DSP work starts.

- app image **344,712 B** of APP_SLOT 581,564 B (236,852 B free); package identity **FM-1_940**.
- **RAM `.data+.bss` 43,380 B of 73,728 (30,348 B free; `build.py check()` floor is 4,096).**
- **Pool 275,496 B of 368,640 (93,144 B free; floor 8,192).**
  The handover and the original baseline recorded flash only. These are the numbers Task 6.1
  needs to size `DRIFT_POLY`: DRIFT is what grows `ENG_MEM_PART` (768 B today), and at
  `poly = 2` the four-part pool goes 3,072 -> ~7,104 B, which this headroom absorbs.
- `regress`: 141 golden renders, 0 changed, 0 gone, 0 CPU entries over budget.
- `ui_render`: 204 screens x 10 palettes, **0 lint findings**.
- Suite failures: only the two known environment ones below (perform FLANGER/PHASER, and the
  ASan probe aborting so the sanitizer stage is skipped).

## Host tests (`sh tests/run_tests.sh`, ~6 min) failures that exist before any FM1-Four change
These are environment failures on this Mac, not regressions. Later tasks may not add to this list.
1. `perform: FLANGER over REPEAT 1/16 ... PHASER ... FAIL` (perform_test cost check, host timing).
   Task 4.1 note: this check divides by the REPEAT 1/16 run, whose cost fell from +164 to +152 over
   idle when OB-Z lost its noise and drive, so both printed figures gained ~0.2 pp. The failure is
   still the pre-existing host-timing one -- no new failure was added -- but the Task 4.1 component
   is now inseparable from the host noise. Phase 5/6 should not read drift here as a regression.
2. AddressSanitizer `CHECK failed: sanitizer_malloc_mac.inc:189` aborts (macOS ASan runtime) in:
   the DRUM grid stress (#211) x2 and the SAMPLE/GRAIN/SLICE fuzz (fuzz_smp). Those tests are deleted
   with their engines in Task 1.1.
3. `tests/cpu_baseline.txt` was recorded on the Felucca author's machine; on this Mac 15 ANALOG/PHASE
   entries read 25-47 % over. It was re-recorded here on the unchanged code
   (`BUDGET_UPDATE=1 build/host/regress tests/golden.txt tests/cpu_baseline.txt`, golden untouched:
   167 renders, 0 changed) so CPU checks in this fork compare like with like.

## Focused commands
- regress: `cc -O2 -w -Ibuild/gen -Ifirmware/src -o build/host/regress tests/regress.c -lm && build/host/regress tests/golden.txt tests/cpu_baseline.txt`
