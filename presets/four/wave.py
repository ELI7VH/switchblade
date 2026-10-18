# WAVE: 32 original factory presets for the single-oscillator engine (Task 4.6): ATMOS 8, LEAD 10, BASS 14.
# The bank is no longer converted from the VOID WAVE plugin (docs/void_presets.md). Each preset is designed around one signature
# route that really defines it, declared in `sig` and checked by tools/four_presets.py: the route must hold in the preset's
# routing, and no two presets may share (table, TYPE, sig).
# e = TBL1 POS WARP (MIX) CUT RES (OSC2) DRV: MIX and OSC2 are hidden (one oscillator, eng_wave.c) and 0; WARP 64 = off.
# px = the void_px_t extras (ftype, voice, detune+1, spread, glide, lfo[5], fe[5], mx[4][3]).
# Matrix codes: SRC 1 LFO 2 ENV 3 VEL 4 KEY 5 RAND 6 MODW 7 AT 9 S&H; DST 1 PITCH 2 CUT 3 SHP(POS) 11 VIB 14 WARP (E3).
# LFO RATE at 120 BPM: 44 = 1 bar-half (0.5 Hz), 57 = 1/2, 70 = 1/4, 83 = 1/8, 96 = 1/16 ("synced" = that rate: the preset format
# has no SYNC field and a preset load sets SYNC OFF). LFO WAVE 0 SIN 1 TRI 2 SAW 3 SQR 4 S&H.
# ENV -> CUT is the amp envelope's ENV -> FLT amount (fenv, up to 63 steps) when the filter envelope is off (fe[0] = 0), the
# filter envelope (fe) itself reaches one octave at most.
# Tables: 0 SINE 1 SAW 2 SQUARE 3 SPECT 4 FORMA 5 FORME 6 BASS1 7 BASS2 8 NOISE. TYPE: 0 LP(24) 1 BP 2 HP 3 LP12 4 HP24 5 NOTCH 6 VOWEL.
NOFE = [0, 0, 64, 127, 64]
NOMX = [[0, 0, 0]] * 4
FXD = dict(ATMOS=(0, 30, 30, 60), LEAD=(0, 10, 35, 25), BASS=(0, 0, 0, 0))
PATD = dict(ATMOS=5, LEAD=4, BASS=2)


def P(name, cat, sig, tbl, pos, cut, res, drv, env, ftype, warp=64, fenv=0, mono=0, fx=None, pat=None, voice=0, detune=0,
      spread=0, glide=0, lfo=None, fe=None, mx=None):
    return dict(name=name, cat=cat, sig=sig, e=[tbl, pos, warp, 0, cut, res, 0, drv], env=env, fenv=fenv, mono=mono,
                fx=fx if fx is not None else FXD[cat], pat=pat or PATD[cat], ftype=ftype, voice=voice, detune=detune,
                spread=spread, glide=glide, lfo=lfo or [0, 0, 0, 0, 0], fe=fe or NOFE,
                mx=(mx or []) + [[0, 0, 0]] * (4 - len(mx or [])))


