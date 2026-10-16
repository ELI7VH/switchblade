# VOID FOUR: continuation plan (review 3.3/3.4 → release)

> **For agentic workers:** REQUIRED SUB-SKILL: use `superpowers:subagent-driven-development` (the user's standing choice, answer 5) to implement this plan task by task. Steps use checkbox (`- [ ]`) syntax.

**Goal:** take VOID FOUR from HEAD `6b6fb26` (UI complete, two machines placeholder) to a signed-off v0.1 with four real machines, 128 auditioned presets and measured hardware performance.

**Architecture:** this plan does not replace `docs/plans/2026-10-16-fm1-four-machine.md`; it **supersedes its Phases 4–9 task bodies** where the findings below contradict them, and adds the tasks that plan is missing. Everything else in the master plan — global constraints, decisions D1–D9, the machine contract, the CC map, the screen designs — still binds.

**Tech stack:** C99 unity build (`firmware/src/felucca.c`), JieLi pi32v2 clang 4.0.1, fixed-point Q15/Q31 DSP, Python generators (`tools/`), host C tests built with `cc`.

---

## Global constraints (unchanged, restated because every task inherits them)

- Exactly four machines, 32 factory presets each. No drums, no noise, no sample engine.
- `engine_t.knob[4] = {P_E4 CUT, P_E5 RES, P_ED_FLT ENV, machine control}`. **CUT stays E4 and RES stays E5 in every new engine.**
- New common params go immediately before `P_E0`. `P_COUNT` is currently 112.
- Identity `FM-1_940`, storage format `FOR4`, version v0.1.
- MIDI out is USB only. The TRS jack stays input-only.
- `ui_render` must report **0 findings**; its findings fail the suite.
- Never run `GOLDEN_UPDATE=1` or `BUDGET_UPDATE=1` to hide a regression. Regenerate only after a deliberate sound change and state the reason in the commit message.
- Never flash the FM-1 without the user's explicit go-ahead **in that session**, with the device connected.
- Target: a full four-track pattern at ≤ 80 % of one core on hardware.

---

## Findings from reading the tree (2026-10-16)

These were verified against the source, and three of them change the work.

**F1 — The FM-1 has a single-precision FPU, and FM1-Four is not using it.**
`fm1-x0x-main/tools/build.py:42` builds with `FPU = ["-mcpu=r3", "-mfprev1", "-ffp-contract=off"]` and its link step asserts no soft-double routines crept in. `FM1-Four/tools/build.py:42` has no FPU flags: `CFLAGS = ["-Os", "-ffunction-sections", "-fno-builtin", "-Wall", "-Wno-unused-function"]`. `voice.c`, `dsp.c`, `eng_obx.c` and `fx.c` contain zero float or double. So VOID FOUR is integer-only today **by inheritance from Felucca, not because the hardware lacks an FPU.** This is the single biggest lever on Phase 5 (see Task 5.1).

**F2 — The ACID donor is not Open303; it is `bass303`, and it already runs on this chip.**
The master plan's Task 5.1 says to port `rosic_TeeBeeFilter` / `rosic_BlendOscillator` from `rosic_Open303.cpp`. **Open303 is not in this tree and neither is schwung-303.** What *is* in the tree is `fm1-x0x-main/firmware/src/dsp/bass303.{c,h}` (729 + 160 lines): a complete, no-libm, single-precision port of Open303 with Devilfish ranges, Soft/RAT drive, polyBLEP saw and 303 square, polyphase halfband oversampling (`BASS303_OS` 1/2/4), control-rate coefficient decimation (`BASS303_CTRL`), an idle hold-off, slide and accent — plus `tests/host/bass303_test.c` (683 lines) and `tests/host/run_bass303.sh`. It is GPL-3.0-only with the Open303 parts MIT, and FM1-Four is GPL-3.0-only, so the licence is compatible. It is also by the same author, on the same hardware. **Porting from `bass303` instead of from Open303 removes the largest unknown in the project.**

**F3 — A validated fixed-point tanh ladder already exists.**
`eng_wave.c:242 void_ladder()` is a four-stage tanh one-pole ladder with unit-delay feedback in Q15, using `VOID_TANH1`, already checked sample-for-sample against the VOID WAVE plugin by `tests/void_test.c`. It was the best fixed-point starting point available when this plan was first written. **Superseded on 2026-10-16 by D10 and D13:** with the FPU on and the KR-106 donor chosen, Task 6.1 ports the donor's float VCF instead, and `eng_wave.c` is left alone so WAVE's golden hashes cannot move. F3 is kept here because it remains the fallback if the float port fails to fit.

**F4 — The handover's claim about `engine_t` is wrong.**
`docs/HANDOVER.md` §6 says "The `engine_t` has `ownenv`, `amp` and `done` hooks for this." It does not. `core.h:161-180` defines exactly: `name`, `page_title[2]`, `edit[8]`, `presets`, `npresets`, `knob[4]`, `poly`, `keep`, `note_on`, `render`, `desc`, `mem`. `grep -rn 'ownenv' firmware/src/` returns nothing. ACID needs its own MEG/VEG (the 303's envelopes are the instrument), so **extending `engine_t` and `voice.c` is unbudgeted work that must happen before ACID** — Task 5.0 below.

**F5 — There is no FFT helper on the host.**
`tests/hostsim.c` has none. Every acceptance test in Phases 4, 5 and 6 is spectral. Build it once in Task 4.1 as `tests/dspfft.h` and reuse it, rather than three copies.

**F6 — Per-part engine memory is the right home for ACID.**
`ENG_MEM_PART` is the max of `VOID_MEM`, `ACID_MEM`, `DRIFT_MEM` (`engines.c:21`), and `VOID_MEM = sizeof(void_vc_t) * NVOICE`. ACID is `poly = 1`, so one `bass303_t`-shaped struct per part fits the model exactly. DRIFT is `poly = 2` and its ladder state (4 stages + drift + variance) fits in `voice_t.s[8]`, which is 8 × `int32_t`.

---

## Decisions this plan needs from the user

Three of them. **D10 blocks Task 5.1 and nothing else; D11 and D12 can be answered any time before their task.**

| id | Question | Recommendation |
|---|---|---|
| ~~**D10**~~ | ~~ACID: enable the FPU, or stay integer-only?~~ | **ANSWERED 2026-10-16: enable the FPU and use the x0x `bass303` engine.** Task 5.1 still runs as a measurement (image size, golden stability, cost per sample), but as a check on a decision already taken, not as a gate on it. Task 5.2 takes **branch A** only; branch B is dead and is kept in this document solely as the fallback if the spike shows the image will not fit. |
| **D11** | The handover's pending question: should the PRESETS encoder on non-HOME pages stay within the track's machine, as it does on HOME? | **Yes, make it consistent.** Today PRESETS on non-HOME pages walks all four machines and can switch the machine out from under the page you are editing. Answer before Task R1's fixes land, since it is a UI change. |
| **D12** | Phase 9's web editor step — still deferred out of v0.1? | **Keep it deferred** (the user's answer 3 already says so; confirming because Phase 9 is where it would land). |
| ~~**D13**~~ | ~~Juno machine: WAVE's slot or DRIFT's?~~ | **ANSWERED 2026-10-16: DRIFT's slot. WAVE stays.** Task 6.1 is rewritten below around the KR-106 donor with a host reference renderer. |

---

## The Juno question (asked 2026-10-16)

**Verdict: yes to a Juno machine, no to it replacing WAVE. Put it in DRIFT's slot.**

### The donor

`../ultramaster_kr106-main` is Ultramaster KR-106, a Roland Juno-6 / 60 / 106 emulation under
**GPL-3** (compatible with this tree's GPL-3.0-only). It is unusually well suited to being ported:

- `Source/DSP/KR106VCF_OPTIMIZED.h` (416 lines) is a **self-contained** `struct kr106::VCF` —
  its only includes are `<cmath>`, `<algorithm>`, `<cstring>`, `<cstdint>`, no JUCE — with
  `SetOversample(1|2|4)`, a control-rate `UpdateCoeffs(frq, res)` and an audio-rate
  `ProcessSample(input)`. It compiles and runs on the host as-is.
- `docs/DSP_ARCHITECTURE.md` gives the algorithms and the exact constants: the DCO charge
  curvature `pos * (1 + 0.15 * (1 - pos))`, the discharge blip, the CD4013 sub-oscillator with
  its RC rolloff (`coeff 0.45`), the Pade tanh `x*(27+x^2)/(27+9*x^2)`, the resonance law
  `k = res * 4.5` and its taper above 0.7.
- `docs/vcf_modulation_budget.md` has hardware-measured keyboard tracking (C4 zero point,
  1143 DAC counts per octave, 248 Hz self-oscillation calibration).
- 240 factory presets with `preset_categories.csv` (Bass 31, Piano/Keys 33, Pad/Strings 24,
  Brass 13, Lead 11).

### Measured cost

`KR106VCF_OPTIMIZED.h` compiled standalone against a reimplementation of `eng_wave.c`'s
`void_ladder`, 4M samples each, `-O2 -ffp-contract=off`, loop overhead subtracted:

| Filter | ns/sample | vs the integer ladder |
|---|---|---|
| `void_ladder` (Q15, 4 tanh one-poles) | 40.6 | 1.00× |
| Juno VCF, OS = 1 | 110.3 | **2.72×** |
| Juno VCF, OS = 2 | 207.1 | **5.10×** |

And the **complete voice** — `kr106::Voice<float>` in polyBLEP mode with a real patch (saw +
pulse + sub + noise, cutoff 1.2 kHz, res 0.7, env and keyboard tracking on, ADSR open):

| Full voice | ns/sample | vs the integer ladder |
|---|---|---|
| Juno voice, OS = 1 | 112.8 | **2.31×** |
| Juno voice, OS = 2 | 216.3 | **4.43×** |
| Juno voice, OS = 4 | 421.2 | **8.63×** |

The render loop has **no amplitude-dependent early-out** (unlike `bass303`, which has an
explicit idle hold-off), so these are the real per-sample costs: the loop runs the same work
every frame. Cost scales linearly with the oversampling factor, as expected.

Host ratios, not target cycles — but the op count is what dominates, and the shape of the
answer is clear: **a 6-voice Juno is not happening on this chip.** Plan for `poly = 2` at
OS = 2, or `poly = 4` at OS = 1, and let Task 6.1's test (f) decide. The existing fallback
ladder in Task 6.1 still applies, with "drop to OS = 1" as the first move.

### The whole DSP layer is JUCE-free

All ten headers in `Source/DSP/` — `KR106Voice.h`, `KR106Oscillators.h`, `KR106ADSR.h`,
`KR106_HPF.h`, `KR106VCA.h`, `KR106Noise.h`, `KR106AnalogNoise.h`, `KR106Resampler.h`,
`KR106VCF_OPTIMIZED.h`, `KR106Chorus.h` — include nothing but `<cmath>`, `<algorithm>`,
`<cstring>` and `<cstdint>`. The voice compiles and runs on the host with a three-line harness.

This matters more than the cost numbers: it means **Task 6.1 gets the same objective testing
Task 5.2 gets.** The donor voice becomes a reference renderer on the host, exactly as
`tools/void_ref.cpp` is for WAVE, so DRIFT's filter can be asserted to within 1 dB of the
Juno rather than judged by ear.

One caveat found while benchmarking: the **wavetable oscillator path (`mOscMode = 0`) faults
standalone** — it needs tables the plugin builds above the DSP layer. The polyBLEP path
(`mOscMode = 1`) is self-sufficient, and it is the one an FM-1 port would use anyway, since
the whole point is modelling the DCO rather than replaying a table.

### Memory

| Struct | Bytes | Note |
|---|---|---|
| `void_vc_t` × `NVOICE` (8) = `VOID_MEM` | **768** | today's `ENG_MEM_PART` |
| `bass303_t` at `BASS303_OS 2` | **752** | **fits with 16 bytes to spare** |
| `kr106::Voice<float>` | **888** | per voice, donor struct untrimmed |

So **ACID costs nothing in engine memory** — `ACID_MEM` 752 slots under the existing 768-byte
region and `ENG_MEM_PART` does not grow. That is a free win for Phase 5.

The Juno is the opposite. At `poly = 2` it needs 1,776 bytes per part, raising `ENG_MEM_PART`
from 768 to 1,776 and the four-part pool from 3,072 to 7,104 bytes; at `poly = 4`, 14,208
bytes. The 888 is the donor's full struct and a port would trim it hard — wavetable
oscillator state, scope sync, the J106 firmware-tick DAC emulation, portamento and the
variance metadata are all droppable — but budget for the pool roughly doubling.

**`docs/BASELINE.md` records flash only, no RAM figure**, so the headroom this has to fit in
is currently unknown. Measure it before committing to a voice count; Task 8.1 already calls
for recording RAM and pool headroom, and that measurement should move earlier, into Task 5.1's
spike.

### Why not WAVE's slot

Replacing WAVE costs real, finished work and narrows the instrument:

- **It discards four tasks that are done and reviewed.** `eng_wave.c` (531 lines) validated
  sample-for-sample against the VOID WAVE plugin by `tests/void_test.c` and `tools/void_ref.cpp`;
  `tools/gen_void_tables.py`; the `void_px_t` / `WAVE_PX` preset-extras mechanism that is wired
  into the preset schema and `tools/four_presets.py`; and `presets/four/wave.py`, 32 presets
  distilled from the 69 converted VOID WAVE originals. Tasks 1.1, 1.2 and 1.3 all touched this.
- **It deletes the only digital machine**, and with it the ATMOS category (12 of the 128
  presets). A Juno makes warm pads; it does not make scanning, formant or spectral
  atmospheres. Nothing else in the lineup covers that ground.
- **It makes three of four machines the same kind of synth.** OB-Z, Juno and DRIFT would all
  be polyphonic subtractive voices behind a resonant lowpass, all competing for brass,
  strings, keys and pads.

### Why DRIFT's slot

- **Nothing is lost.** `eng_drift.c` is 20 lines of placeholder borrowing `analog_render_f`.
- **The donor is DRIFT's spec.** Task 6.1 asks for "a 4-stage ZDF ladder with `TANH_Q15` per
  stage, solved with one Newton step per sample, 2× oversampled". KR-106's VCF is four
  cascaded 1-pole TPT integrators with OTA-saturated feedback, one Newton-Raphson iteration
  per stage, 2× oversampled. That is the same filter, already written and calibrated against
  hardware.
- **DRIFT's parameters map one-to-one onto Juno features**, which is not a coincidence — both
  are describing an analogue poly: E2 DRFT → DCO drift; E3 VAR → KR-106's per-voice variance
  (it has exactly 5 variance parameters); E6 DRV → the BA662 OTA saturation and Q compensation;
  E7 KTR → the documented C4-zero keyboard tracking.
- **The categories already match.** DRIFT is BASS 12 / LEAD 10 / KEYS 10; the donor's preset
  bank is strongest in exactly Bass, Piano/Keys and Pad/Strings, so Task 7.2 can derive from
  authentic Juno patches instead of writing 32 presets blind — the same advantage WAVE got
  from its 69 converted originals.
- **The name still fits.** DRIFT is what a Juno DCO does.

### What this changes in the plan (settled 2026-10-16)

Task 6.1 keeps its slot, its acceptance tests and a fallback ladder; its donor and its sound
change from "Diva-like, hand-rolled" to "KR-106 Juno". The decisions taken with it:

- **Display name stays `DRIFT`.** It is already in the preset names and the category tables,
  the word describes what a Juno DCO does, and renaming is a one-line change later if the
  user prefers `JUNO`.
- **DRIFT is ported in float, not Q15.** D10 turned the FPU on for ACID, and the same applies
  here: `KR106VCF_OPTIMIZED.h` is single-precision and self-contained, so it transfers almost
  verbatim instead of being hand-converted. This is a simplification, not a compromise.
- **`void_ladder` is no longer the starting point.** Finding F3 proposed reusing
  `eng_wave.c`'s Q15 ladder because nothing better existed in fixed point. With the FPU on and
  a hardware-calibrated donor available, moving `void_ladder` into a shared module would put
  WAVE's golden hashes at risk for no gain. **`eng_wave.c` is not touched by Task 6.1.**
- **E1 changes from `DTN` to `PW`.** A Juno has one DCO per voice, so detune has nothing to
  detune; pulse width and PWM are what belong in that slot.
- **The 4-position HPF maps onto the existing `P_FTYPE`**, not onto an edit slot. `P_FTYPE` is
  already a common parameter read by OB-Z and WAVE and it has six values, so the Juno's four
  HPF positions fit without touching the 8-slot contract.
- **The BBD chorus (`KR106Chorus.h`, 805 lines) is out of v0.1.** It is a global effect in the
  real instrument, this tree already has a chorus send and a per-track INSERT
  (`P_ITYPE`/`P_IA`/`P_IB`/`P_IC`/`P_IMIX`), so it belongs in the backlog as a later insert
  type, not in the critical path.
- **`LICENSES/` gains the KR-106 GPL-3 notice**; `CREDITS.md` credits Ultramaster KR-106
  (kayrockscreenprinting) alongside Open303 and Felucca.

---

## File structure

| File | Status | Responsibility |
|---|---|---|
| `tests/dspfft.h` | create (4.1) | Shared host FFT/spectrum helper for every DSP test. |
| `firmware/src/eng_obx.c` | modify | Add the SEM morph, the 4-pole cascade and slop. |
| `tests/obx_test.c` | create (4.1) | OB-Z spectral and slop acceptance tests. |
| `firmware/src/eng_wave.c` | modify (4.2) | Add the DC blocker. |
| `firmware/src/core.h` | modify (5.0) | `engine_t` own-envelope hooks. |
| `firmware/src/voice.c` | modify (5.0) | Honour the hooks in the amp path. |
| `tools/build.py` | modify (5.1) | FPU flags and the soft-double link guard, if D10 says yes. |
| `firmware/src/eng_acid.c` | replace (5.2) | The real 303 voice. |
| `firmware/src/fastmath.h` | create (5.2, branch A) | Lifted from `fm1-x0x-main/firmware/src/dsp/fastmath.h`. |
| `tests/acid_test.c` | create (5.2) | 303 acceptance tests. |
| `tests/acid_ref/` | create (5.2) | Reference vectors rendered from the `bass303` donor on the host. |
| `firmware/src/dsp_va.c` | create (6.1) | Float VA primitives for DRIFT: the TPT ladder, the HIIR halfband pair. |
| `tests/drift_ref.cpp` | create (6.1) | Host reference renderer built against the KR-106 donor. |
| `tests/drift_ref_cfg.h` | create (6.1) | The shared case table both the reference and the test include. |
| `LICENSES/GPL-3-KR106.txt` | create (6.1) | Donor licence notice. |
| `firmware/src/eng_drift.c` | replace (6.1) | The DRIFT voice. |
| `tests/drift_test.c` | create (6.1) | DRIFT acceptance tests. |
| `tests/audition.c`, `tools/audition_index.py` | create (7.1) | Preset renderer and the rating page. |
| `presets/four/*.py` | rewrite (7.2) | The 128 final presets. |
| `LICENSES/`, `CREDITS.md`, `README.md`, `docs/MIDI.md` | modify (9) | Release documentation and attribution. |

---

## Order of work, and why

```
R0 baseline  →  R1 review 3.3+3.4  →  4.1 OB-Z  →  4.2 WAVE DC  →  5.0 env hooks
    →  5.1 FPU spike (D10 gate)  →  5.2 ACID  →  5.3 accent/slide  →  6.1 DRIFT
    →  7.1 audition  →  7.2 presets (user gate)  →  8.1 hardware (user gate)  →  9 release
```

DRIFT stays last of the three DSP phases because it carries the most CPU risk and it is the one with a documented fallback ladder. ACID comes before it because the halfband pair ACID needs is the same one DRIFT needs — writing it once under ACID's tests means DRIFT inherits tested code.

---

## Task R0: Re-establish the baseline

**I did not run the build or the test suite while writing this plan.** Every number below the line comes from `docs/HANDOVER.md` and `.superpowers/sdd/progress.md`, not from a run in this session. Confirm them before trusting them.

- [ ] **Step 1: Build.**

```bash
cd "/Users/ollie_nicklin/Documents/Projects/VOID SYNTH/FM1-Four"
open -a Docker
export PATH="/Applications/Docker.app/Contents/Resources/bin:$PATH"
[ -d /tmp/x0xvenv ] || (python3 -m venv /tmp/x0xvenv && /tmp/x0xvenv/bin/pip install Pillow fonttools numpy mido python-rtmidi)
source /tmp/x0xvenv/bin/activate
./build.sh
```

Expected: `app ... felucca.bin 344712 B` (±) and `identity FM-1_940`.

- [ ] **Step 2: Full suite.**

```bash
sh tests/run_tests.sh
```

Expected: the **only** failure is `perform: FLANGER ... PHASER ... FAIL`. Then verify that claim rather than believing the summary line — the suite has reported a pass with a stage silently skipped before:

```bash
grep -c . build/ui_new/report.txt      # expect 0 findings
```

- [ ] **Step 3:** If anything else fails, stop and report to the user before starting R1. Do not begin DSP work on a red tree.

---

## Task R1: Review 3.3 + 3.4 (the outstanding gate)

Tasks 3.3 and 3.4 are implemented (`b71241d`, `adffc6e`) and **never reviewed**. Nothing downstream should start until this gate closes.

- [ ] **Step 1: Make the diff package.** `scripts/review-package` lives in the subagent-driven-development skill directory.

```bash
scripts/review-package 5f23c16 adffc6e
```

- [ ] **Step 2: Dispatch a spec review and a quality review** (two separate fresh subagents, per the user's standing process). Disclose these three concerns to each judge so they are assessed rather than rediscovered:
  - The sound panel is 80 px instead of 82, so the footer lines up with the EDIT page.
  - The editor's MENU id 23 (the old HOME view row) is now unknown, leaving 36 menu items instead of 37.
  - Most of the pruning had already landed in earlier tasks, so the diff is smaller than the task implies.
- [ ] **Step 3: Look at the renders yourself**, not only at the judges' reports: `build/ui_new/GREY/home_big.png` and `build/ui_new/GREY/home_big_t3.png`.
- [ ] **Step 4:** Fix every Critical and Important finding, then re-review. Log Minor findings in `.superpowers/sdd/progress.md`.
- [ ] **Step 5:** If the user has answered **D11** yes, implement the PRESETS-encoder change in this task (it is a UI change and belongs with the UI review, not stranded in Phase 9).
- [ ] **Step 6: Commit** `fix(ui): Task 3.3+3.4 review findings`

---

## Task 4.1: OB-Z — SEM morph, 4-pole, slop

**Files:** create `tests/dspfft.h`, create `tests/obx_test.c`, modify `firmware/src/eng_obx.c`.

**Machine params (edit[8]) — this is the contract later tasks read:**

| Slot | Param | Range |
|---|---|---|
| E0 | WAVE | enum SAW SQR TRI SIN PWM SYNC SUB (unchanged) |
| E1 | DTN (K4) | 0..127 |
| E2 | MIX | 0..127 |
| E3 | PW | 0..127 |
| E4 | CUT | 0..127 |
| E5 | RES | 0..127 |
| E6 | **MODE** | 0..127: LP → NOTCH → HP → BP morph, 4-pole above 120 |
| E7 | **SLOP** | 0..127 |

**Interfaces produced:** `tests/dspfft.h` exposing

```c
/* magnitude spectrum of n samples (n a power of two) into mag[n/2]; float on the host is fine */
static void fft_mag(const int32_t *x, unsigned n, float *mag);
/* energy in dB of the bins spanning [lo_hz, hi_hz] at sample rate fs */
static float band_db(const float *mag, unsigned n, float fs, float lo_hz, float hi_hz);
```

Tasks 5.2 and 6.1 include `tests/dspfft.h` and call exactly these two functions.

- [ ] **Step 1: Write `tests/dspfft.h`** with a plain radix-2 iterative FFT and the two helpers above. No test logic in this header.
- [ ] **Step 2: Write the failing tests** in `tests/obx_test.c`:
  - `test_mode_lp`: MODE 0, CUT at 1 kHz, render a 100 Hz saw for 1 s → `band_db(3500,4500)` is ≥ 20 dB below `band_db(400,600)`.
  - `test_mode_hp`: MODE 64 → the inverse inequality.
  - `test_mode_notch`: MODE 32 → the bin at cutoff is ≥ 15 dB below the mean of its two neighbouring octaves.
  - `test_mode_4pole`: MODE 127 vs MODE 0 at the same CUT → the magnitude at 4× cutoff is ≥ 10 dB further down.
  - `test_slop_deterministic`: SLOP 0 rendered twice gives byte-identical output.
  - `test_slop_spread`: SLOP 127, two voices on the same note → outputs differ, and the measured pitch spread is ≤ ±6 cents.
  - `test_dtn`: DTN 0 → 1 changes the render hash; DTN 127 gives two spectral peaks 20–40 cents apart.
- [ ] **Step 3: Run them and confirm RED.** Capture the actual failure output; do not skip this and do not accept an implementer's word that it failed.

```bash
cc -O1 -w -Ibuild/gen -Ifirmware/src -Itests -o build/host/obx_test tests/obx_test.c -lm && build/host/obx_test
```

Expected: FAIL — MODE and SLOP are not parameters yet.

- [ ] **Step 4: Implement in `eng_obx.c`.**
  - The morph is a crossfade of the taps `analog_tap` already computes (`eng_obx.c:24-32` returns `v2` for LP, `v1` for BP and `in - k·v1 - v2` for HP). Notch is `lp + hp`. Crossfade across the four in Q12 over MODE 0..120.
  - 4-pole above MODE 120 cascades the second SVF already used by `analog_render_x`.
  - SLOP seeds from the note-on counter into `voice_t.s[]` (`note_on` already seeds `v->s[2]` from `v->age`): ±slop × 6 cents of pitch and ±slop × 2 semitones of cutoff. `keep` must not preserve the slop slot across retrigger.
- [ ] **Step 5: Run the tests to GREEN**, then the regression and budget checks:

```bash
build/host/obx_test
cc -O2 -w -Ibuild/gen -Ifirmware/src -o build/host/regress tests/regress.c -lm && build/host/regress tests/golden.txt tests/cpu_baseline.txt
```

OB-Z golden hashes **will** change — this is a deliberate sound change. Regenerate them, read the diff, and name the reason in the commit. CPU: OB-Z must stay ≤ 1.3× its current figures in `tests/cpu_baseline.txt` (the OB-Z lines are 1298–1787 instructions per sample, so the ceiling is about 2320).

- [ ] **Step 6: Commit** `feat(obx): SEM morph filter, 4-pole, slop`

---

## Task 4.2: WAVE — DC blocker

Backlog item 1. Small, self-contained, and it unblocks a preset fix.

**Files:** modify `firmware/src/eng_wave.c`, modify `presets/four/wave.py`, modify `tests/void_test.c`.

- [ ] **Step 1: Failing test** in `tests/void_test.c`: render WAVE's BRASS LEAD for 2 s at a fixed position; assert `|mean(out)| / peak(out) < 0.01`. Expect RED today — DC is why that preset ships with REV 0.
- [ ] **Step 2: Implement** a one-pole DC blocker (`y[n] = x[n] - x[n-1] + R·y[n-1]`, R ≈ 0.995 at 44.1 kHz, Q15) on the engine output in `eng_wave.c`. Per-engine, after the ladder, before the send.
- [ ] **Step 3: GREEN.** Then restore `REV 25` on BRASS LEAD in `presets/four/wave.py` and rebuild.
- [ ] **Step 4:** WAVE golden hashes change. Regenerate, state the reason.
- [ ] **Step 5: Commit** `fix(wave): DC blocker; restore REV 25 on BRASS LEAD`

---

## Task 5.0: Own-envelope hooks on `engine_t`

Required by ACID, missing from the codebase (finding F4). Doing it as its own task keeps the ACID diff reviewable.

**Files:** modify `firmware/src/core.h`, modify `firmware/src/voice.c`, modify `tests/four_test.c`.

**Interfaces produced** — `eng_acid.c` in Task 5.2 fills these in:

```c
/* added to engine_t, after .mem */
uint8_t ownenv;                      /* 1 = the engine runs its own amp envelope; voice.c applies no ADSR
                                      * gain and does not decide when the voice ends */
/* Q15 amplitude this engine wants applied to its own output for voice v, control rate */
int32_t (*amp)(const struct track *t, const voice_t *v);
/* 1 = this voice has finished sounding and may be freed (ownenv engines only) */
int (*done)(const struct track *t, const voice_t *v);
```

- [ ] **Step 1: Failing test** in `tests/four_test.c`: a stub engine with `ownenv = 1` and an `amp` returning a constant 0x4000 keeps sounding after the track ADSR's release would have ended it, and is freed exactly when its `done` returns 1.
- [ ] **Step 2: RED.** `cc -O1 -w -Ibuild/gen -Ifirmware/src -Itests -o build/host/four_test tests/four_test.c -lm && build/host/four_test`
- [ ] **Step 3: Implement.** In `voice.c` (the control-rate amplitude is built around line 680 where `vmod_t` is assembled): when `eng->ownenv`, take `amp0`/`amp1` from `eng->amp` instead of `v->env`, and gate voice release on `eng->done` instead of `v->stage == 3` reaching zero. Every engine with `ownenv == 0` must take the **identical** path it takes today.
- [ ] **Step 4: GREEN**, and the full `regress` run must show **zero** golden hash changes — no existing engine's sound may move. If a hash changes, the refactor is wrong; fix it rather than regenerating.
- [ ] **Step 5: Commit** `feat(engine): own-envelope hooks for engines that run their own VEG`

---

## Task 5.1: FPU spike — measurement (D10 is decided)

A measurement, not a feature, and no longer a gate: D10 is decided. It confirms the FPU build is sound before the ACID and DRIFT ports depend on it, and it now also captures the **RAM headroom**, which `docs/BASELINE.md` does not record and which Task 6.1 needs in order to size `DRIFT_POLY`.

- [ ] **Step 1:** On a scratch branch, add the x0x flags to `tools/build.py`: `FPU = ["-mcpu=r3", "-mfprev1", "-ffp-contract=off"]` appended to `CFLAGS`, and port x0x's soft-double link guard (`fm1-x0x-main/tools/build.py:298-303`) so a stray `double` fails the build.
- [ ] **Step 2:** Rebuild unchanged VOID FOUR with the flags on. Record: image size vs 344,712 B, and the full test suite result (every golden hash must be unchanged — the integer engines compile to the same arithmetic).
- [ ] **Step 3:** Drop `fm1-x0x-main/firmware/src/dsp/{bass303.c,bass303.h,fastmath.h}` into a throwaway host harness and measure one `bass303` voice at `BASS303_OS 2`, `BASS303_CTRL 4` in the same instructions-per-sample units `tests/regress.c` uses, so it is directly comparable to `cpu_baseline.txt`.
- [ ] **Step 4: Report to the user**, in `.superpowers/sdd/task-5.1-report.md` and in chat:
  - image size delta with the FPU on;
  - whether any golden hash moved;
  - `bass303` cost per sample vs the budget left (`cpu/mix/4parts_full` is 1705 today; OB-Z is ~1500, WAVE ~1800 per voice);
  - a go / no-go on D10.
- [ ] **Step 5:** The user chooses branch A or branch B in Task 5.2. Only then does anything land.

---

## Task 5.2: ACID — the 303 voice

**Write a sub-plan in `docs/plans/` before starting.** The donor is well over 300 lines, so the project rules require it; it should fix the exact parameter mapping from `bass303`'s 12 pots onto ACID's 8 edit slots plus ENV.

**Files:** replace `firmware/src/eng_acid.c`, create `tests/acid_test.c`, create `tests/acid_ref/` vectors, add `LICENSES/MIT-Open303.txt`, update `CREDITS.md`, update `tests/target_budget.py` `FUNCS` and `tests/cpu_baseline.txt`.

**Machine params:**

| Slot | Param | Maps to `bass303` |
|---|---|---|
| E0 | WAVE (SAW/SQR) | `BASS303_WAVE` |
| E1 | TUNE | `BASS303_TUNE` |
| E2 | **DECY** (K4) | `BASS303_DECAY`, 200 ms – 2.5 s |
| E3 | ACC | `BASS303_ACCENT` |
| E4 | CUT | `BASS303_CUTOFF` |
| E5 | RES | `BASS303_RESO` |
| E6 | DRV | `BASS303_DRIVE` + `BASS303_DRVTYPE` Soft |
| E7 | SQL | resonance boost tied to accent (`BASS303_ACCDEC` plus a RES offset) |

`P_ED_FLT` (K3, ENV) is the 303's `BASS303_ENVMOD`. `ownenv = 1`, `poly = 1`, mono forced. Accent is step velocity > 110 (`seq.c:920` sets 127). Slide is `t->slide_glide` (`seq.c:977`) at `BASS303_SLIDE` ≈ 60 ms. `ACID_MEM = sizeof(acid_t)`, asserted against `ENG_MEM_PART`. **Measured: `sizeof(bass303_t)` at `BASS303_OS 2` is 752 bytes and `ENG_MEM_PART` is 768 today (`void_vc_t` 96 × `NVOICE` 8), so ACID fits the existing region with 16 bytes to spare and the pool does not grow.**

- [ ] **Step 1: Build the reference.** Compile the donor on the host, unmodified, and render vectors for a 5×5 CUT/RES grid plus one accented and one slide phrase into `tests/acid_ref/`. This is the `tools/void_ref.cpp` pattern applied to `bass303`, and it is what makes every assertion below objective.

```bash
cc -O2 -w -I"../fm1-x0x-main/firmware/src/dsp" -o build/host/acid_ref tests/acid_ref.c -lm
build/host/acid_ref tests/acid_ref
```

- [ ] **Step 2: Write the failing tests** in `tests/acid_test.c`, using `tests/dspfft.h` from Task 4.1:
  - (a) filter impulse response on the 5×5 grid within 1 dB of the reference vectors;
  - (b) RES 127 self-oscillates within ±3 % of the expected frequency;
  - (c) an accented step peaks ≥ 3 dB louder with a higher spectral centroid than the same step unaccented;
  - (d) over a slide pair 48→60 the pitch moves monotonically across 50–70 ms with no amplitude dip > 1 dB (no retrigger);
  - (e) DECY 0 vs 127 changes the filter envelope time constant by ≥ 5×;
  - (f) CPU per voice ≤ 3× OB-Z (so ≤ ~4600 instructions/sample).
- [ ] **Step 3: RED**, with the output captured.
- [ ] **Step 4: Implement** — **branch A, decided 2026-10-16** (branch B only if the spike shows the image will not fit):
  - **Branch A (FPU enabled).** Lift `bass303.c`, `bass303.h` and `fastmath.h` into `firmware/src/`, keeping every SPDX header and the Open303 MIT notice. Replace `x0x_param.h` with the `param_desc_t` mapping above. Wrap it in `eng_acid.c` behind `note_on` / `render` / `amp` / `done`. Set `BASS303_OS 2`, `BASS303_CTRL 4` and tune down only if (f) fails.
  - **Branch B (integer only).** Hand-port the same signal path to Q15/Q31 against the identical reference vectors: `TANH_Q15` for the shaper, a new exponential decay table in `tools/gen_tables.py`, its own halfband pair (DRIFT's `dsp_va.c` is float and is not shared with this branch). Budget this branch at roughly 3× branch A's effort.
- [ ] **Step 5: GREEN on (a)–(f).** Add `acid_render` to `tests/target_budget.py` `FUNCS` and append its ACID lines to `tests/cpu_baseline.txt` — **append; never overwrite an existing line.**
- [ ] **Step 6: Commit** `feat(acid): 303 voice with accent and slide, ported from bass303 (Open303)`

---

## Task 5.3: Accent and slide are editable on the big UI

Likely already satisfied by the DETAIL page; this task proves it and only then changes anything.

- [ ] **Step 1: Failing `ui_test`:** on track 3, SEQ → step 1 → set ACCENT and SLIDE with the step-page knobs → `step[0].flags == (SF_ACCENT | SF_SLIDE)`; play → `trk[2].slide_glide` is set on step 2.
- [ ] **Step 2:** Run it. **If it passes, stop** — keep the test, change nothing else.
- [ ] **Step 3:** If it fails, make accent and slide reachable in ≤ 4 controls on the SEQ STEP page, and keep `ui_render` at 0 findings.
- [ ] **Step 4: Commit** `test(seq): accent/slide editable on ACID`

---

## Task 6.1: DRIFT — the Juno voice (KR-106 port)

**Write a sub-plan in `docs/plans/` before starting.** The donor is well over 300 lines, so the
project rules require it. This is the highest-CPU-risk task in the project.

**Donor:** `../ultramaster_kr106-main` (Ultramaster KR-106, GPL-3), specifically
`Source/DSP/KR106VCF_OPTIMIZED.h`, `KR106Oscillators.h`, `KR106Resampler.h` and `KR106_HPF.h`.
Read `docs/DSP_ARCHITECTURE.md` and `docs/vcf_modulation_budget.md` in that repo first: they
give the algorithms and the hardware-measured constants. **Read it, never edit it.**

**Files:**
- Create: `firmware/src/dsp_va.c` — the float VCF and the halfband resampler pair.
- Replace: `firmware/src/eng_drift.c` — the DCO, the voice, the parameter mapping.
- Create: `tests/drift_ref.cpp` — the host reference renderer, built against the donor.
- Create: `tests/drift_ref_cfg.h` — the shared case table, included by both sides.
- Create: `tests/drift_test.c` — the acceptance tests.
- Create: `LICENSES/GPL-3-KR106.txt`; modify `CREDITS.md`.
- Modify: `tests/target_budget.py` (`FUNCS`), `tests/cpu_baseline.txt` (append only).

**`eng_wave.c` is not in this list and must not be modified.** Any change to WAVE's golden
hashes in this task is a defect.

### Machine params

| Slot | Param | Range / meaning | Donor |
|---|---|---|---|
| E0 | MIX | 0..127 DCO blend: sub-heavy below 32, saw at 64, pulse at 127 | `KR106Oscillators.h` saw/pulse/sub gains |
| E1 | **PW** | 0..127 pulse width, with LFO PWM depth above 64 | `effPW = pw / (1 + 0.15 * (1 - pw))` |
| E2 | DRFT | 0..127 DCO drift | `Voice::SetDriftAmount` / `UpdateDriftWalk` |
| E3 | VAR | 0..127 per-voice component variance | `Voice::InitVariance`, 5 variance params |
| E4 | **CUT** | 0..127 (**contract: must stay E4**) | `calc_vcf_freq`, C4 zero point |
| E5 | **RES** | 0..127 (**contract: must stay E5**) | `k = res * 4.5`, taper above 0.7 |
| E6 | **DRV** (K4) | 0..127 OTA drive and Q compensation | `comp = 1 + res^2 * 0.5` |
| E7 | KTR | 0..127 VCF keyboard tracking | 1143 DAC counts per octave, C4 zero |

`poly = 2` provisionally — Step 7 sets the final value from the measured budget. The 4-position
HPF reads the existing common param `P_FTYPE`; it does not take an edit slot. `ownenv = 0`:
DRIFT uses the track ADSR through `voice.c` as OB-Z and WAVE do, unlike ACID.

### Interfaces produced

```c
/* dsp_va.c — float VA primitives, FPU-only (D10). DRIFT is the only consumer. */
typedef struct { float s[4], k, g, g1, g1_2, g1_3; int os; } va_ladder_t;
void  va_ladder_reset(va_ladder_t *f);
void  va_ladder_coeffs(va_ladder_t *f, float cutoff_hz, float res, float sr);  /* control rate */
float va_ladder_tick(va_ladder_t *f, float x);                                  /* audio rate, os x per sample */

typedef struct { float x[12], y[12]; } va_hb_t;      /* HIIR 12-coefficient polyphase halfband */
void  va_hb_reset(va_hb_t *h);
void  va_hb_up(va_hb_t *h, float x, float *a, float *b);   /* 1 in, 2 out */
float va_hb_down(va_hb_t *h, float a, float b);            /* 2 in, 1 out */
```

### Step 1: The host reference renderer

This is what makes the rest of the task objective, and it exists because the whole KR-106 DSP
layer is JUCE-free. It mirrors `tools/void_ref.cpp`, which does the same job for WAVE.

- [ ] Write `tests/drift_ref_cfg.h`: a `drift_ref_cfg_t {float hz, res, drv; uint8_t os;}` table
      of at least 25 cases (a 5×5 cutoff × resonance grid), plus `DRIFT_REF_FS`, `DRIFT_REF_N`
      and a `drift_ref_input(n, f0)` test signal. Both the reference and the test include it,
      so neither side can drift from the other.
- [ ] Write `tests/drift_ref.cpp`: include `KR106VCF_OPTIMIZED.h` from the donor, run each case
      through `kr106::VCF` and write `build/drift_ref/NN.f32`.

```bash
c++ -std=c++17 -O2 -ffp-contract=off -w \
  -I"../ultramaster_kr106-main/Source/DSP" -Itests \
  -o build/host/drift_ref tests/drift_ref.cpp
mkdir -p build/drift_ref && build/host/drift_ref build/drift_ref
```

`-ffp-contract=off` is required on both sides: the FM-1 has no fused multiply-add, so the
reference must not use one either or the two will not agree.

- [ ] Commit the renderer on its own: `test(drift): KR-106 reference renderer`

### Step 2: Write the failing tests

In `tests/drift_test.c`, using `tests/dspfft.h` from Task 4.1. Two tiers, because two different
things are being asserted:

**Tier 1 — the lifted filter, held tight.** `dsp_va.c`'s ladder is the donor's algorithm at the
same precision, so it should agree almost exactly, not merely closely:

- [ ] (a) For every case in `drift_ref_cfg.h`, `va_ladder_tick` matches `build/drift_ref/NN.f32`
      to a relative error **< 1e-4** over the whole buffer. A looser tolerance here means the
      port diverged somewhere; find it rather than widening the bound.

**Tier 2 — the adapted voice, held to behaviour.** The oscillator, the control rate and the
parameter mapping are ours, so these are spectral and behavioural:

- [ ] (b) 24 dB/oct slope: magnitude at 4× cutoff ≥ 20 dB below the passband.
- [ ] (c) DRV 0 → 127 raises the THD of a 200 Hz sine through an open filter from < 1 % to > 10 %.
- [ ] (d) Self-oscillation at RES 127, within ±3 % of the expected frequency.
- [ ] (e) DRFT 0 → two renders byte-identical; DRFT 127 → a slow wander ≤ ±8 cents.
- [ ] (f) VAR 0 → every voice has the same cutoff; VAR 127 → measurably different per voice.
- [ ] (g) E0 MIX sweep: sub dominant at 0, saw at 64, pulse at 127, each verified by the
      spectrum (sub = a peak an octave below the fundamental; pulse = odd harmonics at PW 64).
- [ ] (h) `P_FTYPE` moves the HPF corner across its four positions.
- [ ] (i) CPU: a full four-track pattern stays inside the host model of the 80 % target in
      `tests/target_budget.py`.

### Step 3: RED

```bash
cc -O1 -w -Ibuild/gen -Ifirmware/src -Itests -o build/host/drift_test tests/drift_test.c -lm
build/host/drift_test
```

Capture the actual failure output. Do not accept an implementer's word that it failed.

### Step 4: Implement `dsp_va.c`

- [ ] Port `kr106::VCF` to the C interface above: four TPT integrators, OTA-saturated feedback,
      one Newton-Raphson step per stage from the linear estimate, the Pade tanh
      `x * (27 + x*x) / (27 + 9*x*x)`, the BA662 input/feedback ratio and the resonance taper
      above normalised 0.7. Keep the donor's comments explaining the circuit — they are the
      reason the constants are what they are.
- [ ] Port the halfband pair from `KR106Resampler.h` (the HIIR coefficients).
- [ ] **Gate: tier-1 test (a) must pass before any DRIFT code is written.** The filter is the
      part that can be proven correct in isolation; prove it first.

### Step 5: Implement `eng_drift.c`

- [ ] PolyBLEP saw with the charge curvature `pos * (1 + 0.15 * (1 - pos))` and the discharge
      blip, the comparator pulse with width compensation, the CD4013 sub with its RC rolloff
      (`coeff 0.45`), and the LCG noise with the spectral tilt. **Use the polyBLEP path, not the
      wavetable path** — the donor's wavetable oscillator needs tables built above its DSP layer
      and does not stand alone.
- [ ] Per-voice state lives in engine memory, as WAVE's does: `DRIFT_MEM = sizeof(drift_vc_t) * DRIFT_POLY`,
      with `ENG_MEM_FITS("DRIFT", DRIFT_MEM)`. The donor's `kr106::Voice<float>` is 888 bytes;
      trim it hard — the wavetable state, scope sync, J106 firmware-tick DAC emulation,
      portamento and variance metadata are all droppable.
- [ ] **Record the resulting `ENG_MEM_PART` and the four-part pool size in `docs/BASELINE.md`.**
      It is 768 bytes today and DRIFT is what grows it.

### Step 6: GREEN

- [ ] All of (a)–(i) pass.
- [ ] `regress` shows **no golden hash change for OB-Z, WAVE or ACID**. DRIFT's own hashes are
      new entries, not changes.
- [ ] Add `drift_render` to `tests/target_budget.py` `FUNCS` and **append** DRIFT's lines to
      `tests/cpu_baseline.txt`. Never overwrite an existing line.

### Step 7: Set the voice count from the measurement

Measured on the host while planning: a full Juno voice costs 2.31× the integer ladder at OS 1,
4.43× at OS 2 and 8.63× at OS 4. Choose `DRIFT_POLY` and the oversampling factor from test (i),
and **write the chosen values and the measured figures into this file.**

**Fallback ladder, in this order, if (i) fails:** (1) oversampling 2× → 1×; (2) `DRIFT_POLY`
2 → 1; (3) move the VCF coefficient update to every 4th sample, as `bass303` does with
`BASS303_CTRL`; (4) replace the Newton step with the plain explicit TPT form, accepting the
zipper artifacts the donor's comments warn about at high cutoff and resonance. Record which
fallback was used in the commit message and here.

### Step 8: Licences and credit

- [ ] `LICENSES/GPL-3-KR106.txt` with the donor's notice.
- [ ] `CREDITS.md`: Ultramaster KR-106 (kayrockscreenprinting), GPL-3, for the DRIFT machine.
- [ ] Every ported file carries `SPDX-License-Identifier: GPL-3.0-only` and names the donor file
      it came from at the top, as `bass303.c` does for Open303.

- [ ] **Step 9: Commit** `feat(drift): Juno voice ported from Ultramaster KR-106`

---

## Task 7.1: Audition renderer

**Files:** create `tests/audition.c` (on `hostsim.c`), create `tools/audition_index.py`.

- [ ] **Step 1:** `build/host/audition build/audition` renders every factory preset to a 6 s WAV at `build/audition/<MACHINE>/<nn>_<NAME>.wav`.
  - ACID presets play pattern 1 (with accent and slide).
  - BASS and LEAD presets play a 2-bar phrase.
  - KEYS, BRASS, STRINGS and ATMOS presets hold a 4-note chord for 3 s, then release.
- [ ] **Step 2:** The renderer fails the run on any of: peak > −1 dBFS; RMS outside −24..−10 dBFS; DC > 1 %; silence.
- [ ] **Step 3:** `tools/audition_index.py` writes `build/audition/index.html`: one table per machine with an `<audio>` player, name, category, a 1–5 rating select persisted to `localStorage`, and an "export ratings" button that downloads JSON.
- [ ] **Step 4: Commit** `tools: preset audition renderer`

---

## Task 7.2: The 128 presets — user gate

**Files:** rewrite `presets/four/{obx,wave,acid,drift}.py`. Names ≤ 12 characters from `" #%+-./0-9A-Z"`.

ACID's and DRIFT's 32 presets each were **written blind against placeholder DSP** and must be re-voiced from scratch now that the engines are real. OB-Z's need revisiting for the new MODE and SLOP params.

- Category quotas are fixed: OB-Z BRASS 8 / STRINGS 8 / KEYS 8 / LEAD 8; WAVE ATMOS 12 / LEAD 10 / BASS 10; ACID ACID 32; DRIFT BASS 12 / LEAD 10 / KEYS 10.
- ACID's 32 split by style in naming only: CLASSIC 8, SQUELCH 8, DISTORTED 8, DEEP 8.
- **DRIFT's 32 are derived from the donor's bank, not written blind.**
  `../ultramaster_kr106-main/KR106_Presets.h` holds 240 factory patches and
  `preset_categories.csv` labels them (Bass 31, Piano/Keys 33, Pad/Strings 24, Brass 13,
  Lead 11). Convert the best fits into DRIFT's BASS 12 / KEYS 10 / LEAD 10 and fill the gaps
  by hand — the same method WAVE's 32 came from its 69 converted originals. Write a small
  converter under `tools/` rather than transcribing by hand, and note in `CREDITS.md` that the
  preset values derive from the KR-106 bank.

- [ ] **Step 1:** Write each set, run `./build.sh` (the generator validates counts, categories, names and `edit[]` ranges), then run `audition` and fix every loudness or peak failure.
- [ ] **Step 2: USER GATE.** Give the user `build/audition/index.html`. They rate by ear. **Every preset rated ≤ 2 is redesigned and re-auditioned.** Repeat per machine until the user signs off on that machine.
- [ ] **Step 3:** Regenerate `tests/golden.txt` for the presets, and commit with the sign-off noted: `feat(presets): 128 factory presets (OB-Z/WAVE/ACID/DRIFT x32)`

---

## Task 8.1: Hardware — user gate

- [ ] **Step 1:** `./build.sh --release 0.1`. Record image size, RAM and pool headroom in `docs/BASELINE.md`.
- [ ] **Step 2: ASK THE USER before flashing.** Only with their explicit go-ahead in that session, and the FM-1 connected:

```bash
python3 tools/fm1_install.py build/felucca-0.1.fwsc
```

Expected: the identity reads back `FM-1_940`. Have the FM-1 Transporter (XIAO RP2040) rescue rig ready **before** the first flash.

- [ ] **Step 3:** On the device, with the PERF meter (`song.cpu_q8`, console `perf`), play the default project: 4 tracks, each machine at its voice cap, chords on OB-Z and WAVE. Expected ≤ 80 %. If over, reduce in this order: DRIFT oversampling, WAVE `VOID_POLY` 6→4, DRIFT voices 2→1. Record the final voice caps in `four_test` assertions and in this file.
- [ ] **Step 4: MIDI check** against a DAW over USB: channels 1–4 each play their track; CC 74/71/79/70 move K1–K4 on the right track and turning K1–K4 sends them back; EXT silences the track and sends its sequence out on its channel; clock out holds the DAW in sync.
- [ ] **Step 5: Commit** `chore: v0.1 hardware measurements`

---

## Task 9: Release

- [ ] `README.md` rewritten for VOID FOUR: the four machines, the controls, and credit to Felucca / Hügelton and every donor.
- [ ] `CREDITS.md` and `LICENSES/`: add Open303 (MIT, Robin Schmidt), schwung-303 / fm1-x0x (GPL-3), dm-Rat if the RAT drive ships, and confirm `MIT-DaisySP.txt` is still warranted by code actually in the tree.
- [ ] `docs/MIDI.md` final pass. **Document the by-design behaviour change:** with the default INT mode the panel keys do not send MIDI; BOTH re-enables it.
- [ ] Triage the backlog in `docs/HANDOVER.md` §7 at the final broad review: the four MIDI-OUT minor findings (`mout_och[k]`, `mout_try` in `mout_note`, `mout_pay` on INT tracks, nested loads finalising each other at `ui.c:902`), the stale `category.c:20` comment, and the editor-protocol notes.
- [ ] Add to the backlog, not to v0.1: the KR-106 **BBD chorus** (`KR106Chorus.h`, 805 lines,
  ngspice-verified) as a per-track INSERT type (`P_ITYPE`). It is a large part of the Juno's
  character, but it is a global effect in the real instrument and this tree already has a
  chorus send, so it is a Phase 10 item.
- [ ] Update `../CLAUDE.md` (the FM-1 section) and the memory note to point at `FM1-Four/` and this plan.
- [ ] Web editor: **deferred out of v0.1** (D12). `web/EDITOR_PROTOCOL.md` is known stale on the user-preset sentinel (15 → 4), the removed MENU id 23, `G_CLKOUT` and `MOUT`.
- [ ] **One broad review** across the whole release, per the user's standing process.
- [ ] **Commit** `docs: v0.1 release documentation`

---

## Self-review against the master plan

| Master plan item | Where it is handled here |
|---|---|
| Phase 4 OB-Z SEM morph, 4-pole, slop | 4.1 (task body kept; FFT helper extracted) |
| Phase 5 ACID 303 | 5.0 + 5.1 + 5.2 (donor changed to `bass303`, env hooks added, FPU gated) |
| Phase 5.2 accent/slide editable | 5.3 (unchanged) |
| Phase 6 DRIFT ZDF ladder | 6.1, rewritten as the KR-106 Juno port in float, with a host reference renderer and an explicit fallback ladder (D13) |
| Phase 7 audition + presets | 7.1, 7.2 (unchanged) |
| Phase 8 hardware | 8.1 (unchanged) |
| Phase 9 release | 9 (backlog triage folded in) |
| WAVE DC blocker (backlog 1) | 4.2 (given its own task) |
| Review of 3.3 + 3.4 | R1 (the gate everything waits on) |
| Baseline verification | R0 (added: this plan's numbers are inherited, not measured) |
