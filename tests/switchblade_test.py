#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Switch Blade (Task UI.1): the boot logo's generated header and the identity that must NOT change.

  tests/switchblade_test.py [build/gen/ui_logo.h]

  - the header: 236 x 135, at most 128 colours, index 0 pure black, every index below the palette length, the
    tables `const` (flash); the generator is deterministic (twice: the same bytes);
  - the installer identity stays FM-1_940 (felucca.c FELUCCA_ID, build.py PRODUCT: fm1_install.py keys on it); the release package is named switchblade-X.Y (build.py), no felucca-X.Y left;
  - (the USB product string "Switch Blade" is checked on the real descriptors: tests/usb_desc_test.c)"""
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
hdr = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "build/gen/ui_logo.h"
fails = 0


def check(ok, what):
    global fails
    print(f"  {'ok  ' if ok else 'FAIL'} {what}")
    fails += not ok


txt = hdr.read_text()
d = {k: int(v) for k, v in re.findall(r"#define UI_LOGO_(W|H|NCOL) (\d+)", txt)}
pal = [int(v, 16) for v in re.findall(r"0x([0-9A-Fa-f]{4})", re.search(r"UI_LOGO_PAL\[[^\]]*\] = \{([^}]*)\}", txt).group(1))]
px = [int(v) for v in re.search(r"UI_LOGO_PX\[[^\]]*\] = \{([^}]*)\}", txt).group(1).replace("\n", "").split(",") if v.strip()]
check((d["W"], d["H"]) == (236, 135), f"logo is 236 x 135 ({d['W']} x {d['H']})")
check(d["NCOL"] == len(pal) and 1 <= len(pal) <= 128, f"palette length {len(pal)} (<= 128, as declared)")
check(pal[0] == 0, "index 0 is pure black (RGB565 0)")
check(len(px) == d["W"] * d["H"], f"{len(px)} indices = W x H")
check(max(px) < len(pal), f"every index below the palette length (max {max(px)})")
check(all(v <= 0xFFFF for v in pal), "palette entries are RGB565")
check(re.search(r"static const uint8_t UI_LOGO_PX", txt) and re.search(r"static const uint16_t UI_LOGO_PAL", txt) is not None,
      "both tables are const (flash, not RAM)")
check("_Static_assert(UI_LOGO_NCOL <= 128" in txt, "the header asserts its own limits at compile time")
with tempfile.TemporaryDirectory() as t:
    outs = []
    for i in range(2):
        o = Path(t) / f"logo{i}.h"
        subprocess.run([sys.executable, str(ROOT / "tools/gen_logo.py"), str(o)], check=True, capture_output=True)
        outs.append(o.read_bytes())
    check(outs[0] == outs[1] == hdr.read_bytes(), "gen_logo.py is deterministic (two runs and the build's header equal)")

fel = (ROOT / "firmware/src/felucca.c").read_text()
bld = (ROOT / "tools/build.py").read_text()
check('#define FELUCCA_ID "FM-1_940"' in fel, "firmware identity stays FM-1_940")
check('PRODUCT = "FM-1_940"' in bld and '"FM-1_94" + m[1] + m[2]' in bld, "build.py package identity stays FM-1_94XY")
check('switchblade-{a.release}.fwsc' in bld and 'felucca-{a.release}' not in bld, "release package is switchblade-X.Y")
check(not re.search(r"VOID FOUR", (ROOT / "firmware/src/ui_draw.c").read_text().split("static const char *const SPLASH_LINES")[1][:200]),
      "the splash no longer names VOID FOUR")
print("switchblade: all ok" if not fails else f"switchblade: {fails} FAILED")
sys.exit(1 if fails else 0)
