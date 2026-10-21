# New DIST, Juno chorus and subtractive SB303: viability and plan

**Status:** implemented 2026-10-21 (host-tested, built; not yet on the device). Notes in .superpowers/sdd/progress.md. Inputs: `DIST CHORUS SUB/fm1_drive_1.h`, `fm1_juno_chorus.h`, `fm1_subsynth.h`.

**Why:** On the user's live song the FM-1 overloads. With DIST off on every track the late audio blocks drop from 49 to 0 a second.
On the host, DIST is about 38 % of the song's load and the current Scream DIST costs 370 instructions/sample at 4x (226 at 2x).
Moving the Juno chorus to RAM cut most of its device cost; a cheaper chorus would remove the rest.

**Licence:** none of the three files carries a licence or an author. Confirm their origin before anything ships; the firmware is GPL-3.0.

---

## 1. DIST: `fm1_drive_1.h`

Signal path: gain (1x..64x) -> + bias -> SOFT (clamp then cubic) / HARD / FOLD -> DC blocker -> one-pole tone LPF -> level.
Integer Q15, in place, int16 buffer.

| | Current Scream DIST (4x) | fm1_drive SOFT |
|---|---|---|
| Cost, host instr/sample | 370 (2x: 226) | ~15-25 estimated (to measure) |
| Character | tanh into a resonant LP/HP feedback loop ("scream"), drive +27 dB | plain cubic overdrive; no resonance or feedback |
| Aliasing (saw, P_DIST 127) | -51 / -43 dB | none handled: a hard input clamp at up to 64x gain, like the old FX.1 DIST (-20..-44 dB). The tone LPF only darkens it |
| Controls | one macro knob, P_DIST | mode, drive, bias, tone, level |

**Fit with our code (fx.c `track_dist`):**
- **Input range.** `b[]` is int32 and reaches 5 FS on ACID. `buf * gain` overflows past 1 FS at 64x, so clamp (or scale) to int16 first.
- **Overflow.** `(dc - lp) * tone` can pass INT32_MAX once the DC blocker's output exceeds about 65 k (HARD or FOLD on square waves). Use a shift or int64.
- **Controls.** P_DIST stays the single knob. Fix the mode at SOFT, have tone follow the drive, and make the level up with a table as `dist_out_gain` does now. Keep P_DIST 0 as a bit-identical bypass.
- **Tests.** The dist_test alias gates (-50 dB up to P_DIST 102), the intensity gate and the cost pins need new figures. The level match within 3 dB, the bound, silence and switch tests stay. All 47 DIST goldens move.

**Verdict:** viable and roughly 15x cheaper, but it is a different, plainer sound with much more aliasing. The cubic only aliases where the
input clamp cuts in, so running it at 2x on the existing half-band filters (about +60 instr) would recover most of the clean top end
at about a third of today's cost. **Plan:** add it as `DIST_KIND 1` beside Scream; measure 1x and 2x with the same dist_test figures and WAVs
as the ADAA prototype; then the user picks by ear.

## 2. Chorus: `fm1_juno_chorus.h`

Two linear-interpolated taps on one int16 line (512 B), one triangle LFO (L +, R -), one-pole BBD filters (~9 kHz), optional hiss, dry and wet each at -3 dB.

| | Current (KR-106 port, float) | fm1_juno_chorus |
|---|---|---|
| Cost, host instr/sample | 182 | ~40-60 estimated (integer, no Hermite, no 2-pole filters) |
| Modes | Juno-106 figures: 0.514 / 0.842 / 7.85 Hz | Juno-60 figures: 0.513 / 0.863 / 9.75 Hz; I+II shallower and faster |
| Filters | Butterworth 2-pole pre and post, BBD saturation, charge-transfer gain | one-pole pre and post only |
| Switching | 5 ms crossfade on every change, the tail rung out | the delay glides; OFF jumps straight to dry (a click) |

**Fit with our code (`chorus_part` hook):**
- **Range.** It takes int16 and clamps its output to int16, but DRIFT parts reach about 2.8 FS after the chorus. Run it on b >> 2 and shift back up, or widen it to int32. It must still return mid / side and respect `CHORUS_GUARD_FS`.
- **On/off.** Keep our wrapper for the 5 ms fade, the tail (`zrun`) and the clean start. The module writes its line even when OFF, but we only call it while it is on.
- **64-bit multiply.** `(int64)depth * tri` is a 64-bit multiply every sample. On this 32-bit core that may become a library call, which the RAM code may not make. Pre-shift depth so the product fits 32 bits.
- **Tests.** chorus_test compares against the KR-106 donor (LFO rate +-5 %, sweep +-10 %, sidebands +-1.5 dB). Those checks fail by design, so they need restating against this module's own spec. The headroom, wet-bus and tail tests stay. JUNO chorus goldens move.

