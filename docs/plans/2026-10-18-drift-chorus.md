# DRIFT: a Juno chorus (Task 6.2) - sub-plan

Donor: `ultramaster_kr106-main/Source/DSP/KR106Chorus.h` (805 lines) + `BBDFilter.h` (GPL-3, credited in CREDITS.md). Read only.
Input from the profiling investigation (`.superpowers/sdd/drift-3voice-chorus-investigation.md`, section 4): the lean float
port (`chorus/juno_chorus.h` in its scratchpad) is taken as the DSP core: shared pre filter + saturation + one ring, two
Hermite taps, two post filters, CTE gain as an affine function of the delay, no divides. Its hook placement is NOT taken (see 2).

## 1. What is ported, what is trimmed (every departure stated, each measured against the donor)

Kept verbatim: three modes (OFF / I / II / I+II); centre delay 3.30 ms; per-line clock trim (delay0 x0.985, delay1 x1.015);
delay swing +-2.13 / +-1.71 / +-0.236 ms; LFO 0.514 / 0.842 / 7.85 Hz, triangle for I and II, sine for I+II; the antiphase pair
(line 0 `c + d*lfo`, line 1 `c - d*lfo`); the BBD pre/post filter (Butterworth 9661 Hz Q 0.7071 TPT SVF + one-pole 20 kHz, before the
line and after it); 4-point Hermite read; the charge-transfer gain `(1 +- 0.04)(1 - 4468 (1/clock - 1/40000))`; the summer
`out = dryMix*in + wetMix*wet`, kDry 0.863, kWet 1.257 (fade scales both); the 5 ms on/off + mode-to-mode crossfade.

