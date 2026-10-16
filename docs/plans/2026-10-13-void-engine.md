# VOID: the VOID WAVE synth as a Felucca engine

Status (2026-10-13): milestones 1-4 implemented and tested on the host; not yet run on an FM-1.

- Wavetables: `tools/gen_void_tables.py` (55 keyframes, 41.8 KB; `tests/void_tables_test.py`: every keyframe and
  level within -96 dB of the plugin's frames band-limited).
- Engine: `firmware/src/eng_void.c` (engine 14). Its filter matches the plugin's own `Filter.cpp` within 0.06 dB
  (level) and 0.55 dB (each harmonic) over 18 cases (`tools/void_ref.cpp`, `tests/void_test.c`). Added beyond the
  design: Morph (OSC2 = MORPH), the filter envelope (FLT ENV page: the plugin's ENV1), the plugin's close-gain and
  SVF saturation, a voice cap of 6 (cost: 6 VOID voices ~ 8 of PHASE WIRE, the heaviest engine before).
- Presets: all 69 converted (`tools/void_presets.py` -> `firmware/src/void_presets.h`; losses per preset in
  `docs/void_presets.md`).
- Felucca changes outside the engine: TYPE gains LP12 / HP24 / NOTCH (ANALOG plays them as LP / HP / LP), the
  FILTER and FLT ENV pages for VOID, VOID's favourites in DIGITAL's spare row bits, its categories.
- Firmware image 566 888 B of 581 564 (14.7 KB free); RAM unchanged (the engine memory region).
- Still to do: Ring mix, Comb / Formant / Diode, sub / noise, the pitch envelope, OSC1's distortion and spectral
  modes, the web editor; the voice cap and the cost on a real FM-1 (PERF).

## Goal

A Felucca engine (`eng_void.c`, engine number 14, appended) that sounds as close as practical to
the VOID WAVE plugin (`../VoidWave`), within the FM-1's budget, and whose factory presets are
converted from the plugin's 69 presets.

## What the plugin actually sounds like (from its source)

All 69 factory presets: wavetable position 0, tables 0-8 only, every FX at mix 0, unison 1 in 56,
Blend mix in 56 (Morph 8, Ring 5), LP24 in 57, EXP envelope curves. The audible character is: the
frame-0 table spectra (above all the 48-harmonic Saw), phase-0 note restart, the LP24 tanh ladder
(`fb = 4.2(1-(1-r)^1.5)`, makeup `1+1.8re`, a 2 % `x|x|` pre-saturation), cutoff law
`20 + 19980 x^4` with ENV1 adding `env * depth * (cutoff - 20)`, squared envelope curves, the 0.72
voice soft clipper, and slow random movement: pitch drift (+-1.5 cents), WT-position drift (up to
+0.055) and per-unison micro-LFOs (0.4-2.5 cents).

## Budget (stock Felucca 1.6 build)

| | Free |
|---|---|
| Flash | 73 KB (image 508 108 of 581 564 B) |
| RAM pool | ~31 KB usable (8 KB headroom enforced) |
| Engine memory | 12.9 KB per part (shared region, `engine_t.mem`) |
| Arithmetic | integer only: Felucca builds without FPU flags and its engines are fixed point |

## Design

### Wavetables (tools/gen_void_tables.py -> build/gen/void_tables.h)

- A Python port of the plugin's generators (WavetableBank.cpp) for tables 0-8: Sine, Saw, Square,
  Spectral, Formant A, Formant E, Bass 01, Bass 02, Noise (same formulas and seeds; the Noise
  table's per-frame random phases need the JUCE Random sequence reproduced).
- Keyframes instead of 256 frames: the generator picks, per table, the fewest frames whose linear
  crossfade stays within -40 dB of the plugin's frames over position 0..1 (Sine, Saw and the Bass
  tables are close to linear in position: ~3 frames; Spectral, Formant, Noise more). Estimate ~42
  frames in total.
- Band-limited per octave (mip levels of 256/128/64/32/16 samples, each with the harmonics that fit
  below Nyquist at the top of its octave): cleaner than the plugin, which aliases.
- int16, ~1 KB per keyframe with its mips: ~42 KB of flash. The Square table becomes band-limited
  (the plugin's is a naive pulse).
- Spectral HARMONIC (used by 6 presets): a precomputed variant of frame 0 where needed (later).

### Voice (eng_void.c)

- Two oscillators: table, position, pitch; read at 1x with linear interpolation within the frame
  and a crossfade between the two keyframes around the position; the mip level from the pitch.
- Mix: Blend (v1). Morph and Ring later (13 presets).
- Phase 0 on every note (the plugin's FREE mode). 5 ms ramp-in.
- Analogue movement, per control block: pitch drift, WT drift, a micro-LFO per oscillator, with
  the plugin's constants.
- Filter: LP24 (the tanh ladder, fixed point, a rational tanh, coefficients per 32-sample block),
  LP12 / HP12 / BP / Notch (Felucca's SVF helpers), HP24. Drive: the plugin's shaper,
  approximated. Comb, Formant, Diode: later (Formant 5 presets, Comb 2).
- The 0.72 soft clipper on the voice.

### What Felucca provides (not reimplemented)

8 voices, POLY / MONO / LEGATO / UNISON (with DETUNE and SPREAD: stereo), glide, the ADSR (amp)
with ENV -> FILTER / PITCH / SHAPE, the LFO (-> pitch / filter / shape / amp, tempo sync), a
4-slot mod matrix, distortion / chorus / delay / reverb, the sequencer, arp, chords, presets.
SHAPE modulation drives the WT position (the plugin's LFO1 -> WT Pos).

### Controls (8 engine knobs, two EDIT pages)

| Page | E0..E3 / E4..E7 |
|---|---|
| OSC | TBL1, POS, TBL2, MIX |
| FILTER | CUT, RES, DRV, OSC2 pitch (semitones; fine detune via the preset converter) |

The FILTER page (TYPE CUT RES KTR, today ANALOG's only) is enabled for VOID too; its TYPE list gains
VOID's types.

### Departures from the plugin (accepted)

- One envelope (Felucca's ADSR) drives both amp and filter: the plugin has separate ENV1 / ENV2.
- Unison is Felucca's (voices spread with DETUNE, linear, up to ~+-40 cents) instead of the
  plugin's per-oscillator curve `b(0.4+0.6b^2) * 200 * detune` with 1/N gain. Only 13 presets use it.
- Mono through the voice; stereo from Felucca's SPREAD / UNISON and FX (the plugin pans unison
  voices hard left/right).
- The plugin's block-rate pitch-envelope bug (a 10 ms blip becomes ~1.3 s) is not reproduced:
  Felucca's ENV -> PITCH gives the intended blip.
- 4 mod-matrix slots, not 12; the plugin's silent destinations are dropped.
- The web editor (Felucca-WebApp, separate repo) won't know engine 14 until it is updated.

### Presets (tools/void_presets.py)

Reads the plugin's .vwpreset files and writes `preset_t` entries: tables, mix, cutoff (inverse
of the x^4 law), resonance, drive, ADSR (from ENV2; ENV1 depth -> fenv), mono, FX sends 0. A
report lists what each preset loses (Morph / Ring, unison, ENV1 != ENV2, matrix routings).

### Testing

- Host: tables vs the Python reference; the engine rendered in tests/regress.c (all engines and
  presets: golden output and the CPU check, +25 % over the baseline); a sweep test (no NaN-like
  overflow, no clipping runaway at max resonance and drive).
- Listening: render each converted preset on the host and the plugin's preset in the VST
  (offline), compare by ear and by spectrum.
- Device: the PERF numbers, then by ear.

## Milestones

1. Table generator + host check (flash size known).
2. Engine: oscillators + Blend + LP24 + clipper; 5 presets; regress + CPU check.
3. Remaining filter types, drive, analogue movement.
4. Preset converter: all 69 (with the loss report).
5. Firmware build, size checks, install on the FM-1.
6. Later: Morph / Ring, Spectral HARMONIC, Comb / Formant, web editor support.
