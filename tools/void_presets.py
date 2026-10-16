#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""VOID's factory presets from the VOID WAVE plugin's (VoidWave/Resources/Presets/factory/*/*.vwpreset).

Writes firmware/src/void_presets.h (checked in: the build does not need ../VoidWave) and a report of what each
preset keeps and loses (docs/void_presets.md). The mapping, from the plugin's DSP (VoidWave/Source):
  tables, MIX       Blend: the two weights level1 (1 - mix), level2 mix -> MIX = w2 / (w1 + w2) (their sum, the
                    level, is not carried: a load never changes LEVEL, Felucca's mix); Morph: OSC2 = MORPH (OSC1's pitch and phase), MIX = mix * osc1_width; Ring: a blend
                    (lost: the ring)
  OSC2              osc2 - osc1 pitch (coarse + fine) to the nearest step; osc1's own coarse is lost (TRANS, like
                    LEVEL, is the track's: a load keeps it)
  filter            TYPE: LP24 -> LP (the ladder), LP12, HP12 -> HP, HP24, BP, NOTCH; COMB, DIODE -> LP, FORMANT ->
                    BP at ~1.2 kHz (lost); CUT: the inverse of 20 + 19980 x^4; RES, DRV: x 127
  envelope          ENV2 (amp) -> A D S R (Felucca's times: 1 ms * 10000^(v / 127)); ENV1 (the filter's) -> FLT
                    ENV (eng_void.c: P_FM1_*; its velocity sensitivity in P_FM1_LEVEL), its depth -> ENV DEST FLT
                    (63 = 1: the plugin's cutoff times 1 + env depth)
  matrix            the plugin's 12 slots -> Felucca's 4 (the most audible first): LFO1 (the track LFO: its rate,
                    shape, trigger, fade), velocity, envelope, key, mod wheel, aftertouch, smooth random (SLEW)
                    -> CUT (the plugin's linear +10 kHz per unit as steps at the preset's cutoff), SHP (the WT
                    position), PITCH, E4 (MIX), E6 (RES), E8 (DRV); filter keytrack -> KEY -> CUT. Velocity ->
                    AMP is Felucca's own (every engine's amplitude follows the velocity); the plugin's silent
                    destinations (level, pan, detune, LFO rate / depth, ENV1 depth) stay silent
  voice             poly 1 -> mono (LEGATO); unison on a mono preset -> UNISON with DETUNE, on a poly one SPREAD
                    (lost: the unison); glide -> GLIDE
  FX                every plugin preset has its FX at mix 0: the sends 0
  lost              sub, noise, the pitch envelope, OSC1's distortion modes and spectral morph, ENV3, LFO2 where
                    LFO1 is used too, Macro routings (the macros sit at 0: silent there too)
  tools/void_presets.py [PRESET_DIR]   (default ../VoidWave/Resources/Presets/factory)
"""
import math
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT_H = ROOT / "firmware" / "src" / "void_presets.h"
OUT_MD = ROOT / "docs" / "void_presets.md"
CATS = ["BASS", "LEAD", "PAD", "PLUCK", "STAB", "SYNTH", "MISC"]          # the order presets are listed in
CAT_LETTER = {"BASS": "B", "LEAD": "L", "PAD": "P", "PLUCK": "U", "STAB": "K", "SYNTH": "L", "MISC": "F"}
# (MISC: its sirens, zaps, drones are effects; and Felucca keeps OTHER for user sounds: no factory sound in it)
VF = {0: 3, 1: 0, 2: 2, 3: 4, 4: 1, 5: 5, 6: 0, 7: 1, 8: 0}             # plugin filter_type -> eng_void.c VF_*
VF_NAME = ["LP", "BP", "HP", "LP12", "HP24", "NOTCH"]
PLUGIN_FT = ["LP12", "LP24", "HP12", "HP24", "BP", "NOTCH", "COMB", "FORMANT", "DIODE"]
OSC2_FINE = [-50, -30, -20, -15, -12, -10, -8, -6, -4, -2, 2, 4, 6, 8, 10, 12, 15, 20, 30, 50]
OSC2_STEPS = sorted(OSC2_FINE + [100 * k for k in range(-24, 25)])
OSC2_MORPH = len(OSC2_STEPS)
STEPS_PER_OCT = 127 / math.log2(16000 / 30)                              # Felucca's cutoff index: ~14 per octave
MS_OFF, MS_LFO, MS_ENV, MS_VEL, MS_KEY, MS_RAND, MS_MODW, MS_AT, MS_EXPR, MS_SH, MS_SLEW = range(11)
MD_OFF, MD_PITCH, MD_CUT, MD_SHP, MD_AMP = range(5)
MD_E4, MD_E6, MD_E8 = 12 + 3, 12 + 5, 12 + 7
LWAVE = {0: 0, 1: 1, 2: 2, 3: 2, 4: 3, 5: 4, 6: 4}                     # plugin LFO shape -> Felucca SIN TRI SAW SQR S&H


def f(p, k, d=0.0):
    try:
        return float(p.get(k, d))
    except ValueError:
        return d


def i(p, k, d=0):
    return int(round(f(p, k, d)))


def clampi(v, lo, hi):
    return int(max(lo, min(hi, round(v))))


def time_knob(sec):
    """Felucca's F_TIME: 1 ms * 10000^(v / 127)"""
    return clampi(127 * math.log10(max(sec * 1000.0, 1.0)) / 4.0, 0, 127)