Trimmed (inaudible or unaffordable): injected leakage noise, pink floor + shelf, 60 Hz rail ripple, the four biphasic clock-reset
clicks and the 30 Hz Q18 ring (all are noise-floor / click artefacts at -60 dB or lower; the donor's own multipliers can zero them,
which is how the reference is run); the second pre-filter + saturation + write (without injected noise both lines carry the same
input, so one is exactly equivalent); `tanh(0.12x)/0.12` as its odd polynomial (rel error < 1e-5 for |0.12x| <= 0.4, the clamp);
the donor's float phase accumulator as a 32-bit integer phase (exact rate, no float rounding of a 1e-5 increment); the donor's
smoothed depth (moot: the wet is faded to 0 across every switch).
Added: a level trim `CHORUS_TRIM = 1/sqrt(kDry^2 + kWet^2) = 0.6567` on both outputs, so CHOR on does not jump the part's level
(the donor's gain sum is 2.12, +6.5 dB at low frequencies, +3.7 dB power for broadband); the donor reference is scaled by the same
constant in the tests. A guard clamp at the float -> int boundary (below).

## 2. Where it lives: a per-part stereo hook in `mix_part`

A first option, a hook in `track_render` (after the voice loop), is rejected: it runs before DIST / INSERT / the SLICER / the FX
layer's mute, so the part's distortion would hit the chorus' output (a chorus is last on a Juno) and the mono sum it hands over
would have to fake a SPREAD pair. The chosen point is where the part's mono sum becomes stereo.

- `core.h`: one optional member `int (*part_fx)(struct track *t, int32_t *b, int32_t *z, uint32_t n)` of `engine_t`
  (NULL for OB-Z, WAVE, ACID: their path is the same code as today, bit for bit). `b` is the part's mono block after DIST, INSERT,
  SLICER and mute (as `mix_part` has it); the hook returns 0 and touches nothing, or returns 1 with `b` := the mid (L+R)/2 and
  `z` := the side (L-R)/2 of the chorus' stereo pair.
- `fx.c`: `part_fx()` wrapper (one table lookup + a NULL test), a static `chor_z[CTL]`, and ONE new function `mix_stereo(t, b, d, z, n)`
  (noinline, like `mix_spread`): the level / pan / sends / master-bus loop for a part that has a side signal. Call sites: `mix_part`'s
  non-SPREAD branch (after `perf_mute`), and `mix_spread` (after `perf_mute_sd`, before its own loop; it hands over `d`). The existing
  loops are untouched, so a part without a stereo stage runs the same code.
- Interaction with PAN: `L = (x + w) * gl, R = (x - w) * gr` (x the level-scaled mid, w the side, gl / gr PAN's law): the chorus' L / R
  pair is balanced by PAN exactly as a stereo source; PAN centre = the Juno's two jacks.
- Interaction with SPREAD (`P_SPRD`, voices left / right of PAN): the voice side `d` keeps its law (`y * gld`, `y * grd`), the chorus
  side is added on top of the mid with the mid's gains (`glm`, `grm`). So SPREAD still places voices, the chorus widens the sum;
  SPRD 0 and MONO / LEGATO parts work (the non-SPREAD path). The chorus' input is the mono sum of all voices.
- The sends (CHO / DLY / REV) and `perf_throw` take the mid, mono, as today; `t->peak` takes the louder channel.
- The hook is also what the follow-up polyphony task keeps: DRIFT's render loop is not edited.

## 3. The DSP file and the engine hook

`firmware/src/dsp_chorus.c` (new, float, FPU only, `fastmath.h`, no libm): `chorus_t` (zero state = OFF, coefficients are constants),
`chorus_block(c, mode, in, outL, outR, n)` (float in, float L / R out: what the tests drive), and `chorus_part(c, mode, b, z, n)`
(the int32 part block: Q15 -> float, the chorus, the guard clamp, mid / side). `eng_drift.c` gets only: `#include "dsp_chorus.c"`,
the state placement (`DRIFT_MEM` grows by `sizeof(chorus_t)`, after the voices), a 6-line `drift_part_fx` and `.part_fx = drift_part_fx`.
Activation clears the ring and the filters (an off -> on switch never plays stale audio) and starts the LFO at phase 0 (the donor's
Init state, so the reference is sample-aligned). A part with no voice sounding keeps calling the hook with a zero block until the line has rung
out (fix round 1: the first version dropped the tail and clicked at REL 0): `CHORUS_TAIL` = 280 samples (6.3 ms) after the last non-zero input = the longest delay 243.06 + 2 Hermite
taps + 1, + 2 x 16 samples of filter settle, rounded up to the 8-sample sub-block. Then the chorus stops (the next start is clean, as from a fresh state) and an idle part costs a
NULL test and a compare. Only parts with a `part_fx` hook are affected (OB-Z, WAVE, ACID return 0 at once).

## 4. Integer headroom

The hook's output feeds `x = ((b >> 2) * lvl) >> 10` then `x * gl` (limit x <= 524,287) and the four-track sum (< 2^21).
Guard at the float -> int boundary, as ACID's: each of L and R is held to `|s| <= CHORUS_GUARD_FS = 5` full scale (NaN -> 0; fix round 1: 6 FS left too little for the wet bus),
so `|mid| + |side| = max(|L|, |R|) <= 163,840` and `x <= 388,520` (LEVEL 127, 26 % margin on `x * gl`, 4 tracks 1.554 M of 2.097 M, 543 k for the wet bus; four guard-hit parts with every send 127 and HALL SIZE 127 measured 2.05 M).
DRIFT's own voice output is held to +-2 FS, so with POLY 1 the chorus never reaches the guard (2.12 * 0.657 * 2 = 2.8 FS);
the guard is for DIST (a mono sum up to 7 FS) and the 3-voice follow-up. Pinned by a test through the real `mix_block` (RES 127,
DRV 127, chords at max, LEVEL 127, I+II, four DRIFT tracks).

## 5. The parameter, its page, MIDI, presets, compatibility

- Id: `P_RSV_LN` (the first reserved id, "nothing reads them"), descriptor `PE("CHOR", N_CHOR, 0)`, names OFF / I / II / I+II,
  default OFF. FOR4 layout and P_COUNT do not change. Nothing treats the id specially except `motion_param` (excluded: a setting,
  like P_FTYPE) and the tests that pin it so.
- Compatibility: the reserved ids are stored by every saved project and user preset with their old default 127; `param_fit` would
  clamp that to 3 = I+II on every DRIFT track of an existing project. `param_fit` therefore sends an out-of-range value of this one
  descriptor to its default (OFF); the hook also reads anything outside 0..3 as OFF.
- Page: DRIFT's non-big-four params live on MACHINE 1 / MACHINE 2 (E0..E7, all eight slots used), so CHOR gets a new EDIT-family page
  "JUNO" after VOICE 3, `{P_RSV_LN}` alone, `page_visible` true for DRIFT only (as FILTER / FLT ENV are engine pages); ui_help line,
  EDIT-cycle test for DRIFT updated, `ui_render` stays at 0 findings.
- MIDI: no CC in `docs/MIDI.md` is free-by-nature (CC 93 is the CHORUS send, 91 / 94 are the other sends); none is added. CHOR is
  reachable with MIDI LEARN (it is a page parameter) and is not automatable (a setting).
- Presets: `presets/four/drift.py` gets `chor=` (0..3, default 0 when absent); `tools/four_presets.py` validates it and emits
  `DRIFT_PX[32]` (uint8); `drift_track_loaded(t)` (eng_drift.c) applies it, called from `ui.c apply_preset_to` and `tests/hostsim.c`
  next to `void_track_loaded`. Defaults: keys / pads / strings I or II, leads mostly I, basses OFF. Task 7.2 sets them from the
  KR-106 presets: `Source/KR106_Presets_JUCE.h` `values[44]`, index 26 kChorusOff, 27 kChorusI, 28 kChorusII (I = [27] only,
  II = [28] only, I+II = both).

## 6. Memory

`chorus_t` = 256-float ring (1024 B) + ~88 B state = ~1.1 KB, in the part's engine memory after the voice (zeroed on an engine claim, so a
switch to DRIFT starts clean and union_test hashes it). `ENG_MEM_PART` grows 800 -> `sizeof(drift_vc_t) * DRIFT_POLY + sizeof(chorus_t)`
(~1.3 KB, ~1.8 KB at POLY 3): +2.0 KB of pool in all (4 parts), against ~93 KB free; `.data + .bss` untouched. It cannot fit 800 B.

## 7. Tests (RED first)

`tests/chorus_ref.cpp` builds the donor's `KR106Chorus.h` (noise multipliers 0, as `drift_ref.cpp` does the VCF) over a case list in
`tests/chorus_ref_cfg.h`; `tests/chorus_test.c` (wired into run_tests.sh next to DRIFT):
OFF bit-identical (every DRIFT preset, CHOR 0 vs the hook compiled out, and the golden file); per mode: LFO rate within +-5 % of the
donor's (measured from the delay track of an impulse train), delay sweep range within +-10 %, L / R differ and the two delays are in
antiphase (correlation < -0.99), a sine's sidebands at +-LFO rate (level within 1.5 dB of the donor's); against the donor: time-domain
error of each mode on three signals, tolerance chosen after measuring (justified in the report); fade / mode-switch click bound;
guard clamp and the worst-case headroom through `mix_block`; CPU per part per sample, an absolute pin (+10 %, -O2); the cost of the
OFF path unchanged.