PRESETS = [
    # ------------------------------------------------------------------------------------------------ ATMOS
    # (preset 0 is the power-on sound of part 2 (engines.c TRK_DEF): POLY, and the tests that record into that part need it)
    P("VOWEL CLOUD", "ATMOS", "LFO->CUT", 4, 40, 56, 55, 0, [90, 80, 120, 100], 6, lfo=[1, 28, 0, 1, 0], mx=[[1, 2, 40]],
      fx=(0, 70, 30, 70)),
    P("EVOLVE PAD", "ATMOS", "LFO->POS", 3, 30, 90, 12, 25, [92, 80, 118, 100], 3, voice=3, detune=51, spread=110,
      lfo=[1, 30, 1, 0, 0], mx=[[1, 3, 42]], fx=(0, 50, 30, 60)),
    P("NOISE WIND", "ATMOS", "LFO->CUT", 8, 20, 72, 95, 40, [88, 80, 127, 95], 1, lfo=[1, 33, 0, 1, 0], mx=[[1, 2, 30]],
      fx=(0, 30, 30, 55)),
    P("CZ PLUCK", "ATMOS", "ENV->WARP", 0, 10, 100, 0, 60, [0, 84, 0, 60], 3, mx=[[2, 14, 40]], pat=7, fx=(0, 20, 55, 25)),
    P("GLASS BELL", "ATMOS", "ENV->POS", 5, 0, 110, 0, 40, [0, 95, 0, 100], 0, mx=[[2, 3, 63], [2, 3, 63]], pat=7, fx=(0, 20, 40, 60)),
    P("S+H BLIPS", "ATMOS", "SH->CUT", 2, 64, 60, 95, 70, [5, 70, 127, 50], 0, lfo=[1, 96, 4, 1, 0], mx=[[9, 2, 40]],
      fx=(0, 10, 45, 40)),
    P("DARK DRONE", "ATMOS", "LFO->POS", 7, 40, 54, 35, 0, [100, 80, 127, 100], 4, lfo=[1, 20, 0, 1, 0], mx=[[1, 3, 63], [1, 3, 30]],
      fx=(0, 30, 20, 70)),
    P("VELO KEYS", "ATMOS", "VEL->CUT+WARP", 1, 0, 50, 15, 0, [5, 80, 40, 55], 3, mx=[[3, 2, 45], [3, 14, 30]], pat=6,
      fx=(0, 20, 25, 35)),
    # ------------------------------------------------------------------------------------------------ LEAD
    P("ELVEN FLUTE", "LEAD", "LFO->PITCH", 0, 12, 90, 0, 0, [50, 64, 118, 62], 3, mono=1, glide=62,
      lfo=[1, 89, 0, 0, 92], mx=[[1, 1, 1]], fx=(0, 0, 30, 40)),
    P("OCARINA", "LEAD", "ENV->POS", 5, 20, 85, 0, 30, [28, 56, 92, 60], 3, mono=1, glide=20, lfo=[1, 86, 0, 0, 85],
      mx=[[2, 3, 63], [1, 1, 1]], fx=(0, 0, 25, 35)),
    P("MYSTIC VOICE", "LEAD", "MODW->CUT", 4, 40, 28, 40, 70, [40, 64, 120, 70], 6, mono=1, glide=50,
      lfo=[1, 88, 0, 0, 90], mx=[[6, 2, 63], [6, 2, 55], [1, 1, 1]], fx=(0, 10, 25, 50)),
    P("SPIRIT GLASS", "LEAD", "LFO->POS", 5, 50, 80, 50, 40, [30, 64, 120, 80], 1, mono=1, glide=20, lfo=[1, 40, 0, 1, 0],
      mx=[[1, 3, 30]], fx=(0, 10, 60, 50)),
    P("FAIRY LEAD", "LEAD", "SH->POS", 3, 90, 100, 10, 55, [10, 64, 120, 70], 3, mono=1, glide=15, lfo=[1, 50, 4, 1, 0],
      mx=[[9, 3, 30]], fx=(0, 15, 55, 35)),
    P("HEROIC HORN", "LEAD", "AT->VIB", 1, 0, 52, 15, 0, [66, 80, 110, 70], 0, fenv=45, mono=1, glide=25,
      lfo=[1, 85, 0, 0, 70], mx=[[7, 11, 12]], fx=(0, 5, 20, 35)),
    P("ORACLE", "LEAD", "LFO->WARP", 0, 20, 100, 0, 0, [40, 64, 118, 80], 3, warp=90, mono=1, glide=20,
      lfo=[1, 38, 0, 1, 0], mx=[[1, 14, 14]], fx=(0, 5, 25, 70)),
    P("SUPERSAW", "LEAD", "UNISON", 1, 0, 95, 15, 0, [20, 64, 120, 55], 3, voice=3, detune=101, spread=100,
      fx=(0, 20, 30, 30)),
    P("WARP SYNC", "LEAD", "ENV->WARP", 1, 0, 90, 25, 20, [66, 70, 112, 60], 0, mono=1, glide=25, mx=[[2, 14, 40]],
      fx=(0, 5, 25, 25)),
    P("MODW MORPH", "LEAD", "MODW->POS", 3, 20, 88, 20, 45, [30, 64, 120, 70], 0, mono=1, glide=15,
      mx=[[6, 3, 63], [6, 3, 63]], fx=(0, 10, 30, 30)),
    # ------------------------------------------------------------------------------------------------ BASS
    P("NEURO WOBBLE", "BASS", "LFO->POS+CUT", 3, 20, 56, 64, 60, [0, 80, 127, 30], 0, mono=1,
      lfo=[1, 83, 2, 1, 0], mx=[[1, 3, 40], [1, 2, 30]]),
    P("FORMANT GRWL", "BASS", "LFO->CUT", 1, 0, 64, 90, 60, [0, 80, 127, 30], 6, mono=1,
      lfo=[1, 77, 0, 1, 0], mx=[[1, 2, 50]]),
    P("REESE", "BASS", "UNISON", 1, 0, 48, 30, 30, [10, 80, 127, 40], 0, voice=3, detune=71, spread=70,
      lfo=[1, 30, 0, 1, 0], mx=[[1, 2, 20]]),
    P("SUB STOMP", "BASS", "ENV->PITCH", 0, 0, 70, 0, 0, [0, 76, 0, 40], 0, mono=1, mx=[[2, 1, 63], [2, 1, 20]]),
    P("ACID WARP", "BASS", "ENV->WARP", 2, 64, 52, 100, 40, [0, 66, 80, 40], 0, fenv=0, mono=1, glide=40,
      fe=[100, 0, 60, 0, 50], mx=[[2, 14, 45]]),
    P("TALK BASS", "BASS", "ENV->CUT", 6, 20, 32, 70, 40, [60, 90, 90, 50], 6, fenv=63, mono=1,
      mx=[[2, 2, 40]]),
    P("S+H GRIT", "BASS", "SH->POS", 7, 40, 60, 45, 80, [0, 80, 127, 30], 0, mono=1, lfo=[1, 96, 4, 1, 0],
      mx=[[9, 3, 63], [9, 3, 63]], fx=(25, 0, 0, 0)),
    P("CZ BASS", "BASS", "ENV->WARP", 0, 0, 80, 20, 20, [0, 72, 0, 40], 0, mono=1, mx=[[2, 14, 50]]),
    P("MORPH STAB", "BASS", "ENV->POS", 6, 0, 70, 40, 30, [0, 65, 0, 40], 0, mono=1, mx=[[2, 3, 63], [2, 3, 63]]),
    P("KEY SCAN", "BASS", "KEY->POS", 7, 70, 64, 35, 25, [0, 80, 90, 40], 0, mono=1, mx=[[4, 3, 63], [4, 3, 63], [4, 3, 63]], pat=9),
    P("DUB YOI", "BASS", "LFO->CUT", 2, 40, 56, 70, 50, [0, 80, 120, 40], 6, mono=1, glide=15, lfo=[1, 83, 3, 1, 0],
      mx=[[1, 2, 50]], fx=(0, 0, 30, 20)),
    P("ELECTRO PNCH", "BASS", "ENV->CUT", 2, 40, 34, 55, 50, [0, 60, 55, 30], 0, fenv=60, mono=1),
    P("ELECTRO ZAP", "BASS", "ENV->PITCH", 1, 0, 80, 20, 20, [0, 62, 0, 40], 3, mono=1, mx=[[2, 1, 63], [2, 1, 63]]),
    P("ELEC FIDGET", "BASS", "LFO->WARP", 7, 40, 70, 30, 30, [0, 80, 120, 30], 0, mono=1, lfo=[1, 96, 3, 1, 0],
      mx=[[1, 14, 24]]),
]
