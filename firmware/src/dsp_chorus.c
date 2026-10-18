/* SPDX-License-Identifier: GPL-3.0-only */
/* dsp_chorus.c: the Juno chorus, a float port of the Ultramaster KR-106's (kayrockscreenprinting/ultramaster_kr106, GPL-3.0; Karl LaRocca)
 * Source/DSP/KR106Chorus.h and BBDFilter.h. Modes OFF / I / II / I+II as on the Juno-106: two MN3009 BBD delays modulated by one LFO in
 * antiphase (line 0 centre + depth x lfo, line 1 centre - depth x lfo; the left output is the dry plus line 0, the right the dry plus
 * line 1), each behind the donor's pre filter and in front of its post filter. Design and every departure: docs/plans/2026-10-18-drift-chorus.md.
 *
 * From the donor, verbatim: the centre delay 3.30 ms; the per-line clock trims (x0.985, x1.015); the three modes' LFO rates (0.514, 0.842,
 * 7.85 Hz), shapes (triangle, triangle, sine) and half-amplitudes (2.13, 1.71, 0.236 ms); the BBD pre / post filter (a Butterworth
 * 2nd-order TPT SVF at 9661 Hz, Q 0.7071, and a TPT one-pole at 20 kHz clamped to 0.45 fs); the 4-point Hermite read; the BBD input
 * saturation tanh(0.12 x) / 0.12; the charge-transfer gain (1 +- 0.04)(1 - 4468 (1/clock - 1/40000)); the summer, dry 0.863, wet 1.257;
 * the 5 ms crossfade on every switch (off -> mode, mode -> mode, mode -> off).
 * Not ported (the donor's character extras, below -60 dB or clicks): the injected leakage / pink / rail-ripple noise, the four biphasic
 * clock-reset clicks and their 30 Hz ring. Departures that change the arithmetic and not the sound:
 *   - one pre filter + saturation + ring for both lines (without the injected noise the donor's two are the same signal);
 *   - the saturation as its odd series, exact to 1e-5 for |0.12 x| <= 0.4 (3.3 full scale), a plain gain beyond;
 *   - the LFO phase a 32-bit integer (the rate exact), evaluated once per 8 samples and the delay, the gains and the fade ramped
 *     linearly between (the donor evaluates every sample; the error of the line is 1e-5 of the depth at the fastest mode);
 *   - the charge-transfer gain as the affine function of the delay it is (1/clock is proportional to the delay): no divide;
 *   - the delay depth is not smoothed (the wet is faded to zero at every switch, where it is configured).
 * Added: CHORUS_TRIM, a level trim folded into the two mix gains (dry 0.863 T, wet 1.257 T, T = 1/sqrt(0.863^2 + 1.257^2)), so
 * that CHOR on does not jump the part's loudness (the donor's gain sum is 2.12: +6.5 dB at low frequencies), and the fade runs between
 * the untrimmed input and the trimmed chorus, so the level is continuous across an on / off switch.
 * Float, FPU only, no libm (fastmath.h), no divide in the sample loop; the state is zero when off (the part's engine memory is zeroed
 * on a claim). One instance per DRIFT part (the summed voices), not per voice. */
#ifndef DSP_CHORUS_C
#define DSP_CHORUS_C
#include <stdint.h>
#include "fastmath.h"

#define CHORUS_FS 44100.0f
#define CHORUS_LEN 256u                  /* the ring: 5.8 ms; the longest delay is 3.30 + 2.13 ms at x1.015 = 5.51 ms (243 samples) + 2 for the Hermite */
#define CHORUS_CHUNK 32u                 /* chorus_part's float scratch (the mixer's block, CTL) */
#define CHORUS_SUB 8u                    /* the LFO, the delay and the fade are ramped over this many samples */
/* the tail: the samples after the last non-zero input until the wet is below 1 LSB of the Q15 output even at the guard (5 FS). The input, once in the ring,
 * is heard for the longest delay: 3.30 ms x 1.015 + 2.13 ms x 1.015 = 243.06 samples, plus the Hermite's 2 taps beyond it (age 245) and the write offset 1 = 246;
 * then the post filter pair's settle and the pre filter's before it, CHORUS_SETTLE each: the Butterworth pair has |z| = 0.425 (the bilinear map of g (-1 +- j)
 * / sqrt 2 with g = 0.822), so 0.425^16 = 1.2e-6, 0.2 LSB of 163,840; the 20 kHz pole (0.137) is faster. 246 + 2 x 16 = 278 samples (6.3 ms), a whole number
 * of CHORUS_SUB sub-blocks: 280. tests/chorus_test.c measures the last audible sample from a burst and pins this from both sides. */