## 8. Order

1. This plan (commit). 2. Tests + reference, RED (commit). 3. `dsp_chorus.c`, the hook, `drift_part_fx` -> GREEN.
4. Param + page + help + `param_fit`, presets + generator + `drift_track_loaded`. 5. Goldens (only DRIFT presets with CHOR != 0 and the
mixes / union sequences containing them), `cpu/` lines, docs (MIDI.md, HANDOVER). 6. `./build.sh` (no soft-double), full suite.

## 9. Measured result (filled in after the build)

- Cost: `chorus_part` alone 177 (I), 177 (II), 182 (I+II) instructions per sample per part (tests/chorus_test.c, -O2, regress.c's units); in the
  mix (`cpu/DRIFT_CHOR/*`, DRIFT KEYS, over the OFF line 728): +199 / +202 / +195. The investigation's prototype measured 176 / 214: the 8-sample ramps make
  the sine mode as cheap as the triangle. The OFF path is the old code (mix_part: one NULL test per part).
- Memory: `chorus_t` 1,124 B in the part's engine memory (ring 1,024 B); `ENG_MEM_PART` 800 -> 1,312 B (+2,048 B of pool for the four parts); `.data+.bss`
  +128 B (`chor_z[CTL]`). Image 369,980 B; RAM 43,508 B of 73,728; pool 277,672 B of 368,640.
- Donor: rate and sweep inside 0.1 % of the constants, centre within 0.02 samples, sidebands within 0.1 dB, waveform error -63 .. -100 dB in 50 .. 100 ms,
  the long-term spectrum of noise within 0.01 dB. After 100 ms the waveforms part slowly: the donor's float LFO phase runs 0.1 % off its constant (0.5146 Hz
  for mode I), where the port's integer phase is exact. See the report.
