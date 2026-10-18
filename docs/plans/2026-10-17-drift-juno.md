# Task 6.1 sub-plan: DRIFT, the Juno voice (2026-10-17)

Master plan: `docs/plans/2026-10-16-void-four-continuation.md` Task 6.1 (the brief), as amended by the controller's
context of 2026-10-17. The donor is `../ultramaster_kr106-main` (Ultramaster KR-106, GPL-3.0-only; read, never edited):
`KR106VCF_OPTIMIZED.h` (416 lines), `KR106Oscillators.h` (253), `KR106Resampler.h` (92), `KR106_HPF.h` (217), so a
sub-plan is required. Float, FPU on (Task 5.1); the in-tree example is ACID (`eng_acid.c` + `bass303.c` + `fastmath.h`).

## What lands

| File | Change |
|---|---|
| `firmware/src/dsp_va.c` | new. `va_ladder_*`: `kr106::VCF` in C (four TPT integrators, OTA tanh, one Newton step per stage, BA662 input/feedback ratio, the J106 resonance curve, the adaptive thermal noise, the pitch compensation); `va_hb_*`: the HIIR 12-coefficient halfband pair. No libm: `fastmath.h`. SPDX header names the donor files. |
| `firmware/src/eng_drift.c` | replaced: DCO, voice, parameter mapping, per-voice state in the part's engine memory. |
| `firmware/src/eng_obx.c` | **only** the dead ANALOG copy is removed (`analog_tap`, `analog_ft`, `ANALOG_K`'s dead uses, `analog_note_on`, `analog_osc`, `analog_render_f`). Nothing else. OB-Z's goldens must not move. |
| `firmware/src/engines.c` | `ENG_MEM_PART` unchanged in form (already the max of the three); `ENG_MEM_FITS("DRIFT", ...)` lives in `eng_drift.c`. |
| `tests/drift_ref_cfg.h`, `tests/drift_ref.cpp` | the host reference renderer (the donor's own `kr106::VCF` and the shared case table). |
| `tests/drift_test.c` | acceptance tests (a)-(i) + a bounded/NaN corner sweep + the halfband pair. |
| `tests/run_tests.sh`, `tests/target_budget.py`, `tests/target_budget.txt`, `tests/cpu_baseline.txt`, `tests/golden.txt`, `tests/union_golden.txt` | wire the test in; `drift_render` in FUNCS; the 32 `cpu/DRIFT/*` lines replaced (user decision of 2026-10-17, as ACID's in e2e47ec); DRIFT's goldens regenerated (changes, not new entries); the dead-ANALOG guard in run_tests.sh removed with the copy. |
| `presets/four/drift.py` | `e[1]` (was DTN, now PW) mapped onto the new meaning; nothing else. 32 presets stay (Task 7.2 converts the 43). |
| `LICENSES/GPL-3-KR106.txt`, `CREDITS.md`, `LICENSING.md` | licence + credit. |
| `docs/BASELINE.md`, `docs/HANDOVER.md` | ENG_MEM_PART and pool before/after; the baseline exception note. |

Not touched: `eng_wave.c`, `dsp.c`, `eng_acid.c`, `bass303.*`, `tests/ui_test.c`, the donor.

## Where the code is the source of truth, not the brief's prose

The brief and `docs/DSP_ARCHITECTURE.md` describe an older donor than the checked-out one. Where they disagree the
donor's code wins, because test (a) is "equals `kr106::VCF`", and each case is a reported deviation:

1. **Resonance.** The brief says `k = res * 4.5`. The code has `ResK_J106` (quartic fitted to hardware, knee, boost)
   or `ResK_J6` (+ `SoftClipK`). DRIFT is a Juno-106 (KTR is the J106's 1143 counts/octave), so `mJ106Res = true`.
   The "taper above 0.7" is the code's: at 1x oversampling `k` fades to 0 across normalised cutoff 0.7 -> 0.9.
2. **Oscillator. DECISION (user, 2026-10-17, fix round 1): the DCO follows the donor's code, not the brief.** The current
   `KR106Oscillators.h` has a *linear* saw (lines 187-196: the curvature seen in recordings was the interface's AC-coupling
   draining the ramp, not the DCO), no discharge blip, no sub RC filter (the sub is the half-rate square with its
   transitions smoothed by the shared BLEP only) and a J106-inverted pulse (`effPW = 1 - pw`); the pulse width range is
   0.5 .. 0.95 (`KR106Voice.h`). The first cut added the brief's (stale doc's) charge curvature `pos (1 + 0.15 (1 - pos))`,
   a discharge blip (0.1, decay 0.5) and a sub RC (0.45): the blip is a step with no BLEP, and the three cost 6 to 12 dB of
   inharmonic energy at MIDI 72-96 (FFT: -47 / -44 / -41 dB shipped against about -53 / -51 / -53 dB now; -57 for the
   reviewer's donor-like variant at 1x). All three are removed, and the pulse is inverted as the donor's J106. The gains are
   the code's J106 set (saw 0.6, pulse 0.5, sub 0.75). Remaining departures from the donor's oscillator are listed in the head
   of `eng_drift.c` (DCO at DRIFT_OS x the base rate, not 4x; the MIX law; auto PWM; a finished voice also resets the filter).
   Pinned by tests (j) inharmonic energy < -50 dB, (k) the spectrum and (p) the waveform against the ideal of the donor's
   amplitudes. Task 7.2 voicing can still decide by ear.
3. **Noise.** The brief lists the LCG noise with spectral tilt, but no edit slot controls a noise level. Not
   implemented (it would be dead code); the filter's own adaptive thermal noise (inside the ported VCF) is kept, and
   that is what starts self-oscillation.
4. **DRFT.** The donor's static offset is 12 cents x a sigma-1 sample (up to 36 cents), which cannot meet the brief's
   "wander <= +-8 cents". DRFT here: static offset (clamped unit sample) x 5 cents + three slow sines (0.07, 0.13,
   0.31 Hz, the donor's rates) averaged x 3 cents; the sum is at most 8 cents at DRFT 127.

## Parameter mapping (fixed here)

| Slot | Param | Mapping |
|---|---|---|
| E0 | MIX | `sub = clamp((64 - E0) / 48)`, `saw = E0 <= 64 ? clamp((E0 - 8) / 56) : (127 - E0) / 63`, `pulse = E0 <= 64 ? 0 : (E0 - 64) / 63`. 0: sub only; 64: saw only; 127: pulse only. Then the donor gains (saw 0.6, pulse 0.5, sub 0.75 x the RC) |
| E1 | PW | `E1 < 64`: pulse knob pw from `pwmin` (E1 = 64) to 0.95 (E1 = 0); `E1 >= 64`: pw = `pwmin`, plus an internal triangle PWM (0.8 Hz) whose depth grows 0 -> 0.4 of the knob range over 64..127. `pwmin` = 0.5 (the donor's, so E1 = 64 is a true square: odd harmonics only); the duty is `1 - pw` (J106 inverted, see 2). The track's SHAPE modulation (`m->shape`, LFO/ENV/matrix) adds to it too. Per-voice PW variance (+-0.02 at VAR 127) |
| E2 | DRFT | the voice's pitch drift above |
| E3 | VAR | per-voice deterministic offsets scaled by E3 / 127: cutoff +-5 %, pitch +-3 cents, VCA +-0.5 dB, VCF width +-10 cents/oct, PW +-0.02 (the doc's table; the donor's seeded LCG per voice index) |
| E4 | CUT | `CUTOFF_HZ` (the display law) interpolated at `E4 * 256 + m->cutoff`, then x key tracking, clamped 20 Hz .. 0.82 x Nyquist |
| E5 | RES | `res = E5 / 127` into the donor's J106 curve |
| E6 | DRV | input gain `1 + 24 (E6/127)^2` into the OTA, times the BA662 Q compensation `1 + 0.5 res^2 (E6/127)`; a matching output trim; DRV 0 is the donor's filter exactly |
| E7 | KTR | `ktr = E7 / 127`; cutoff Hz x `2^(ktr (pitch - 60) / 12)`, C4 (60) the zero point, 127 = 1:1 (the J106's 1143 counts per octave) |
| ENV (`P_ED_FLT`) | | voice.c's ENV -> cutoff, as OB-Z and WAVE (`ownenv = 0`) |
| `P_FTYPE` | | the 4-position HPF, `ft = min(P_FTYPE, 3)`: 0 flat (so every preset's default is neutral), 1 bass boost (+10.5 dB below ~60 Hz, biquad from the donor's `BassBoostFilter`), 2 HPF 236 Hz, 3 HPF 754 Hz (one-pole TPT). Per voice, linear, so it equals the donor's bus filter. The FILTER page is hidden for DRIFT (`ui.c page_visible`); no UI change here, so the HPF is reachable by MIDI/CC/preset only until Task 7.2 |

`poly` provisional 2; `ownenv = 0`; `knob = {P_E4, P_E5, P_ED_FLT, P_E6}` as now.

## Signal path (per voice, per block of CTL = 32 base samples)

`m->pitch16` -> Hz (`fm_exp2f`) x drift/variance cents -> `cps = Hz / (fs OS)`.
Per oversampled sample: DCO (polyBLEP saw/pulse/sub, 4th-order BLEP from the donor) -> post-DCO DC blocker (1.59 Hz TPT)
-> `va_ladder_tick` (x input gain) -> [OS 2: `va_hb_down` pair -> base rate] -> post-VCF DC blocker (base rate, same
corner) -> HPF -> x amp ramp (`m->amp0 -> m->amp1`, voice.c's ADSR) -> `out[i]`.
Coefficients: `va_ladder_coeffs` every `DRIFT_CTRL` base samples (default 8, so four per block) with the cutoff
interpolated linearly in Hz across the block (from where the last block ended); the held-note case hits the donor's own (frq, res) cache and costs nothing.
Voice start: a fresh voice (not retriggered) zeroes the ladder, halfband and DC states and seeds `s[0]` as the donor's
"filter startup" (`max(res - 0.7, 0)`); a retriggered one keeps them (no click).
Test seam: `DRIFT_IN(s)` replaces the oscillator output (the identity in the product), as bass303's `B3_IN`.

State: one `drift_vc_t` per voice in the engine memory, `DRIFT_MEM = sizeof(drift_vc_t) * DRIFT_POLY`,
`ENG_MEM_FITS("DRIFT", DRIFT_MEM)`; the voice index is `(v - t->v) % DRIFT_POLY`.

## Tests (`tests/drift_test.c`; `-ffp-contract=off` throughout)

Tier 1, the lifted filter held tight:
(a) every case of `drift_ref_cfg.h` (a 5x5 cutoff x resonance grid at OS 2, plus OS 1 and OS 4 and loud inputs):
    `max |y - y_ref| / max |y_ref| < 1e-4` over the whole buffer against `build/drift_ref/NN.f32`. The renderer drives
    the donor in the voice's own order (`TrackInputEnv`, `UpdateCoeffs` per base sample, `ProcessSample` OS times).
    *Gate: (a) passes before any `eng_drift.c` code is written.* The grid stops short of self-oscillation: there the
    output is a phase-accumulating oscillation and `fm_tanf`'s 1e-6 coefficient error (against libm's) is not a
    1e-4 amplitude error but a frequency error, which (d) checks as +-3 %.
Tier 2 (through `trk_note_on` / `track_render`):
(b) slope, (c) THD, (d) self-oscillation, (e) DRFT, (f) VAR, (g) MIX spectrum, (h) HPF corners, (i) CPU, as the brief.
Plus: the halfband pair (gain, delay, image rejection), and a bounded / finite output sweep of the corners
(RES 127, DRV 127, every MIX, extreme CUT/KTR, notes C0..C8).

RED: first with `dsp_va.c` absent (compile failure, captured); then with `dsp_va.c` in and the placeholder engine
(tier 1 passes, tier 2 fails, captured); then GREEN.

## CPU (test (i)) and the voice count

The target is a full four-track pattern at <= 80 % of one core on the device. The only instrument here is the host
instruction count (`regress.c` units, macOS `proc_pid_rusage`); `target_budget.py` counts the pi32v2 disassembly's
integer loops and cannot see FPU opcodes, so `drift_render` goes into FUNCS to guard structure only. The FPU's divide
cost is not visible to the host count either: the divides per sample are counted separately and reported.

**Restated in fix round 1** (the first derivation was wrong). It had 2137 (one part: `cpu/WAVE/27_CYBER_BASS`, six voices,
mix included) as the bound for `4parts_full` and for the DRIFT part, "no machine's worst case, and no full mix, may exceed
the heaviest part the project ships". That compares a whole mix with one part: `4parts_full` hands WAVE one voice (the shared
8-voice budget goes to the earlier parts), so it is not a worst case; `4parts_light` printed 2339, 9 % over the bound and
unasserted; and a legal arrangement (ACID 1 + DRIFT 1 + WAVE CYBER BASS 6) measured 3049, which was over it before DRIFT
existed. The host count cannot establish the 80 % claim (it counts a divide as one instruction, and no host figure maps to
the device's core), so test (i) no longer asserts it. It does two things:
- asserts DRIFT's own part cost (its dearest preset at the voice cap, mix included) against an absolute pin of +10 % of its
  measured worst, at `-O2` (regress/acid_test units), as ACID's (f) does for its voice;
- MEASURES and prints the dearest legal four-track mix it can construct (each engine's dearest preset, every split of the
  8-voice budget over one part per engine, each within its own voice cap) and `4parts_light` / `4parts_full`. The figure goes
  into `docs/BASELINE.md` as the number Task 8.1's PERF run must check on the device.

Voice count: start `DRIFT_POLY 2`, `DRIFT_OS 2`. If (i) fails, the brief's ladder in order: (1) OS 2 -> 1,
(2) POLY 2 -> 1, (3) VCF coefficient update every 4th sample (`DRIFT_CTRL`, the other way), (4) the plain explicit TPT
stage in place of the Newton step. The choice, the measured figures and any fallback go in the commit message and in
"Measured result" below. The hardware PERF run (Task 8.1) is the real gate and must confirm it.

## Goldens and budgets

DRIFT's golden entries (engine renders, any mix/union sequence containing DRIFT) are regenerated once, deliberately;
no OB-Z, WAVE or ACID engine hash may move (checked by diffing `tests/golden.txt` and `tests/union_golden.txt`). The 32
`cpu/DRIFT/*` lines are replaced; every other baseline line stays byte-identical. Never `BUDGET_UPDATE`.

## Measured result

Host instructions per sample, `-O2`, `tests/drift_test.c` (i) (the mix alone 359; before the task `4parts_full` read 1791):

| DRIFT_OS | DRIFT_POLY | `4parts_full` | DRIFT part alone (worst seed, HOOVER) | per voice |
|---|---|---|---|---|
| 2 | 2 (start) | 2966 | 1985 | 813 |
| 1 | 2 (ladder rung 1: OS 2 -> 1) | 2302 | 1268 | 455 |
| 2 | 1 | 2349 | 1221 | 862 |
| **1** | **1 (ladder rung 2: POLY 2 -> 1)** | **2018** | **863** | **504** |

**Chosen: `DRIFT_OS 1`, `DRIFT_POLY 1`.** Rung 1 alone leaves `4parts_full` 7.7 % over the 2137 bound, rung 2 puts it inside
(2018). Rungs 3 (coefficient update cadence: 8 -> 32 samples moves it by 17 instructions) and 4 (the explicit TPT stage) were
not needed and not taken. **DRIFT is monophonic as shipped**, a consequence of the bound derived above, not of a defect: the
ten KEYS seeds play one note. The bound is conservative (the heaviest configuration already shipped); Task 8.1's PERF run is
the real gate, and `DRIFT_POLY` / `DRIFT_OS` are the two macros to raise if it shows room. The six divides a sample (host: one
instruction each) are the cost most likely to be dearer on the device than here.

Before the choice, the ladder code was tightened without changing the sound: the stage written without the cancelling
linear parts (one divide, ~19 operations in place of 30), the three passes per segment with the filter state in registers, the
small-state series for the pitch compensation, the folded constants. That took OS 2 x POLY 2 from 3399 to 2966. The same
(a) bound still holds (worst 6e-6 of the peak against 1e-4).

Pinned: `DRIFT_COST_MAX` 950 = the dearest seed's 863 + 10 % (+/-1 run to run), at `-O2`. (The `DRIFT_MIX_MAX` 2137 bound is
gone, see "CPU" above. The OS / POLY choice stands as the user's decision: DRIFT mono until Task 8.1. Its table above was
measured against that bound, which is why it is a ranking of the four options and not a proof that the others are too dear.)

Fix round 1: the oscillator simplification (decision 2) took the dearest seed to 863-864 (HOOVER; the blip, the curvature and
the sub RC were ~0.5 % of the mix), `4parts_full` 2001, `4parts_light` 2324, the dearest legal mix 3283 (OB-Z 0 + WAVE
CYBER BASS 6 + ACID OVERDRIVE 1 + DRIFT HOOVER 1). `cpu/DRIFT/*` in `tests/cpu_baseline.txt` re-recorded (down ~2.5 %).

State: `sizeof(drift_vc_t)` 188 B (OS 1; 284 B at OS 2; was 196 / 292 before the blip and the sub RC state went). `ENG_MEM_PART` 800 B before and after (WAVE's 8 x 100 B), the pool
275,624 B before and after. Image 361,616 -> 366,128 B. RAM unchanged.

## Fix round 1 (2026-10-17): from the review of 89d4a18..55c58e2

- **DCO follows the donor** (decision 2): linear ramp, no blip, no sub RC, J106-inverted pulse, pw 0.5 .. 0.95.
- **Hidden P_FTYPE**: any value above DRIFT's four HPF positions (4 HP24, 5 NOTCH from another engine's filter, left on the
  track) is the FLAT position, not the 754 Hz cut. The FILTER page stays hidden for DRIFT; showing it with HPF labels
  (LP / BP / HP / LP12 would read wrongly) is a UI question, logged for later (Task 7.x).
- **Tests**: (j) inharmonic energy, (k) spectrum against the ideal, (p) waveform against the ideal, (l) PWM, (m) KTR,
  (n) the two DC blockers (-6 dB at 1.59 Hz), (o) voice start (a finished voice resets, a retrigger keeps the states),
  (h) flat fallback, (i) restated. Donor reset contract (`KR106Voice.h` Trigger): the VCF is never reset (an idle voice
  keeps running), the DCO resets only when the voice was idle, a retrigger keeps both. Ours resets a finished voice's
  filter as well, because the engine skips an idle voice and a stale resonance would burst out of its next note.
- **Tier-1 grid**: six OS 1 cases added (40 in all), since OS 1 is what ships.

## Task 6.3 (2026-10-18): three voices, exact ladder and DCO optimisations

The user: "push for 3 voices on the Drift machine". Basis: `.superpowers/sdd/drift-3voice-chorus-investigation.md` (read-only, measured), re-measured
on the tree after Tasks 4.4 / 4.5 (WAVE is single-oscillator since 4.4, so the dearest legal mixes moved: see below). Report:
`.superpowers/sdd/task-6.3-report.md`. Host instructions per sample, `-O2 -ffp-contract=off`, regress units; "marginal voice" = (3-voice part - 1-voice part) / 2.

**Landed (all exact to the donor within 1e-5; (a) reads 4.8e-6 of the peak against 6.0e-6 before, 40 of 40 cases under 1e-4):**
1. the small-signal ladder tick (`dsp_va.c va_ladder_tick`): while the last stage's `s3^2 < 0.04` and each stage's `xs^2 < 0.25` the Newton step is a short series
   in `w = xs^2` (coefficients made by `va_ladder_coeffs` from g1) and `1 + gnl = 1 / (1 - g1nl)`: no divide but the OTA's. Counted: every tick of the 32 presets
   (as shipped and with RES 127). A loud or self-oscillating filter takes the exact stage and its divides;
2. the k-cache in `va_ladder_coeffs`: `va_res_k`, the pull-fit exp, `fb_scale`, `1/fb_scale` only when k moved (bit-identical results);
3. `1/cps` once a block for `drift_blep` (the 4 divides a window hit become multiplies);
4. `va_ladder_tick` is `always_inline` (one caller in the firmware; tier 1 of `drift_test.c` also calls it, which kept it out of line and the state out of registers in
   that build only), and `va_ladder_run` writes back the four integrators and the noise seed only.

| per marginal voice, mean of 32 presets | before | after |
|---|---:|---:|
| host instructions / sample (CTRL 8) | 325.1 | **300.1** (-7.7 %) |
| divides / sample (investigation's counters) | 6.4 | 1.4 |
| `va_ladder_run` alone (test (a2): 0.3 noise, 1.2 kHz, RES 0.5) | 240 | **156** (-35 %) |

**`DRIFT_POLY 3`, `DRIFT_OS 1`, `DRIFT_CTRL 8` (one `#define` each in `eng_drift.c`; CTL must divide by CTRL).** The 3-voice part, dearest preset (FAT SAW BASS, mix
included; probe = a build that references nothing but the engine, harness = `drift_test.c (i)`, which carries the DRIFT_IN seam as a real call and whose figure moves a few
percent with which test functions are in the file, as the compiler's inlining does: re-pin from the same file):

| DRIFT_CTRL | marginal voice, before -> after | 3-voice part, probe, before -> after | harness (i) after |
|---:|---:|---:|---:|
| **8 (shipped)** | 325.1 -> **300.1** | 1495.8 -> **1427.1** | 1386 (before: 1654) |
| 16 | 316.3 -> 290.3 | 1469.0 -> 1397.7 | 1350 |
| 32 | 306.5 -> 268.7 | 1434.7 -> 1321.4 | 1313 |

CTRL 32 saves a further 10 % of a voice but adds about 9 dB of sideband on a bare sine under a deep, fast LFO on the cutoff (investigation 2.1: -55 -> -45 dB re the
signal; 8 -> 16 costs the same 8 dB for a third of the saving). That is the user's decision by ear; it stays 8. **No runtime lite mode** (not in this task).

Pins: `DRIFT_COST_MAX` 950 -> **1525** (= the harness 1386 + 10 %, `-O2`); new `LADDER_COST_MAX` 172 (= 156 + 10 %). Tests added: (a2) k-cache bit-exact, `va_ladder_run` = n ticks
bit for bit, the small-signal stage within 3.5e-5 of the nonlinear term and the exact stage bit for bit outside its range, the fast tick against the pre-6.3 tick within
5e-6 of the peak; (f) fixed (`hz[2][2 * DRIFT_POLY]`) with a per-voice spread check (any two of a part's 3 voices >= 0.3 % apart at VAR 127, one cutoff at VAR 0); (o) per
voice (a chord of POLY notes resets, a note past the cap steals and keeps every voice's state, MONO retriggers voice 0); a headroom test with every voice at its +-2 FS clamp.

**Headroom at three voices.** A voice is held to +-2 FS, a part's sum to 3 x 2 = 6 FS: x = 466,224 in `mix_part` (limit 524,287 for `x * gl`), +12 % margin; the 8-voice
budget gives 3 + 3 + 1 + 1 at the most, 16 FS-equivalents dry. With the chorus on, `chorus_part` holds each channel to the 5 FS guard (x <= 388,520). The new case drives every
voice into its clamp (100 x square through the DRIFT_IN seam), four DRIFT parts, LEVEL 127, every send 127, REVERB HALL SIZE 127 / DAMP 0: CHOR OFF part x 466,224,
|mix + wet| 1,743,640 (16.9 % under 2^21); CHOR I+II x 388,531, 1,714,720 (18.2 %). Nothing in DRIFT's clamp or the 5 FS guard had to move. (The same wet-bus exposure existed
for ACID at 6 FS and was closed separately: `ACID_GUARD_FS` 5, commit 63c0d0d.)

**Dearest legal four-track mixes** (8 voices, each engine's dearest preset, any engine on any part, `tests/drift_test.c (i)` / `docs/BASELINE.md`):

| mix (probe units) | POLY 1 (shipped before) | POLY 3 unoptimised | POLY 3 landed (CTRL 8) | CTRL 16 | CTRL 32 |
|---|---:|---:|---:|---:|---:|
| 2 x DRIFT FAT SAW BASS x3 + 2 x ACID OVERDRIVE x1 | | 4000 | **3864** | 3791 | 3650 |
| ... with CHOR I+II on both DRIFT parts | | | **4268** (II: 4258, I: 4247) | 4195 | 4053 |
| WAVE NEURO WOBBLE x5 + 3 x ACID OVERDRIVE x1 (no DRIFT) | **3481** | 3481 | 3481 | 3481 | 3481 |
| one part per engine: OB-Z TAPE STR x1 + WAVE NEURO WOBBLE x3 + ACID OVERDRIVE x1 + DRIFT FAT SAW BASS x3 | 2802 (DRIFT HOOVER x1, WAVE x5) | 3070 | 3002 | 2966 | 2895 |

The investigation's premise (the dearest mix has no DRIFT in it) no longer holds: WAVE became cheaper (Task 4.4), so the DRIFT-free worst fell from 3873 to 3481 and two
DRIFT x3 parts now top it by +383 (+11 %), +787 (+23 %) with the chorus on both. CTRL 32 would bring that to +169 (+4.9 %) / +572 (+16 %). The shed path (audio.c) is a backstop; Task 8.1's PERF
on the device is the gate, on the compositions in the report.
