#!/usr/bin/env python3
"""Designs the polyphase-allpass IIR half-band filters of DIST's oversampler (fx.c, FX.2).
H(z) = (A0(z^2) + z^-1 A1(z^2)) / 2, each A a cascade of first-order all-passes (a + z^-2) / (1 + a z^-2).
Minimises the worst stop-band gain for a pass-band edge fp and a count of sections per branch; prints the C tables.
  python3 tools/gen_dist_halfband.py            (needs numpy only)"""
import numpy as np, sys

def resp(a0, a1, w):
    z2 = np.exp(-2j * w)
    A0 = np.ones_like(w, complex); A1 = np.ones_like(w, complex)
    for a in a0: A0 *= (a + z2) / (1 + a * z2)
    for a in a1: A1 *= (a + z2) / (1 + a * z2)
    return 0.5 * (A0 + np.exp(-1j * w) * A1)

def design(n0, n1, fp, trials=60):                      # fp: pass-band edge as a fraction of the filter's own sample rate (< 0.25)
    ws = 2 * np.pi * (0.5 - fp)               # stop band starts at 0.5 - fp (half-band symmetry)
    w = np.linspace(ws, np.pi, 600)
    def cost(p):
        a0, a1 = p[:n0], p[n0:]
        if np.any(np.abs(p) >= 0.999): return 1e3
        h = np.abs(resp(a0, a1, w))
        return np.sum(h ** 12) ** (1 / 12)
    rng = np.random.default_rng(1)
    best = None
    for trial in range(trials):
        # start near the elliptic pattern: a0 small increasing, a1 interleaved
        k = n0 + n1
        base = np.sort(rng.uniform(0.02, 0.9, k))
        p = np.concatenate([base[0::2][:n0], base[1::2][:n1]]) if (n0 + n1) == k else base
        # Nelder-Mead
        sim = [p] + [p + 0.05 * rng.standard_normal(k) for _ in range(k)]
        f = [cost(x) for x in sim]
        for it in range(1500):
            o = np.argsort(f); sim = [sim[i] for i in o]; f = [f[i] for i in o]
            c = np.mean(sim[:-1], axis=0)
            xr = c + (c - sim[-1]); fr = cost(xr)
            if fr < f[0]:
                xe = c + 2 * (c - sim[-1]); fe = cost(xe)
                if fe < fr: sim[-1], f[-1] = xe, fe
                else: sim[-1], f[-1] = xr, fr
            elif fr < f[-2]: sim[-1], f[-1] = xr, fr
            else:
                xc = c + 0.5 * (sim[-1] - c); fc = cost(xc)
                if fc < f[-1]: sim[-1], f[-1] = xc, fc
                else:
                    sim = [sim[0]] + [sim[0] + 0.5 * (s - sim[0]) for s in sim[1:]]; f = [cost(x) for x in sim]
        o = np.argmin(f)
        if best is None or f[o] < best[0]: best = (f[o], sim[o])
    p = best[1]
    return p[:n0], p[n0:]

def report(a0, a1, fp):
    w = np.linspace(0, np.pi, 20000)
    h = np.abs(resp(a0, a1, w)); f = w / (2 * np.pi)
    pb = h[f <= fp]; sb = h[f >= 0.5 - fp]
    return 20 * np.log10(pb.min()), 20 * np.log10(pb.max()), 20 * np.log10(sb.max())

if __name__ == "__main__":
    # usage: gen_dist_halfband.py FP_HZ FS_HZ N0 N1 [trials]      (FS: the filter's output rate; e.g. 19800 88200 3 3)
    fp_hz, fs_, n0, n1 = float(sys.argv[1]), float(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4])
    a0, a1 = design(n0, n1, fp_hz / fs_, int(sys.argv[5]) if len(sys.argv) > 5 else 60)
    lo, hi, sb = report(a0, a1, fp_hz / fs_)
    print(f"pass edge {fp_hz:.0f} Hz at {fs_:.0f}: {n0}+{n1} sections: pass {lo:+.4f} .. {hi:+.4f} dB, stop {sb:.1f} dB (from {fs_ / 2 - fp_hz:.0f} Hz)")
    print("  A0", np.round(a0, 6).tolist(), "A1", np.round(a1, 6).tolist())