#define CHORUS_SETTLE 16u
#define CHORUS_TAIL 280u
#define CHORUS_TRIM 0.6558515f           /* 1 / sqrt(kDry^2 + kWet^2) */
/* the guard clamp, in whole full scales (inclusive: |s| <= CHORUS_GUARD_FS). Each of L and R is held to it, so |mid| + |side| <= 5 x 32768 = 163,840:
 *   mix_part (fx.c) takes x = ((b >> 2) * LEVEL_Q12[127]) >> 10 with LEVEL_Q12[127] = 9713 (x = 77,704 per full scale), then x * gl with gl <= 4096,
 *   which has to stay below 2^31: x <= 524,287 (6.747 FS); the master forms ((mix + wet) >> 2) * mg and needs mix + wet < 2^21, four parts
 *   included (.superpowers/sdd/task-7.2a-review.md and -r2). At 5 FS: x <= 388,520 (26 % margin on x * gl), four parts 1.554 M of 2.097 M, which leaves 543 k for the wet bus. At 6 FS (466,224; 1.865 M) the wet bus did not fit:
 *   four parts held at the guard with every send 127 and a HALL (SIZE 127, DAMP 0) reached 2.36 M (tests/chorus_test.c (p); the reviewer measured a 0.5 % margin on ACID).
 *   DRIFT's voice is held to +-2 FS, so the chorus (2.12 x T = 1.39 at most) reaches 2.8 FS and never this at POLY 1; it is for DIST (a mono sum
 *   up to 7 FS), the follow-up polyphony, and a runaway or a NaN, which never reach the integer conversion. */
#define CHORUS_GUARD_FS 5

/* the donor's filter pair (BBDFilter.h): tan(pi 9661 / 44100) = g, a1 = 1 / (1 + g / Q + g^2), the pole's g / (1 + g) at min(20 kHz, 0.45 fs) */
#define CH_G 0.82236413f
#define CH_A1 0.35220042f
#define CH_A1G 0.28963700f
#define CH_GP 0.86327126f
/* the mixer: the fade 0 is the input as it is, the fade 1 the chorus: out = dm x + wm wet, dm = 1 - fade (1 - kDry T), wm = fade kWet T */
#define CH_DRY_T (0.863f * CHORUS_TRIM)
#define CH_WET_T (1.257f * CHORUS_TRIM)
#define CH_FADE_STEP (8.0f / 220.5f)     /* the 5 ms crossfade, per CHORUS_SUB samples */
/* the delays in samples: centre 3.30 ms x the clock trim; the per-mode half-amplitude in samples x the trim */
#define CH_C0 143.34705f                 /* 3.30 ms x 0.985 x 44.1 */
#define CH_C1 147.71295f                 /* 3.30 ms x 1.015 x 44.1 */
/* the charge-transfer gain (1 +- 0.04)(1 - 4468 (d_ms 7.8125e-6 - 2.5e-5)) with d_ms = d / 44.1: ga - gb d */
#define CH_GA0 1.156168f
#define CH_GB0 0.00082318594f
#define CH_GA1 1.067232f
#define CH_GB1 0.00075986395f

typedef struct {
    float buf[CHORUS_LEN];               /* the shared BBD line: the pre-filtered, saturated input */
    float pre_bq[2], pre_p;              /* the shared pre filter: biquad states, pole state */
    float post_bq[2][2], post_p[2];      /* the post filters, per line */
    float d0, d1, dd0, dd1;              /* the delays (samples) of line 0 / 1 now and their step per sample */
    float dm, wm, ddm, dwm;              /* the mix gains now and their step per sample */
    float fade;                          /* the crossfade at the end of the sub-block (the wet, 0..1) */
    float k0, k1;                        /* the mode's delay half-amplitude in samples, per line */
    uint32_t w;                          /* the write position */
    uint32_t ph, inc;                    /* the LFO phase (2^32 = one turn) and its step per sample */
    uint8_t on, mode, req, sub;          /* running; the configured mode (1..3); the requested one; the sample in the sub-block */
    uint8_t sine, pad;                   /* the LFO shape: 1 sine (I+II), 0 triangle */
    uint16_t zrun;                       /* chorus_part: the samples of exact zero input in a row (saturating); CHORUS_TAIL of them and the line has rung out */
} chorus_t;
#define CHORUS_MEM (sizeof(chorus_t))

