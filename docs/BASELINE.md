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

## Task 6.1: DRIFT, the Juno voice (2026-10-17)

Measured on this Mac. Plan and derivation: `docs/plans/2026-10-17-drift-juno.md`.

- **`ENG_MEM_PART`: 800 B before, 800 B after** (WAVE's `VOID_MEM`, 8 x 100 B, is still the largest; ACID 764 B;
  DRIFT `sizeof(drift_vc_t)` 196 B x `DRIFT_POLY` 1 = 196 B; even OS 2 x POLY 2 would be 584 B). So the four-part
  engine pool is **3,200 B before and after**, and the brief's expected growth (768 B -> 7 KB) did not happen: DRIFT
  fits inside the region WAVE already sizes.
- **Image 361,616 B -> 366,128 B** of APP_SLOT 581,564 (+4,512 B, net of the deleted ANALOG copy); **RAM `.data+.bss`
  43,380 B of 73,728, unchanged**; **pool 275,624 B of 368,640, unchanged** (the 275,496 B of R0 grew by 128 B in
  Task 5.2, not here). `build.py check()`: no soft-double routines.
- **CPU (host instructions per sample, `-O2`, mix included, idle mix 359 / 337):**

  | DRIFT_OS | DRIFT_POLY | `4parts_full` | DRIFT part alone (HOOVER, the dearest seed) | per voice |
  |---|---|---|---|---|
  | 2 | 2 (the plan's start) | 2966 | 1985 | 813 |
  | 1 | 2 | 2302 | 1268 | 455 |
  | 2 | 1 | 2349 | 1221 | 862 |
  | **1** | **1 (shipped)** | **2018** | **863** | **504** |

  Bound: `4parts_full` and the DRIFT part each <= 2137 (the heaviest configuration the baseline already records,
  `cpu/WAVE/27_CYBER_BASS`, six voices). Only OS 1 x POLY 1 is inside it. Before this task `4parts_full` read 1791
  (1705 in the file) and the placeholder cost ~400 for its two voices.
  Where a voice's ~500 goes (OS 1): the filter ~230, the DCO ~70, the rest (DC blockers, HPF, amplitude, output, voice.c's
  share, coefficient updates) ~150. The filter does six divides per oversampled sample. **The host count sees a divide as
  one instruction; the device's FPU divide is dearer, so Task 8.1's PERF run is the real gate and the first thing
  to try raising (`DRIFT_POLY`, then `DRIFT_OS`) if it shows room.**
- `regress`: 141 goldens, 34 moved (all the 32 DRIFT presets, `song/4track_mix_four`, `slicer/song_gate_stut_four`;
  no OB-Z, WAVE or ACID engine hash moved), `union_golden.txt` 168 of 168 (every sequence plays DRIFT on part 3).

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

## Limits that sit at their value on purpose (Task 4.1, `tests/obx_test.c test_mode_bounded`)
Both read close to their limits, and that is not drift to chase:
- DC past MODE 0's: **0.045 of rms against the 0.05 limit.** This is the 4-pole at RES 127 (the soft knee
  clipping an asymmetric resonant waveform), not the morph. Tightening the limit, or anything that adds a
  little DC to the 4-pole, fails the test; loosening it hides a real DC regression. See HANDOVER section 7.
- Peak: **93146 against OBX_CEIL 94866**, the engine's own ceiling (soft_knee(., 16000) <= 32383, << 1,
  through voice_amp at full amplitude). The headroom is the knee's, 1.8 %, by construction.
