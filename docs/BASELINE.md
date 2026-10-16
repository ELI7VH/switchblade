# FM1-Four baseline (2026-10-16)

Fork point: Felucca 1.6 + VOID engine 14, copied from `../Felucca-main` (commit e73bcfe here).

## Build
- `./build.sh` (Docker Desktop must be running; docker CLI at /Applications/Docker.app/Contents/Resources/bin;
  Python deps in /tmp/x0xvenv: `source /tmp/x0xvenv/bin/activate`).
- app image 566,888 B of APP_SLOT 581,564 B (14,676 B free); package identity FM-1_900.

## Host tests (`sh tests/run_tests.sh`, ~6 min) failures that exist before any FM1-Four change
These are environment failures on this Mac, not regressions. Later tasks may not add to this list.
1. `perform: FLANGER over REPEAT 1/16 ... PHASER ... FAIL` (perform_test cost check, host timing).
2. AddressSanitizer `CHECK failed: sanitizer_malloc_mac.inc:189` aborts (macOS ASan runtime) in:
   the DRUM grid stress (#211) x2 and the SAMPLE/GRAIN/SLICE fuzz (fuzz_smp). Those tests are deleted
   with their engines in Task 1.1.
3. `tests/cpu_baseline.txt` was recorded on the Felucca author's machine; on this Mac 15 ANALOG/PHASE
   entries read 25-47 % over. It was re-recorded here on the unchanged code
   (`BUDGET_UPDATE=1 build/host/regress tests/golden.txt tests/cpu_baseline.txt`, golden untouched:
   167 renders, 0 changed) so CPU checks in this fork compare like with like.

## Focused commands
- regress: `cc -O2 -w -Ibuild/gen -Ifirmware/src -o build/host/regress tests/regress.c -lm && build/host/regress tests/golden.txt tests/cpu_baseline.txt`