/* the LFO, -1 .. 1, at the phase (a turn = 2^32) */
static inline float chorus_lfo(const chorus_t *c)
{
    float t = (float)(c->ph >> 8) * (1.0f / 16777216.0f);
    if (c->sine)
        return fm_sin_turns(t);
    t -= 0.5f;
    return 1.0f - 4.0f * (t < 0.0f ? -t : t);
}

static void chorus_cfg(chorus_t *c, uint32_t mode)
{
    static const uint32_t INC[4] = {0u, 50059u, 82004u, 764524u};                    /* 0.514, 0.842, 7.85 Hz / 44100 x 2^32 */
    static const float DEPTH[4] = {0.0f, 2.13f, 1.71f, 0.236f};                       /* ms, half the swing */
    c->mode = (uint8_t)mode;
    c->inc = INC[mode];
    c->sine = mode == 3u;
    c->k0 = DEPTH[mode] * 0.985f * (CHORUS_FS * 0.001f);
    c->k1 = DEPTH[mode] * 1.015f * (CHORUS_FS * 0.001f);
}

/* one sub-block of m <= CHORUS_SUB samples; the LFO, the delays and the gains ramp from where the last one ended */
static inline void chorus_run(chorus_t *c, const float *in, float *l, float *r, uint32_t m)
{
    float d0 = c->d0, d1 = c->d1, dd0 = c->dd0, dd1 = c->dd1, dm = c->dm, wm = c->wm, ddm = c->ddm, dwm = c->dwm;
    float pre1 = c->pre_bq[0], pre2 = c->pre_bq[1], prep = c->pre_p;
    float p0a = c->post_bq[0][0], p0b = c->post_bq[0][1], p1a = c->post_bq[1][0], p1b = c->post_bq[1][1], pp0 = c->post_p[0], pp1 = c->post_p[1];
    uint32_t w = c->w, i;
    for (i = 0; i < m; i++) {
        float x = in[i], v1, v2, f, u, wet0, wet1, uu, g0, g1;
        int32_t i0, i1;
        float f0, f1, a0, b0, a1, b1;
        d0 += dd0;
        d1 += dd1;
        dm += ddm;
        wm += dwm;
        /* the shared pre filter (a Butterworth biquad, then the 20 kHz pole), then the BBD input saturation */
        v1 = CH_A1 * pre1 + CH_A1G * (x - pre2);
        v2 = pre2 + CH_G * v1;
        pre1 = 2.0f * v1 - pre1;
        pre2 = 2.0f * v2 - pre2;
        f = (v2 - prep) * CH_GP;
        u = prep + f;
        prep = u + f;
        uu = u * 0.12f;
        uu = uu > 0.4f ? 0.4f : uu < -0.4f ? -0.4f : uu;
        uu *= uu;
        f = u * (1.0f - uu * (0.33333333f - 0.13333333f * uu));
        c->buf[w & (CHORUS_LEN - 1u)] = f;
        /* the two Hermite taps: the delay d = i + fr, read at w - i - 1 + (1 - fr) */
        i0 = (int32_t)d0;
        i1 = (int32_t)d1;
        f0 = 1.0f - (d0 - (float)i0);
        f1 = 1.0f - (d1 - (float)i1);
        {
            uint32_t q = w - (uint32_t)i0;
            float y0 = c->buf[(q - 2u) & (CHORUS_LEN - 1u)], y1 = c->buf[(q - 1u) & (CHORUS_LEN - 1u)];
            float y2 = c->buf[q & (CHORUS_LEN - 1u)], y3 = c->buf[(q + 1u) & (CHORUS_LEN - 1u)];
            a0 = 0.5f * (y2 - y0);
            b0 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
            g0 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
            wet0 = ((g0 * f0 + b0) * f0 + a0) * f0 + y1;
        }
        {
            uint32_t q = w - (uint32_t)i1;
            float y0 = c->buf[(q - 2u) & (CHORUS_LEN - 1u)], y1 = c->buf[(q - 1u) & (CHORUS_LEN - 1u)];
            float y2 = c->buf[q & (CHORUS_LEN - 1u)], y3 = c->buf[(q + 1u) & (CHORUS_LEN - 1u)];
            a1 = 0.5f * (y2 - y0);
            b1 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
            g1 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
            wet1 = ((g1 * f1 + b1) * f1 + a1) * f1 + y1;
        }
        w++;
        /* the post filters, then the charge-transfer gain and the summer */
        v1 = CH_A1 * p0a + CH_A1G * (wet0 - p0b);
        v2 = p0b + CH_G * v1;
        p0a = 2.0f * v1 - p0a;
        p0b = 2.0f * v2 - p0b;
        f = (v2 - pp0) * CH_GP;
        wet0 = pp0 + f;
        pp0 = wet0 + f;
        v1 = CH_A1 * p1a + CH_A1G * (wet1 - p1b);
        v2 = p1b + CH_G * v1;
        p1a = 2.0f * v1 - p1a;
        p1b = 2.0f * v2 - p1b;
        f = (v2 - pp1) * CH_GP;
        wet1 = pp1 + f;
        pp1 = wet1 + f;
        l[i] = dm * x + wm * ((CH_GA0 - CH_GB0 * d0) * wet0);
        r[i] = dm * x + wm * ((CH_GA1 - CH_GB1 * d1) * wet1);
    }
    c->d0 = d0, c->d1 = d1, c->dm = dm, c->wm = wm, c->w = w;
    c->pre_bq[0] = fm_flush(pre1), c->pre_bq[1] = fm_flush(pre2), c->pre_p = fm_flush(prep);   /* (a silent input would run the states into the denormals) */
    c->post_bq[0][0] = fm_flush(p0a), c->post_bq[0][1] = fm_flush(p0b), c->post_bq[1][0] = fm_flush(p1a), c->post_bq[1][1] = fm_flush(p1b);
    c->post_p[0] = fm_flush(pp0), c->post_p[1] = fm_flush(pp1);
}