**Verdict:** viable and probably 3-4x cheaper. Decide between Juno-60 rates (the module) and Juno-106 rates (the current chorus): the
JUNO machine models a 106, and its rates can be set in `jch_set_mode`. **Plan:** port as `dsp_chorus2.c` behind the same `chorus_part`
interface, put the loop in RAM (no 64-bit multiply), A/B on the device with the song.

## 3. Subtractive voice in the SB303 machine: `fm1_subsynth.h`

Goal: SB303 presets 1-2 stay the classic 303 (SAW, SQUARE); presets 3-32 use the subtractive voice.

**What the module brings:** PolyBLEP saw and pulse (with PWM), sub-octave square, noise, a 2x-oversampled Chamberlin SVF (LP12 / LP24 / BP / HP) with a soft-saturated band path, all integer. About 25-35 multiplies per sample.

**What it duplicates (do not port):** its own note stack, glide, legato, ADSRs, LFO, velocity and pitch bend. Our `voice.c` already
does MONO / LEGATO / POLY / UNISON, glide, the ADSR, both LFOs, the mod matrix and bend. Use only the oscillators, the filter and the
VCA multiply as a render core, driven by `voice_t` and `vmod_t` like OB-Z and JUNO.

**How the engine changes (eng_acid.c, voice.c, engines.c):**
1. **MODEL per track.** A stored hidden parameter `P_RSV_LN + 1` (0 = 303, 1 = SUB), set by the preset's extras table. It saves with projects and user presets for free (DRIFT already stores its CHOR at `P_RSV_LN`).
2. **Own envelope per track.** ACID has `ownenv = 1` (the 303's own VCA). The SUB model needs voice.c's ADSR. Replace the three reads of `e->ownenv` in voice.c with `eng_ownenv(t)`, and the same for `amp` / `done`.
3. **Voices per track.** ACID is `.poly = 1` (one bass303 per part). SUB needs a per-model voice cap (`eng_poly(t)`): mono or up to 4 voices.
4. **Engine memory.** Union of `acid_t` and N × the voice state (about 120 B each without the note stack and envelopes); must fit `ENG_MEM_PART`.
5. **EDIT pages by model (engine `.desc` hook already exists):** 303 keeps WAVE TUNE DECY ACC CUT RES DRV SQL. SUB: E0 WAVE (SAW / PULSE / SAW+PULSE), E1 DTUN (osc 2 detune), E2 PW, E3 SUB, E4 CUT, E5 RES, E6 OSC2 (level / semitone), E7 NOIS. The filter type is P_FTYPE (LP12 / LP24 / BP / HP), ENV is P_ED_FLT and PWM comes from the matrix (LFO -> SHP). HOME's four knobs stay CUT RES ENV + E2.
6. **Presets.** `presets/four/acid.py`: 2 ACID + 30 SUB. Categories BASS / LEAD / KEYS / ATMOS (counts to choose) in `four_presets.py`, plus an `ACID_PX` table (model and extras), validated like WAVE_PX.
7. **Level and guard.** Trim SUB to the other machines' audition median (-17 dBFS). The output stays under ACID's 5 FS guard.

**Tests:** keep acid_test on presets 1-2. Add a `sub_test`: pitch accuracy, PolyBLEP alias floor, filter stable at full resonance and every
type, voices freed after release, mono/legato/glide through voice.c, cost. Then audition gates for the 30, new goldens (ACID 3-32, union),
ui_render with the SUB labels, and editor MENU_DESC through the `.desc` hook.

## Order of work

1. Confirm the licence and origin of the three files.
2. Chorus port (smallest, measurable on the device right away).
3. DIST_KIND 1, with 1x and 2x WAVs to compare against Scream.
4. SUB model: engine plumbing (steps 1-4 above), then the render core, then the 30 presets, then tests.
5. Device PERF on the user's song after each step.

## Decisions (user, 2026-10-21)

- The three modules were written from scratch for this project: no third-party licence to carry (GPL-3.0 like the rest).
- SUB voice: **polyphonic** (up to 4 voices).
- DIST: **replace Scream** with the drive (no Scream build option kept).
- Chorus: **Juno-60 rates as in the module** (0.513 / 0.863 / 9.75 Hz).
- The SB303 machine is renamed **NANO**. Presets 1-2 the classic 303, 3-32 subtractive: BASS 10 / LEAD 10 / KEYS 6 / ATMOS 4 (Claude's default split).