def cut_knob(hz):
    """the inverse of the plugin's 20 + 19980 x^4 (filter_cutoff, skew 0.25)"""
    return clampi(127 * (max(hz - 20.0, 0.0) / 19980.0) ** 0.25, 0, 127)


def steps(ratio):
    """a cutoff ratio as Felucca cutoff steps"""
    return STEPS_PER_OCT * math.log2(max(ratio, 1e-3))


def hz_steps(base, dhz):
    """the plugin's linear +dhz at a base cutoff, in steps (down: towards 20 Hz)"""
    return steps((base + dhz) / base) if dhz >= 0 else steps(max(base + dhz, 20.0) / base)


# the plugin's names over Felucca's 12 characters
SHORT = {"CLASSIC PLUCK": "CLASSIC PLK", "CYBERPUNK ARP": "CYBERPNK ARP", "FM BELL SYNTH": "FM BELL SYN",
         "FORMANT GROWL": "FORMNT GROWL", "GLASS SHIMMER": "GLASS SHIMMR", "LUSH SUPERSAW": "LUSH SUPRSAW",
         "RETROWAVE PAD": "RETROWAV PAD", "SPECTRAL LEAD": "SPECTRL LEAD", "SPECTRAL STAB": "SPECTRL STAB",
         "SUPERSAW LEAD": "SUPRSAW LEAD", "SUPERSAW STAB": "SUPRSAW STAB", "CLASSIC ANALOG": "CLASSIC ANLG",
         "HARMONIC SWEEP": "HARMNC SWEEP", "SPECTRAL DRIFT": "SPECTRL DRFT", "POLYPHONIC KEYS": "POLY KEYS",
         "STRING ENSEMBLE": "STRING ENSMB", "CYBER BASS SYNTH": "CYBER BASS", "HARMONIC CRYSTAL": "HARM CRYSTAL",
         "PITCH BEND SYNTH": "PITCHBND SYN", "UNISON SAW SYNTH": "UNISON SAW", "SPECTRAL MORPH SYNTH": "SPECTRL MRPH"}


def name12(n, used):
    n = " ".join(n.upper().replace("_", " ").split())
    n = SHORT.get(n, n)
    if len(n) > 12:
        w = n.split()
        n = " ".join(w)
        while len(n) > 12 and len(w) > 1:                                 # shorten the longest word first
            k = max(range(len(w)), key=lambda j: len(w[j]))
            if len(w[k]) <= 3:
                break
            w[k] = w[k][:-1]
            n = " ".join(w)
        n = n[:12].rstrip()
    base, k = n, 2
    while n in used:
        n = (base[:10] + " " + str(k))[:12]
        k += 1
    used.add(n)
    return n


