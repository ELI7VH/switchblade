#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""VOID FOUR's factory presets: presets/four/{obx,wave,acid,drift}.py -> build/gen/four_presets.h.

  tools/four_presets.py [OUT.h] [VOID_TABLES.h]    (default build/gen/four_presets.h, build/gen/void_tables.h)

Each source holds PRESETS, a list of dicts (see presets/four/obx.py for the fields; wave.py adds the void_px_t
extras ftype voice detune spread glide lfo[5] fe[5] mx[4][3]). The generator enforces the contract and exits non-zero
on any violation, so a build with a bad preset set fails: exactly 32 presets per machine, the categories and their
counts of the machine (CATS), unique names of at most 12 characters in the large font's glyphs (GLYPHS), every e[i]
inside the engine's edit[i] range (RANGES: a copy of the engines' tables; WAVE's from void_tables.h).
Writes <M>_PRESETS[32], <M>_CAT[32] (CAT_*, category.c) for OBX WAVE ACID DRIFT, WAVE_PX[32] and DRIFT_PX[32] (DRIFT's chor).
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "presets" / "four"
N = 32
MACHINES = ["OBX", "WAVE", "ACID", "DRIFT"]                      # engine numbers 0..3 (core.h ENGI_*)
CAT_ORDER = ["BASS", "LEAD", "KEYS", "BRASS", "STRINGS", "ATMOS", "ACID"]     # category.c: CAT_NONE = 0, then these
CATS = {                                                         # per machine: category -> count (in this order)
    "OBX": {"BRASS": 8, "STRINGS": 8, "KEYS": 8, "LEAD": 8},
    "WAVE": {"ATMOS": 12, "LEAD": 10, "BASS": 10},
    "ACID": {"ACID": 32},
    "DRIFT": {"BASS": 12, "LEAD": 10, "KEYS": 10},
}
GLYPHS = set(" #%+-./0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ")      # the AF_L face (gen_aa_font.py L_CHARS)
PCT = (0, 127)
RANGES = {                                                       # edit[0..7] min..max (eng_obx.c, eng_acid.c, eng_drift.c)
    "OBX": [(0, 6)] + [PCT] * 7,
    "ACID": [(0, 1), (-64, 63)] + [PCT] * 6,
    "DRIFT": [PCT] * 8,
}
WAVE_PX = dict(ftype=(0, 5), voice=(0, 3), detune=(0, 128), spread=PCT, glide=PCT)
DRIFT_CHOR = (0, 3)                                              # DRIFT's chor: the Juno chorus, 0 OFF, 1 I, 2 II, 3 I+II (absent = 0)


def load(m):
    """PRESETS of presets/four/<m>.py, read as source (no .pyc: an edit within the same second and size is seen)"""
    path = SRC / f"{m.lower()}.py"
    ns = {}
    exec(compile(path.read_text(), str(path), "exec"), ns)
    return ns["PRESETS"]


def wave_ranges(tables):
    t = Path(tables).read_text()
    ntbl = int(re.search(r"#define VOID_NTBL (\d+)", t).group(1))
    hid = (0, 0)                                                 # TBL2 (E2), MIX (E3), OSC2 (E6): hidden since Task 4.4 (eng_wave.c), range 0..0
    return [(0, ntbl - 1), PCT, hid, hid, PCT, PCT, hid, PCT]


def check_range(errs, who, what, v, lo, hi):
    if not isinstance(v, int) or isinstance(v, bool) or not lo <= v <= hi:
        errs.append(f"{who}: {what} = {v!r} outside {lo}..{hi}")


def validate(m, pr, rng, errs):
    allowed = CATS[m]
    if len(pr) != N:
        errs.append(f"{m}: {len(pr)} presets, need {N}")
    count = {}
    names = set()
    for k, p in enumerate(pr):
        who = f"{m}[{k}] {p.get('name')!r}"
        cat = p.get("cat")
        if cat not in allowed:
            errs.append(f"{who}: category {cat!r} not allowed for {m} ({', '.join(allowed)})")
        count[cat] = count.get(cat, 0) + 1
        nm = p.get("name", "")
        if not nm or len(nm) > 12:
            errs.append(f"{who}: name must be 1..12 characters")
        bad = sorted(set(nm) - GLYPHS)
        if bad:
            errs.append(f"{who}: characters {''.join(bad)!r} not in the large font")
        if nm in names:
            errs.append(f"{who}: duplicate name")
        names.add(nm)
        e = p.get("e", [])
        if len(e) != 8:
            errs.append(f"{who}: e needs 8 values")
        for i, v in enumerate(e[:8]):
            check_range(errs, who, f"e[{i}]", v, *rng[i])
        env = p.get("env", [])
        if len(env) != 4:
            errs.append(f"{who}: env needs 4 values")
        for i, v in enumerate(env[:4]):
            check_range(errs, who, f"env[{i}]", v, 0, 127)
        check_range(errs, who, "fenv", p.get("fenv"), -64, 63)
        check_range(errs, who, "mono", p.get("mono"), 0, 1)
        check_range(errs, who, "pat", p.get("pat", 0), 0, 9)
        fx = p.get("fx", ())
        if len(fx) != 4:
            errs.append(f"{who}: fx needs 4 sends")
        for i, v in enumerate(fx[:4]):
            check_range(errs, who, f"fx[{i}]", v, 0, 126)
        if m == "DRIFT":
            check_range(errs, who, "chor", p.get("chor", 0), *DRIFT_CHOR)
        if m == "WAVE":
            for key, (lo, hi) in WAVE_PX.items():
                check_range(errs, who, key, p.get(key), lo, hi)
            for key, n_ in (("lfo", 5), ("fe", 5)):
                v = p.get(key, [])
                if len(v) != n_:
                    errs.append(f"{who}: {key} needs {n_} values")
                for i, x in enumerate(v[:n_]):
                    check_range(errs, who, f"{key}[{i}]", x, 0, 127)
            mx = p.get("mx", [])
            if len(mx) != 4 or any(len(r) != 3 for r in mx):
                errs.append(f"{who}: mx needs 4 x 3 values")
            else:
                for j, r in enumerate(mx):
                    for i, x in enumerate(r):
                        check_range(errs, who, f"mx[{j}][{i}]", x, -64 if i == 2 else 0, 63 if i == 2 else 127)
    for cat, want in allowed.items():
        if count.get(cat, 0) != want:
            errs.append(f"{m}: {count.get(cat, 0)} {cat} presets, need {want}")


def main():
    out = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "build" / "gen" / "four_presets.h"
    tables = sys.argv[2] if len(sys.argv) > 2 else ROOT / "build" / "gen" / "void_tables.h"
    rng = dict(RANGES, WAVE=wave_ranges(tables))
    errs, sets = [], {}
    for m in MACHINES:
        sets[m] = load(m)
        validate(m, sets[m], rng[m], errs)
    if errs:
        sys.stderr.write("four_presets: " + str(len(errs)) + " problem(s)\n  " + "\n  ".join(errs) + "\n")
        return 1
    H = ["/* generated by tools/four_presets.py from presets/four/<m>.py: do not edit.",
         " * <M>_PRESETS: name, {E0..E7}, {ATK DEC SUS REL}, ENV -> FLT, mono, the sends, the pattern;",
         " * <M>_CAT: the category of each (category.c CAT_*); WAVE_PX: what a preset_t cannot hold (core.h void_px_t);",
         " * DRIFT_PX: DRIFT's chorus mode of each (P_RSV_LN, 0 OFF 1 I 2 II 3 I+II; eng_drift.c drift_track_loaded) */"]
    for m in MACHINES:
        H.append(f"static const preset_t {m}_PRESETS[] = {{")
        for p in sets[m]:
            pat = f", PAT({p['pat']})" if p.get("pat") else ""
            H.append(f'    {{"{p["name"]}", {{{", ".join(map(str, p["e"]))}}}, {{{", ".join(map(str, p["env"]))}}}, '
                     f'{p["fenv"]}, {p["mono"]}, FX({", ".join(map(str, p["fx"]))}){pat}}},')
        H += ["};", f"static const uint8_t {m}_CAT[] = {{"]
        H += [f"    CAT_{p['cat']}," for p in sets[m]]
        H.append("};")
    H += [f"static const void_px_t WAVE_PX[] = {{",
          "    /* ftype, voice (3: UNISON), detune + 1, spread, glide, lfo {set, RATE, WAVE, TRIG, FADE}, fe {LVL, A, D, S, R}, "
          "mx {SRC, DST, AMT} x 4 */"]
    for p in sets["WAVE"]:
        mx = ", ".join("{" + ", ".join(map(str, r)) + "}" for r in p["mx"])
        H.append(f'    {{{p["ftype"]}, {p["voice"]}, {p["detune"]}, {p["spread"]}, {p["glide"]}, '
                 f'{{{", ".join(map(str, p["lfo"]))}}}, {{{", ".join(map(str, p["fe"]))}}}, {{{mx}}}}},   /* {p["name"]} */')
    H.append("};")
    H += ["static const uint8_t DRIFT_PX[] = {"]
    H += [f'    {p.get("chor", 0)},   /* {p["name"]} */' for p in sets["DRIFT"]]
    H.append("};")
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text("\n".join(H) + "\n")
    print(f"four_presets: {N * len(MACHINES)} presets -> {out.relative_to(ROOT) if out.is_relative_to(ROOT) else out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
