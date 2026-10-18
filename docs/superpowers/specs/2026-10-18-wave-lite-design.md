# WAVE lite: single-oscillator WAVE with VOWEL, WARP and 32 original presets

Date: 2026-10-18. Status: design approved by the user, spec awaiting review.

## Goal

VOID FOUR's WAVE machine (`firmware/src/eng_wave.c`), made single-oscillator by Task 4.4, gets two cheap signature
features and a new bank of 32 presets that are each clearly distinct and lean on what a wavetable synth does best.
The engine stays "ultra lightweight": LP24 becomes a cheap two-SVF filter (about half the ladder's cost), every
filter type runs on one SVF routine, and WARP costs nothing when it is off.

## Order of work

1. **Task 4.4** (WAVE single-osc, uncommitted in the tree as of this spec) is finished, reviewed and committed first.
   Everything below builds on it.
2. **Task 4.5**: the cheap LP24, the VOWEL filter type and WARP (DSP + tests).
3. **Task 4.6**: the 32 presets (replace `presets/four/wave.py`'s bank) + distinctness check + audition sign-off.
4. The single flash already planned (after 6.2, 4.4, 6.3, and now 4.5 / 4.6), then device PERF.

## What shipped (recorded after Task 4.6)

- **4.5a was dropped by the user**: LP24 is still the plugin-faithful Huovilainen ladder (dca1659), not two SVFs. In the
  tables below "LP" means that ladder and "LP12" the SVF.
- **WARP's descriptor minimum is 9** (`VOID_WARP_MIN`): a stored P_E2 of 0..8 is an old TBL2 value and loads as 64 (off), so
  WARP 0..8 is not selectable.
- **WARP's alias bound**: the "-50 dB at MIDI 96" figure cannot be met (the unwarped SAW at C7 is already -34 dB); the
  kept bound is off-harmonic energy of at most -21.7 dB over MIDI 36..96 and at most +13 dB over the same note unwarped,
  reached by pulling the knee towards 1/2 (the warp fades out above about 2 kHz).
- **4.6 realised within the engine**: no engine change. "Synced" LFO rates are the rate matching the division at 120 BPM
  (the preset format has no SYNC field and a preset load sets SYNC OFF). UNISON is the engine's six voices (not x3). The
  only matrix sources are the amp ENV, LFO, VEL, KEY, RAND, MODW, AT, S&H, SLEW, so "filter env -> WARP" (WARP SYNC) is
  ENV -> WARP, and ENV -> PITCH presets end their dive with the amp envelope (sustain 0).

## Task 4.5a: LP24 as two SVFs and one saturator (user decision, 2026-10-18)

The faithful LP24 ladder (4 table-lerped tanh stages, ~85 of a ~175 instr/sample voice, about half) is replaced. The
bank is now original, so matching the plugin's `Filter.cpp` is no longer a requirement for WAVE.

- `VF_LP24` = two cascaded SVF low-passes on the same coefficients routine LP12 uses (`void_svf1`, states f[0..3],
  as HP24 already does): stage 1 at a fixed k = 1.414 (no resonance), then **one** `void_tanh` on stage 1's output,
  then stage 2 with k from RES. RES 127 resonates strongly and softly self-oscillates; the tanh between the stages
  and the existing +-150000 state clamps bound it.
- Makeup gain from RES (tuned in 4.5) so the passband level at RES 0 is within 2 dB of LP12 and within 3 dB across
  RES.
- The close-gain behaviour near 20 Hz, the 2 % pre-saturation, the drive front-end and the SVF saturation above
  RES 0.78 are unchanged and shared with LP12.
- `void_ladder`, its feedback / makeup coefficients and the `g / (1 + g)` divide are deleted. `VOID_TANH1` is
  deleted if nothing else reads it (flash saving).
- Cost target (host): LP24 <= 45 instr/sample/voice, measured and recorded in the 4.5 report.
- **LP12 is untouched** (bit-exact).
- `tests/void_test.c`'s 18-case comparison against the plugin's `Filter.cpp` is narrowed to the types WAVE still
  shares with the plugin (LP12, BP, HP, NOTCH, HP24). LP24 gets its own tests (below).

## Task 4.5b: VOWEL filter type

- `N_FTYPE` (params.c) gains `"VOWEL"` as type 6. WAVE_PX `ftype` range (tools/four_presets.py) grows from (0, 5)
  to (0, 6). `eng_wave.c` uses `p[P_FTYPE] % 7u`. OB-Z and DRIFT play type 6 as LP, the same fallback they use for
  the other types they do not have.
- DSP: two parallel band-passes (F1, F2) built on the SVF helpers WAVE already uses, summed with fixed gains
  (F1 1.0, F2 0.7, then trimmed so VOWEL's level matches LP24 at the same CUT within 2 dB).
- **CUT (0..127, the post-modulation value) morphs through five vowels in order U, O, A, E, I**, with F1 / F2
  interpolated linearly in Hz between neighbours (table: U 300/870, O 450/800, A 730/1090, E 530/1840,
  I 270/2290 Hz). Every source that moves the cutoff (HOME CUT knob, filter envelope, LFO -> cutoff, matrix CUT)
  therefore sweeps the vowel.
- RES sets the band-passes' Q (from Q 2 at 0 to Q 12 at 127). DRV keeps feeding the existing pre-filter shaper.
- Coefficients are computed once per 32-sample block, like the other types.
- VOWEL is a filter type like the others (it does not stack). It uses the existing f[0..3] states, so per-voice
  memory does not grow.
- Cost target: VOWEL within 10 % of the new LP24 on the host CPU check.

## Task 4.5c: WARP on E2

- P_E2 (hidden since 4.4, previously TBL2) is un-hidden and named `WARP`. Bipolar, 0..127, **64 = off**. Its stored
  id does not move. The parameter's default becomes 64, and any loaded preset or project whose P_E2 holds an old
  TBL2 value (0..8) is out of range for WARP's meaning, so `void_track_loaded` and the preset generator set 64 for
  every factory preset that does not specify WARP.
- DSP: a phase bend on the table read. The read phase phi (0..1) goes through a two-segment knee at
  `k = 0.5 + w * 0.45`, where `w = (WARP - 64) / 63`: `phi' = phi * 0.5 / k` below k, and
  `0.5 + (phi - k) * 0.5 / (1 - k)` above. This squeezes the cycle to one side (CZ / Serum-style bend) and gives a
  sync-like brightening from one oscillator.
- At WARP = 64 the bend is skipped entirely: **every existing render is bit-exact**.
- Anti-aliasing: the mip level is chosen from `inc * max(0.5 / k, 0.5 / (1 - k))` (the steepest segment's slope),
  not from `inc`, so a heavy warp reads a darker mip instead of aliasing.
- Modulation: the matrix destination `E2` already exists, so ENV -> E2, LFO -> E2, VEL -> E2 work with no new routing.
- The big EDIT page stays `CUT, RES, ENV, POS`. WARP lives on the OSC edit page in the slot TBL2 used.

## What does not change

P_E3 / P_E6 stay hidden; the stored layout (FOR4, user presets) does not move; voice cap 6; the keyframe crossfade;
the DC blocker; the soft clipper; the shared Felucca LFO / matrix / unison / FX.

## Task 4.6: 32 presets

Split: **BASS 14, LEAD 10, ATMOS 8**. The existing 32 WAVE presets (conversions of the plugin's 69) are replaced,
not kept. The bank is original material designed for the single-oscillator engine. `docs/void_presets.md` gets a note
saying WAVE's bank is no longer converted.

**Distinctness rule** (enforced by `tools/four_presets.py`, which exits non-zero on a violation): each preset declares
one *signature* `(table, filter TYPE, signature mod route)`, and no two presets in WAVE's bank share the same triple.
The signature route is the preset's defining modulation (e.g. `LFO->SHP`, `ENV->E2`, `MODW->CUT`, or `UNISON` /
`GLIDE` / `ENV->PITCH` where those are the defining move).

"Synced" below means a tempo-synced LFO rate if Felucca's LFO RATE offers sync divisions; otherwise the rate matching
that division at 120 BPM.

### BASS (14)

| # | Name | Table | TYPE | Signature |
|---|---|---|---|---|
| 1 | NEURO WOBBLE | SPECT | LP | saw LFO (synced) -> POS + CUT; drive |
| 2 | FORMANT GROWL | SAW | VOWEL | sine LFO -> CUT ("yoy-yoy"); drive |
| 3 | REESE | SAW | LP | UNISON x3 wide detune, mono; slow LFO -> CUT |
| 4 | SUB STOMP | SINE | LP | ENV -> PITCH drop; clean |
| 5 | ACID WARP | SQUARE | LP | mono glide, high RES; ENV -> WARP |
| 6 | TALK BASS | BASS1 | VOWEL | filter env sweeps the vowel per note ("yoi") |
| 7 | S&H GRIT | BASS2 | LP | S&H LFO (synced) -> POS |
| 8 | CZ BASS | SINE | LP | ENV -> WARP (phase-distortion pluck bass) |
| 9 | MORPH STAB | BASS1 | LP | ENV -> POS, the table morphs per hit |
| 10 | KEY SCAN | BASS2 | LP | KEY -> POS across the keyboard |
| 11 | DUB YOI | SQUARE | VOWEL | square LFO (synced) -> CUT, chopped vowels |
| 12 | ELECTRO PUNCH | SQUARE | LP | short snappy filter env, mid RES, drive (electro-house pluck bass) |
| 13 | ELECTRO ZAP | SAW | LP12 | mono, fast ENV -> PITCH dive (80s electro-funk laser bass) |
| 14 | ELECTRO FIDGET | BASS2 | LP | square LFO (synced) -> WARP, choppy fidget bass |

### LEAD (10)

Seven fantasy / mystical solo leads: clear (low drive, little or no unison), with vibrato, glide and space from
delay / reverb.

| # | Name | Table | TYPE | Signature |
|---|---|---|---|---|
| 1 | ELVEN FLUTE | SINE (slight POS) | LP12 | mono glide; delayed sine vibrato (LFO -> PITCH, FADE) |
| 2 | OCARINA | FORME (low POS) | LP12 | short ENV -> POS "chiff"; soft vibrato |
| 3 | MYSTIC VOICE | FORMA | VOWEL | MODW -> CUT ("oo -> ah -> ee" on the wheel); glide; reverb |
| 4 | SPIRIT GLASS | FORME | BP | slow LFO -> POS shimmer; long delay |
| 5 | FAIRY LEAD | SPECT (high POS) | LP12 | slow S&H -> POS sparkle; ping-pong delay |
| 6 | HEROIC HORN | SAW | LP | slow-attack filter env swell; AT -> VIB |
| 7 | ORACLE | SINE | LP12 | slow LFO -> WARP, a breathing pure-to-bright tone; long reverb |
| 8 | SUPERSAW | SAW | LP12 | UNISON, wide detune + spread; delay |
| 9 | WARP SYNC | SAW | LP | filter env -> WARP sync sweep |
| 10 | MODW MORPH | SPECT | LP | MODW -> POS across the whole table |

### ATMOS (8)

| # | Name | Table | TYPE | Signature |
|---|---|---|---|---|
| 1 | EVOLVE PAD | SPECT | LP12 | slow triangle LFO -> POS; UNISON spread |
| 2 | VOWEL CLOUD | FORMA | VOWEL | slow sine LFO -> CUT; chorus + reverb |
| 3 | NOISE WIND | NOISE | BP | LFO -> CUT sweep |
| 4 | CZ PLUCK | SINE | LP12 | fast ENV -> WARP; delay |
| 5 | GLASS BELL | FORME | LP | ENV -> POS, fast decay |
| 6 | S&H BLIPS | SQUARE | LP | S&H (synced) -> CUT, high RES |
| 7 | DARK DRONE | BASS2 | HP24 | slow sine LFO -> POS |
| 8 | VELO KEYS | SAW | LP12 | VEL -> CUT + WARP |

Names are 12 characters or fewer (the PRESETS display's limit). Presets with AT -> VIB or MODW routes must still
sound complete with no aftertouch / mod wheel input.

## Testing

- **4.5 (void_test.c):** LP24 slope >= 21 dB/oct one octave above the cutoff at RES 0; a resonant peak at RES 127
  within +-1 semitone of the cutoff; bounded at RES 127 + DRV 127 over MIDI 24..99 (no int32 overflow, DC < 1 % rms);
  LP12 renders bit-exact against the pre-4.5 render; VOWEL F1 / F2 peaks within +-1 semitone of the vowel table at CUT 0, 64, 127; VOWEL at
  RES 127 + DRV 127 over MIDI 24..99 stays bounded (no runaway, no int32 overflow, DC < 1 % rms after the blocker);
  WARP = 64 renders bit-exact against the pre-4.5 render; WARP 0 and 127 at MIDI 96 keep aliasing below -50 dB;
  host CPU lines for VOWEL vs LP24 and WARP on vs off.
- **Goldens:** 4.5 re-records the WAVE goldens and `cpu/WAVE/` lines its LP24 change moves (the old bank, still in
  place); 4.6 re-records them again for the new bank. Each is a deliberate, stated re-record; no other machine's hash
  may move.
- **4.6:** the generator's range and distinctness checks; every preset passes the audition renderer's level window
  (no RMS_LOW / clipping).
- **Sign-off:** the user rates all 32 by ear on the audition page (`build/audition/index.html`) before the flash.

## Risks

- The new worst-case CPU mix may move: VOWEL is designed to be no costlier than LP24, but UNISON-heavy presets
  (REESE, SUPERSAW, EVOLVE PAD) set WAVE's worst case. Record the new worst legal mix in `docs/BASELINE.md`.
- WARP increases the effective brightness; the darker-mip rule may make extreme warps sound duller than the plugin
  would. That is accepted (the plugin has no WARP).