def convert(p, used):
    lost = []
    name = name12(p.get("_name", "?"), used)
    cat = p.get("_category", "MISC").upper()
    t1, t2 = clampi(i(p, "osc1_table"), 0, 8), clampi(i(p, "osc2_table"), 0, 8)
    for k, t in (("osc1_table", t1), ("osc2_table", t2)):
        if i(p, k) > 8:
            lost.append(f"{k} {i(p, k)} (a hidden table): SINE")
    pos = clampi(127 * f(p, "osc1_position"), 0, 127)
    l1, l2, m, mode = f(p, "osc1_level", 0.8), f(p, "osc2_level", 0.8), f(p, "osc_mix", 0.5), i(p, "osc_mix_mode", 0)
    osc2 = None
    if mode == 0:                                                         # Morph
        amt = max(0.0, min(1.0, m)) * max(0.0, min(1.0, f(p, "osc1_width", 1.0)))
        mix, gain, osc2 = clampi(127 * amt, 0, 127), l1, OSC2_MORPH
    else:
        if mode == 2:
            lost.append("Ring mix (played as a blend, 10 dB down)")
        if mode >= 3:
            lost.append("Sync / FM mix (the plugin plays them as Blend too)")
        w1, w2 = l1 * (1 - m), l2 * m
        mix = clampi(127 * w2 / (w1 + w2), 0, 127) if w1 + w2 > 1e-6 else 0
        gain = (w1 + w2) * (0.32 if mode == 2 else 1.0)
    ldb = 20 * math.log10(max(gain / 0.8, 1e-3))
    if abs(ldb) > 3:
        lost.append(f"level {ldb:+.0f} dB against the plugin's default (LEVEL is the track's)")
    if i(p, "osc1_coarse"):
        lost.append(f"OSC1 {i(p, 'osc1_coarse'):+d} semitones (TRANS is the track's)")
    if osc2 is None:
        rel = (i(p, "osc2_coarse") + f(p, "osc2_fine") / 100) - (i(p, "osc1_coarse") + f(p, "osc1_fine") / 100)
        osc2 = min(range(len(OSC2_STEPS)), key=lambda k: abs(OSC2_STEPS[k] - rel * 100))
        if mix and abs(OSC2_STEPS[osc2] - rel * 100) > 1:
            lost.append(f"OSC2 at {rel * 100:+.0f} cents: {OSC2_STEPS[osc2]:+d}")
    if abs(f(p, "osc1_fine")) > 0.5:
        lost.append(f"OSC1 fine {f(p, 'osc1_fine'):+.0f} cents")
    # filter
    pft = i(p, "filter_type", 1)
    ft = VF.get(pft, 0)
    base = max(20.0, min(20000.0, f(p, "filter_cutoff", 5000)))
    if pft == 7:
        base = 1200.0
    if pft in (6, 7, 8):
        lost.append(f"filter {PLUGIN_FT[pft]}: {VF_NAME[ft]}")
    cut, res, drv = cut_knob(base), clampi(127 * f(p, "filter_res"), 0, 127), clampi(127 * f(p, "filter_drive"), 0, 127)
    if pft == 7:
        res = max(res, 50)
    # envelope: ENV2 the amp; ENV1 depth -> FLT
    a, d, s, r = (time_knob(f(p, "env2_attack", 0.01)), time_knob(f(p, "env2_decay", 0.1)),
                  clampi(127 * f(p, "env2_sustain", 0.7), 0, 127), time_knob(f(p, "env2_release", 0.3)))
    if f(p, "env2_hold") > 0.005:
        lost.append(f"ENV2 hold {f(p, 'env2_hold'):.2f} s")
    fenv = clampi(63 * f(p, "env1_depth", 0.5), -64, 63)                 # FLT ENV's depth (63 = 1)
    fe = [clampi(1 + 126 * f(p, "env1_vel_sens", 0.5), 1, 127), time_knob(f(p, "env1_attack", 0.01)),
          time_knob(f(p, "env1_decay", 0.1)), clampi(127 * f(p, "env1_sustain", 0.7), 0, 127),
          time_knob(f(p, "env1_release", 0.3))]
    if f(p, "env1_hold") > 0.005:
        lost.append(f"ENV1 hold {f(p, 'env1_hold'):.2f} s")
    mono = i(p, "voice_poly", 8) == 1
    # voice
    voice, detune, spread = (1 if mono else 0), None, 0
    uni = [1, 2, 3, 4, 6, 8][clampi(i(p, "osc1_unison"), 0, 5)]
    if uni > 1:
        if mono:
            voice = 3
            detune = clampi(127 * 200 * f(p, "osc1_detune") / 40, 0, 127)
            if 200 * f(p, "osc1_detune") > 40:
                lost.append(f"unison detune +-{200 * f(p, 'osc1_detune'):.0f} cents (Felucca's at most +-40)")
        else:
            spread = 64
            lost.append(f"unison {uni} (poly: SPREAD instead)")
    glide = time_knob(f(p, "voice_glide")) if f(p, "voice_glide") > 0.001 else 0
    for k, w in (("sub_level", "sub oscillator"), ("noise_level", "noise")):
        if f(p, k) > 0.001:
            lost.append(w)
    if abs(f(p, "pitchenv_amount")) > 0.01:
        lost.append(f"pitch envelope {f(p, 'pitchenv_amount'):+.0f} st")
    if i(p, "osc1_dist_mode") and f(p, "osc1_dist_amt") > 0.001:
        lost.append(["", "SYNC", "PWM", "BEND"][clampi(i(p, "osc1_dist_mode"), 0, 3)] + " on OSC1")
    if i(p, "osc1_spec_mode") and f(p, "osc1_spec_amt") > 0.001:
        lost.append(["", "HARMONIC", "SMEAR", "LOWPASS"][clampi(i(p, "osc1_spec_mode"), 0, 3)] + " spectral morph")
    # matrix: the plugin's slots -> (importance, src, dst, amt)
    lfo_used = {}
    routes = []
    if abs(f(p, "filter_keytrack")) > 0.01:
        routes.append((5, MS_KEY, MD_CUT, clampi(64 * STEPS_PER_OCT * f(p, "filter_keytrack") / 12, -64, 63), "keytrack"))
    for k in range(1, 13):
        if not i(p, f"mod{k}_enabled"):
            continue
        src, dst, dd = i(p, f"mod{k}_source"), i(p, f"mod{k}_dest"), f(p, f"mod{k}_depth")
        if abs(dd) < 0.005 or src in (9, 14) or 10 <= src <= 13:
            continue                                                      # breath, CC, the macros: 0 in the plugin
        sval = 1.0
        if src in (3, 4):                                                 # an LFO: its depth in the source
            sval = f(p, f"lfo{src - 2}_depth")
            if abs(sval) < 0.005:
                continue
            lfo_used[src] = True
        fsrc = {0: MS_ENV, 1: MS_ENV, 2: MS_ENV, 3: MS_LFO, 4: MS_LFO, 5: MS_VEL, 6: MS_KEY, 7: MS_AT, 8: MS_MODW}.get(src)
        if src in (3, 4) and i(p, f"lfo{src - 2}_shape") == 6:
            fsrc = MS_SLEW                                                # smooth random
        if fsrc is None:
            continue
        x = dd * sval
        if dst == 10:                                                     # cutoff, linear +10 kHz per unit
            amt, imp = hz_steps(base, 10000 * x), 10
        elif dst in (2, 3):                                               # WT position (one table position each)
            amt, imp = 127 * x, 8
        elif dst in (0, 1, 19, 20, 21, 22):                               # pitch: semitones (coarse 24 per unit)
            amt, imp = 63 / 11.8 * x * (24 if dst in (19, 20) else 1), 6
        elif dst == 36 and mode == 1:
            amt, imp = 64 * x, 7
        elif dst == 11:
            amt, imp = 64 * x, 4
        elif dst == 12:
            amt, imp = 64 * x, 4
        else:
            if dst not in (18, 4, 5, 6, 7, 8, 9, 13, 14, 15, 16, 17):
                lost.append(f"matrix slot {k}: source {src} -> destination {dst}")
            continue                                                      # AMP (Felucca's own), the silent ones
        fdst = {10: MD_CUT, 2: MD_SHP, 3: MD_SHP, 0: MD_PITCH, 1: MD_PITCH, 19: MD_PITCH, 20: MD_PITCH, 21: MD_PITCH,
                22: MD_PITCH, 36: MD_E4, 11: MD_E6, 12: MD_E8}[dst]
        if i(p, "lfo1_shape") == 3 and src == 3:                          # RAMP: a falling saw
            amt = -amt
        routes.append((imp, fsrc, fdst, clampi(amt, -64, 63), f"slot {k}"))
    merged = {}
    for imp, s_, d_, a_, why in routes:                                   # same source and destination: one slot
        key = (s_, d_)
        if key in merged:
            merged[key] = (max(imp, merged[key][0]), s_, d_, clampi(merged[key][3] + a_, -64, 63), why)
        else:
            merged[key] = (imp, s_, d_, a_, why)
    routes = sorted((r_ for r_ in merged.values() if r_[3]), key=lambda r_: -r_[0])
    for r_ in routes[4:]:
        lost.append(f"matrix ({r_[4]}): more than Felucca's 4 slots")
    routes = routes[:4]
    lfo = 1 if 3 in lfo_used else 2 if 4 in lfo_used else 0
    if 3 in lfo_used and 4 in lfo_used:
        lost.append("LFO2 (Felucca has one LFO: LFO1's routings kept)")
    lx = None
    if lfo:
        hz = max(f(p, f"lfo{lfo}_rate", 1.0), 0.05)
        lx = (clampi(127 * math.log(hz / 0.05) / math.log(800), 0, 127), LWAVE.get(i(p, f"lfo{lfo}_shape"), 0),
              0 if i(p, f"lfo{lfo}_trigger") else 1, time_knob(f(p, f"lfo{lfo}_fade")) if f(p, f"lfo{lfo}_fade") > 0.001 else 0)
        if i(p, f"lfo{lfo}_tempo_sync"):
            lost.append("LFO tempo sync (free rate)")
    return dict(name=name, cat=cat, e=[t1, pos, t2, mix, cut, res, osc2, drv], env=[a, d, s, r], fenv=fenv, mono=mono,
                fe=fe, ft=ft, voice=voice, detune=detune, spread=spread, glide=glide, lfo=lx,
                routes=routes, lost=lost)