/* The edge of a sub-block: the mode switch (at fade 0), then the ramps to the values CHORUS_SUB samples on. 0 = the chorus has faded out and stops. */
static inline int chorus_edge(chorus_t *c)
{
    float tgt, fn, lfo;
    if (c->fade <= 0.0f && c->req != c->mode) {
        if (!c->req) {
            c->on = 0;
            return 0;
        }
        chorus_cfg(c, c->req);
    }
    tgt = c->req == c->mode ? 1.0f : 0.0f;
    fn = c->fade < tgt ? c->fade + CH_FADE_STEP : c->fade - CH_FADE_STEP;
    fn = c->fade < tgt ? (fn > tgt ? tgt : fn) : (fn < tgt ? tgt : fn);
    c->ddm = ((1.0f - fn * (1.0f - CH_DRY_T)) - c->dm) * (1.0f / (float)CHORUS_SUB);
    c->dwm = (fn * CH_WET_T - c->wm) * (1.0f / (float)CHORUS_SUB);
    c->fade = fn;
    c->ph += c->inc * CHORUS_SUB;
    lfo = chorus_lfo(c);
    c->dd0 = ((CH_C0 + c->k0 * lfo) - c->d0) * (1.0f / (float)CHORUS_SUB);
    c->dd1 = ((CH_C1 - c->k1 * lfo) - c->d1) * (1.0f / (float)CHORUS_SUB);
    return 1;
}

/* n samples of a mono input into L and R, for the requested mode (0 = OFF, 1 I, 2 II, 3 I+II; anything else is OFF). Returns 1 when the chorus
 * was running for the block (L / R are the stereo pair; the sub-block it faded out in ends as a copy of the input), 0 when it was off (L = R = input).
 * Any block size gives the same bits: the sub-blocks sit on a grid of samples counted from the start. */
