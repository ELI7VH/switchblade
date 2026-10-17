#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Regenerates tests/acid_ref/ from the donor (fm1-x0x's bass303.c, never edited): see tests/acid_ref.c.
#   sh tests/acid_ref.sh            (from the repo root; DONOR_DIR overrides ../fm1-x0x-main/firmware/src/dsp)
set -e
D="${DONOR_DIR:-../fm1-x0x-main/firmware/src/dsp}"
O=build/host
mkdir -p "$O/acid_seam" tests/acid_ref
cc -O2 -w -ffp-contract=off -I"$D" -Itests -o "$O/acid_ref" tests/acid_ref.c -lm
"$O/acid_ref" tests/acid_ref
# the IMPULSE build: a temp copy of the donor with the one seam line added
cp "$D"/bass303.c "$D"/bass303.h "$D"/fastmath.h "$D"/x0x_param.h "$O/acid_seam/"
python3 - "$O/acid_seam/bass303.c" <<'PY'
import sys
p = sys.argv[1]
s = open(p).read()
a = "ym = hp1_b0 * (s - hp1x) + hp1_a1 * hp1y;"
assert s.count(a) == 1
open(p, "w").write(s.replace(a, "s = B3_IN(s);\n                    " + a))
PY
cc -O2 -w -ffp-contract=off -DIMPULSE -I"$O/acid_seam" -Itests -o "$O/acid_ref_imp" tests/acid_ref.c -lm
"$O/acid_ref_imp" tests/acid_ref