def main():
    src = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT.parent / "VoidWave" / "Resources" / "Presets" / "factory"
    files = []
    for c in CATS:
        files += sorted((src / c).glob("*.vwpreset"))
    files += sorted(x for x in src.glob("*/*.vwpreset") if x.parent.name.upper() not in CATS)
    if not files:
        raise SystemExit(f"void_presets: no presets in {src}")
    used, pr = set(), []
    for fp in files:
        pr.append(convert(ET.parse(fp).getroot(), used))
    H = ["/* generated by tools/void_presets.py from the VOID WAVE plugin's factory presets: do not edit */",
         "/* VOID_PRESETS: name, {TBL1 POS TBL2 MIX CUT RES OSC2 DRV}, {A D S R}, ENV -> FLT, mono, the sends;",
         " * VOID_PX: what a preset_t cannot hold (eng_void.c void_px_t, applied by void_track_loaded) */",
         "static const preset_t VOID_PRESETS[] = {"]
    for x in pr:
        H.append(f'    {{"{x["name"]}", {{{", ".join(map(str, x["e"]))}}}, {{{", ".join(map(str, x["env"]))}}}, '
                 f'{x["fenv"]}, {int(x["mono"])}, FX(0, 0, 0, 0)}},')
    H += ["};", "static const void_px_t VOID_PX[] = {",
          "    /* TYPE, VOICE (3: UNISON), DETUNE + 1, SPREAD, GLIDE, LFO {set, RATE, WAVE, TRIG, FADE}, FLT ENV {LVL, A, D, S, R},",
          "     * matrix {SRC, DST, AMT} x 4 */"]
    for x in pr:
        lx = x["lfo"] or (0, 0, 0, 0)
        ms = [list(r_[1:4]) for r_ in x["routes"]] + [[0, 0, 0]] * (4 - len(x["routes"]))
        H.append(f'    {{{x["ft"]}, {x["voice"]}, {0 if x["detune"] is None else x["detune"] + 1}, '
                 f'{x["spread"]}, {x["glide"]}, {{{int(bool(x["lfo"]))}, {lx[0]}, {lx[1]}, {lx[2]}, {lx[3]}}}, '
                 f'{{{", ".join(map(str, x["fe"]))}}}, '
                 f'{{{", ".join("{" + ", ".join(map(str, q)) + "}" for q in ms)}}}}},   /* {x["name"]} */')
    H += ["};", f'#define VOID_PRESET_CAT "{"".join(CAT_LETTER.get(x["cat"], "F") for x in pr)}"']
    OUT_H.write_text("\n".join(H) + "\n")
    M = ["# VOID presets: what each keeps of the VOID WAVE plugin's",
         "", "Generated by `tools/void_presets.py` from `VoidWave/Resources/Presets/factory`. Felucca's VOID engine plays",
         "the plugin's wavetables and filter exactly (tests/void_test.c compares the filter with the plugin's own code);",
         "this lists, per preset, what the conversion could not carry over.", "",
         "| # | Preset | Category | Lost or approximated |", "|---|---|---|---|"]
    for k, x in enumerate(pr):
        M.append(f'| {k} | {x["name"]} | {x["cat"]} | {"; ".join(x["lost"]) if x["lost"] else "-"} |')
    n_clean = sum(1 for x in pr if not x["lost"])
    M += ["", f"{len(pr)} presets, {n_clean} with nothing lost."]
    OUT_MD.parent.mkdir(parents=True, exist_ok=True)
    OUT_MD.write_text("\n".join(M) + "\n")
    print(f"void_presets: {len(pr)} presets -> {OUT_H.relative_to(ROOT)}, report {OUT_MD.relative_to(ROOT)} "
          f"({n_clean} with nothing lost)")


if __name__ == "__main__":
    main()