static int chorus_block(chorus_t *c, uint32_t req, const float *in, float *l, float *r, uint32_t n)
{
    uint32_t i = 0, m;
    int ran = 0;
    if (req > 3u)
        req = 0u;
    if (!c->on) {
        if (!req) {
            for (; i < n; i++)
                l[i] = r[i] = in[i];
            return 0;
        }
        {   /* start: a clean line and filters (never stale audio), the LFO at phase 0, as the donor's Init */
            uint32_t *z = (uint32_t *)c, k;
            for (k = 0; k < sizeof *c / 4u; k++)
                z[k] = 0u;
        }
        c->on = 1;
        {   /* the donor's line is always running, so its ring holds the signal when the chorus engages. Ours starts at the first sample: the ring and
             * the filters are set to the steady state of a constant input of it, so that no step from silence is heard a delay later */
            float x0 = in[0], uu = x0 * 0.12f, f0;
            uu = uu > 0.4f ? 0.4f : uu < -0.4f ? -0.4f : uu;
            uu *= uu;
            f0 = x0 * (1.0f - uu * (0.33333333f - 0.13333333f * uu));
            for (m = 0; m < CHORUS_LEN; m++)
                c->buf[m] = f0;
            c->pre_bq[1] = c->pre_p = x0;
            c->post_bq[0][1] = c->post_bq[1][1] = c->post_p[0] = c->post_p[1] = f0;
        }
        chorus_cfg(c, req);
        c->dm = 1.0f;
        c->d0 = CH_C0 + c->k0 * chorus_lfo(c);
        c->d1 = CH_C1 - c->k1 * chorus_lfo(c);
    }
    c->req = (uint8_t)req;
    ran = 1;
    while (i < n) {
        if (!c->sub && !chorus_edge(c)) {
            for (; i < n; i++)
                l[i] = r[i] = in[i];
            break;
        }
        m = CHORUS_SUB - c->sub;
        m = m < n - i ? m : n - i;
        chorus_run(c, in + i, l + i, r + i, m);
        c->sub = (uint8_t)((c->sub + m) & (CHORUS_SUB - 1u));
        i += m;
    }
    return ran;
}

/* The part's block, for fx.c's hook: b[0..n) the mono sum (Q15 full scale 32768), out: b the mid (L + R) / 2 and z the side (L - R) / 2 of the
 * chorus' pair, in the same scale. Returns 0, b untouched, when the mode is OFF (0, or anything outside 0..3: the old reserved lane level 127 a
 * stored project carries) and the chorus is not fading out. Each of L and R is held to CHORUS_GUARD_FS before the integer conversion. */
static int chorus_part(chorus_t *c, int32_t mode, int32_t *b, int32_t *z, uint32_t n)
{
    float in[CHORUS_CHUNK], l[CHORUS_CHUNK], r[CHORUS_CHUNK];
    uint32_t req = (uint32_t)mode <= 3u ? (uint32_t)mode : 0u, o, i, m, zr;
    int ran = 0;
    if (!c->on && !req)
        return 0;
    zr = c->zrun;
    for (o = 0; o < n; o += m) {
        m = n - o < CHORUS_CHUNK ? n - o : CHORUS_CHUNK;
        for (i = 0; i < m; i++)
            in[i] = (float)b[o + i] * (1.0f / 32768.0f);
        for (i = m; i > 0u && !b[o + i - 1u]; i--)         /* the zeros at the end of the chunk (one compare for a chunk that ends in sound) */
            ;
        zr = i ? m - i : (zr + m < 65535u ? zr + m : 65535u);   /* (saturating) */
        if (!c->on && !i) {                                 /* idle and silent (the tail has rung out): never starts on nothing; b is zero already */
            for (i = 0; i < m; i++)
                z[o + i] = 0;
            continue;
        }
        ran |= chorus_block(c, req, in, l, r, m);
        c->zrun = (uint16_t)zr;                             /* (after: a start clears the state) */
        if (zr >= CHORUS_TAIL)                         /* nothing went in for the whole line: it has rung out, stop (the next start is clean) */
            c->on = 0;
        for (i = 0; i < m; i++) {
            float sl = l[i] * 32768.0f, sr = r[i] * 32768.0f;
            int32_t li, ri;
            if (!(sl > -(float)(CHORUS_GUARD_FS * 32768) && sl < (float)(CHORUS_GUARD_FS * 32768)))   /* a runaway or a NaN never reaches the integer conversion */
                sl = sl >= (float)(CHORUS_GUARD_FS * 32768) ? (float)(CHORUS_GUARD_FS * 32768) : sl <= -(float)(CHORUS_GUARD_FS * 32768) ? -(float)(CHORUS_GUARD_FS * 32768) : 0.0f;
            if (!(sr > -(float)(CHORUS_GUARD_FS * 32768) && sr < (float)(CHORUS_GUARD_FS * 32768)))
                sr = sr >= (float)(CHORUS_GUARD_FS * 32768) ? (float)(CHORUS_GUARD_FS * 32768) : sr <= -(float)(CHORUS_GUARD_FS * 32768) ? -(float)(CHORUS_GUARD_FS * 32768) : 0.0f;
            li = (int32_t)sl;
            ri = (int32_t)sr;
            b[o + i] = (li + ri) >> 1;
            z[o + i] = (li - ri) >> 1;
        }
    }
    return ran;
}
#endif
