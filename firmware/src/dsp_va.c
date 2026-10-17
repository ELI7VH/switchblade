/* SPDX-License-Identifier: GPL-3.0-only */
/* dsp_va.c: float virtual-analogue primitives for DRIFT, the Juno voice. FPU only (D10), no libm (fastmath.h). DRIFT is the
 * only consumer.
 *
 * Ported from the Ultramaster KR-106 (kayrockscreenprinting/ultramaster_kr106, GPL-3.0; Karl LaRocca, after the Ultramaster
 * Group), the donor's Source/DSP/KR106VCF_OPTIMIZED.h (kr106::VCF: va_ladder_*) and Source/DSP/KR106Resampler.h (the
 * Upsampler2x / Downsampler2x pair: va_hb_*; its 12 coefficients are Laurent de Soras' HIIR half-band, WTFPL). The donor's
 * comments explaining the circuit are kept: they are the reason the constants are what they are. See LICENSES/GPL-3-KR106.txt.
 *
 * What differs from the donor, all of it in how it is written and none in what it computes (tests/drift_test.c (a) holds
 * the output to the donor's own kr106::VCF within 1e-4 of the peak, 40 cases):
 *   - tan / exp / 1/sqrt come from fastmath.h (rel 1e-6 / 6e-7 / 5e-6), not libm;
 *   - the OTA tanh, its derivative and the Newton step share one divide per stage (the Pade denominator d is common);
 *   - the donor's mJ106Res is always true (DRIFT is a Juno-106): the J6 resonance curve is not carried;
 *   - the struct holds the donor's cached Coeffs and the thermal-noise state itself, and the donor's SetSampleRate /
 *     SetOversample are va_ladder_setup.
 * Interface of the brief, plus va_ladder_setup (oversampling, the follower's span) and va_ladder_env (the donor's
 * TrackInputEnv) that the brief's four functions leave out. */
#pragma once
#include <stdint.h>
#include "fastmath.h"

/* ============================================================
 * BA662 feedback architecture -- circuit-accurate OTA model (the donor's note, abridged).
 *
 * Hardware signal path (from the Juno-106 schematic):
 *   OscMix --10K--> Filter Input Node <-- OTA Output (current)
 *   OscMix --47K--> OTA +IN --1.5K--> GND
 *   FilterOut-100K-> OTA -IN --1.5K--> GND
 * The input and the filter output BOTH enter the BA662 OTA's differential pair. The input is attenuated 1.5/(47+1.5) = 0.0309,
 * the feedback 1.5/(100+1.5) = 0.0148: the input is 2.09x less attenuated. The OTA's output current (Gm x tanh of the
 * differential) sums with the direct input current (OscMix/10K) at the filter input node. So resonance naturally boosts
 * the input, because the OTA amplifies a differential that contains the input on +IN; at high cutoff FilterOut ~ Input, the
 * differential approaches zero and the OTA's share vanishes, with no explicit frequency-dependent correction.
 *
 * The filter: four cascaded 1-pole TPT (trapezoidal) integrators with global resonance feedback and a per-stage OTA tanh
 * (the IR3109 model). The state variables are integrator outputs, coefficient-independent, so the cutoff can move every
 * sample without artifacts. It runs at os x the base rate (1, 2 or 4); the caller drives va_ladder_tick at that rate and
 * decimates (va_hb_down) itself.
 * ============================================================ */
typedef struct {
    float s[4];                  /* integrator states */
    float k, g, g1, g1_2, g1_3;  /* feedback gain; g = tan(), peak-pull corrected; g/(1+g) and its powers */
    int os;                      /* oversampling: 1, 2, 4 (0 = not set up: treated as 1) */
    /* the donor's cached Coeffs, its thermal-noise state and its follower */
    float G, inv_den;            /* g1^4; 1 / (1 + k G) */
    float fb_scale, inv_fb;      /* the feedback OTA's scale and its inverse */
    float c0, c1, c2, c3;        /* the linear predictor's weights, (1 - g1) g1^3 .. (1 - g1), times fb_scale */
    float xa, xg, ko;            /* 0.065 fb_scale; 0.142 / (1 + k G); k / (1 + k G) / fb_scale */
    float noise_lvl;             /* adaptive thermal noise, set by va_ladder_coeffs */
    float env, edec;             /* peak follower (noise suppression) and its per-call decay */
    float cfrq, cres;            /* the (frq, res) the coefficients were computed for; cfrq < 0 = none */
    uint32_t seed;               /* the noise PRNG */
} va_ladder_t;

#define VA_OTA_SCALE 0.35f       /* per-stage OTA drive (the donor's kOTAScaleBase) */
#define VA_DIRECT_GAIN 0.142f    /* OscMix through 10K to the filter input node: the passband level at R = 0. With the
                                    output gain 8.59, kDirectGain x 8.59 = 1.22 (passband near unity) */
#define VA_INPUT_RATIO 0.065f    /* the BA662's input/feedback ratio in DSP-normalised levels (hardware 2.09; the old
                                    InputComp(k) = 0.142 + 0.065 k slope: linearised input gain = direct + k x ratio) */
#define VA_OUT_GAIN 8.59f

/* clears the filter's state, the follower and the cache; keeps os and edec */
static void va_ladder_reset(va_ladder_t *f)
{
    f->s[0] = f->s[1] = f->s[2] = f->s[3] = 0.0f;
    f->env = 0.0f;
    f->cfrq = -1.0f;
    f->cres = -1.0f;
    f->seed = 123456789u;
    f->noise_lvl = 0.0f;
}

/* os: 1, 2 or 4. span: base-rate samples between two va_ladder_env calls (the donor calls TrackInputEnv once per base sample:
 * span 1). sr: the base rate. The follower's decay is the donor's 22 ms time constant over the span */
static void va_ladder_setup(va_ladder_t *f, int os, int span, float sr)
{
    f->os = os <= 1 ? 1 : os <= 2 ? 2 : 4;
    f->edec = fm_expf(-(float)span / (0.022f * sr));
    f->cfrq = -1.0f;
}

/* TrackInputEnv: the peak envelope follower for the noise suppression. peak = the oscillator's peak since the last call */
static inline void va_ladder_env(va_ladder_t *f, float peak)
{
    float a = fm_fabsf(peak), d = f->env * f->edec;
    f->env = a > d ? a : d;
}

/* Resonance slider -> feedback gain k for the J106: a quartic polynomial fitted to hardware resonance-sweep data
 * (res_calibrate, April 2026). Noise through the VCF at a fixed cutoff (byte 64, ~800 Hz), peak-relative-to-passband measured
 * at 29 resonance steps via Welch PSD; the peak growth curve inverted through the DSP's own transfer function to recover the k
 * values that produce the measured peak heights. The polynomial reaches k = 3.42 at R = 127; a quadratic knee above fader
 * position 5.5 adds up to 0.35 to match the hardware curve's steep onset; a boost above fader position 9 pushes k past 4.0
 * for the self-oscillation onset. */
static inline float va_res_k(float res)
{
    float r = res, r2 = r * r, r3 = r2 * r, r4 = r2 * r2;
    float k = 4.6265f * r + 4.3866f * r2 - 8.9869f * r3 + 3.3902f * r4, boost;
    if (r > 0.7f) {
        float knee = (r - 0.7f) / 0.3f, add = knee * knee * 1.2f;
        k += add < 0.35f ? add : 0.35f;
    }
    boost = (r - 0.898f) * (0.17f / 0.102f);
    boost = boost > 0.0f ? boost : 0.0f;
    return k * (1.0f + boost);
}

/* Control-rate coefficient update. cutoff_hz and res (0..1, the slider); sr the BASE rate. Cached on the normalised cutoff
 * and res, so a held note pays only the noise level. Call once per base sample (the donor) or per few: the states are
 * coefficient-independent. */
static void va_ladder_coeffs(va_ladder_t *f, float cutoff_hz, float res, float sr)
{
    float frq = cutoff_hz / (sr * 0.5f);
    float osf = (float)(f->os < 1 ? 1 : f->os), energy, se;
    if (frq != f->cfrq || res != f->cres) {
        float k = va_res_k(res), over, g, g1, g1_2, G, kfit, logm, fade;
        f->cfrq = frq;
        f->cres = res;
        /* Anti-aliasing at 1x OS. At 1x there is no decimator after the VCF -- the filter output goes straight to the mix
         * bus. Any resonant peak above frq ~ 0.7 (output Nyquist x 0.7) sits too close to Nyquist and folds audibly. Fade k to
         * zero across frq 0.7 -> 0.9 so even broad (non-self-oscillating) peaks collapse before reaching Nyquist. At 2x and 4x
         * this is not needed: the decimator's stopband catches near-Nyquist content on the way out. */
        if (f->os <= 1 && frq > 0.7f) {
            fade = 1.0f - (frq - 0.7f) / 0.2f;
            k *= fade > 0.0f ? fade : 0.0f;
        }
        f->k = k;
        /* frqOver is clamped at 0.85 as a numerical safety bound: at 0.85, g = tan(0.85 pi/2) ~ 6.3, near the top of where the
         * bilinear-prewarped integrators stay well-behaved. At 2x OS the largest frq gives frqOver = 0.5, at 4x 0.25, both well
         * inside; the clamp only engages at 1x, where the k fade above frq 0.7 has already collapsed the resonance. */
        over = frq / osf;
        over = over < 0.85f ? over : 0.85f;
        g = fm_tanf(over * FM_PI * 0.5f);
        /* Peak-pull compensation -- empirical fit from the donor's hardware noise grid. A bilinear 4-pole TPT cascade
         * intrinsically pulls its peak below the nominal cutoff, but the hardware shows only a mild, roughly constant pull
         * (~+150..300 cents across k = 1..3.5, falling to 0 at k >= 4); the theoretical linear-bilinear pull overcompensates at
         * low k, likely because the hardware BA662 loop's phase differs from the ideal. The fit is a 3rd-order polynomial in k,
         * calibrated to the 10 x 10 noise grid. Edge fades: below k = 1 the filter is in the pure-rolloff regime with no
         * resonant peak to compensate, so the correction fades to 0 linearly; above k = 3.7, approaching self-oscillation, the
         * natural pull collapses to 0 on the hardware (the peak sits on the target at R = 127), so it fades to 0 by k = 4.4 to
         * avoid pushing the self-oscillation pitch above the target. */
        kfit = k < 0.0f ? 0.0f : k > 5.0f ? 5.0f : k;
        logm = 0.20646f + kfit * (-0.04140f + kfit * (0.00602f - kfit * 0.00127f));
        if (k < 1.0f)
            logm *= k > 0.0f ? k : 0.0f;
        if (k > 3.7f) {
            fade = (4.4f - k) / 0.7f;
            fade = fade < 0.0f ? 0.0f : fade > 1.0f ? 1.0f : fade;
            logm *= fade;
        }
        g *= fm_expf(logm);
        g = g < 10.0f ? g : 10.0f;
        g1 = g / (1.0f + g);
        g1_2 = g1 * g1;
        G = g1_2 * g1_2;
        f->g = g;
        f->g1 = g1;
        f->g1_2 = g1_2;
        f->g1_3 = g1_2 * g1;
        f->G = G;
        f->inv_den = 1.0f / (1.0f + k * G);
        f->fb_scale = 4.20f * (k - 2.5f < 0.3f ? 0.3f : k - 2.5f > 1.0f ? 1.0f : k - 2.5f);
        f->inv_fb = 1.0f / f->fb_scale;
        {   /* what the sample loop folds: S and the OTA's argument scaled once, here */
            float o = (1.0f - g1) * f->fb_scale;
            f->c0 = o * f->g1_3;
            f->c1 = o * g1_2;
            f->c2 = o * g1;
            f->c3 = o;
            f->xa = VA_INPUT_RATIO * f->fb_scale;
            f->xg = VA_DIRECT_GAIN * f->inv_den;
            f->ko = k * f->inv_den * f->inv_fb;
        }
    }
    /* Adaptive noise level: runs every call, because the follower and the state energy change with the signal. When the filter
     * is loud the noise floor shrinks (so it is not amplified through the resonant peak); when quiet it can be larger (it
     * seeds self-oscillation on quiet patches). Real analog filters self-oscillate from thermal noise; digital ones are
     * silent at zero input. */
    se = fm_fabsf(f->s[0]) + fm_fabsf(f->s[1]) + fm_fabsf(f->s[2]) + fm_fabsf(f->s[3]);
    energy = f->env > se ? f->env : se;
    f->noise_lvl = 1e-2f / (osf * (1.0f + energy * 1000.0f));
}

/* The audio-rate sample, called os x per base sample. The OTA stage solves y = s + g tanh(x - y) by one Newton-Raphson
 * iteration from the linear TPT estimate, which avoids the zipper artifacts the explicit form has at high cutoff and
 * resonance. The tanh is the Pade x (27 + x^2) / (27 + 9 x^2) (|x| > 3: +-1, derivative 0).
 *
 * The donor evaluates f = y0 - s - g t and df = 1 + g t' for y0 = s + g1 (x - s) and subtracts f / df. Written out, with
 * xs = 0.35 (x - y0) = 0.35 (1 - g1)(x - s), w = xs^2 and d = 27 + 9 w, the linear parts cancel exactly (g1 / (1 - g1) = g):
 *     f = 8 g xs w / (0.35 d),   df = (d^2 + 27 g (27 - 3 w)) / d^2,   so   y = y0 - 8 g xs w d / (0.35 (d^2 + 27 g (27 - 3 w)))
 * the same number with one divide, no cancellation, and about 19 operations a stage in place of 30. Saturated (|xs| > 3: tanh
 * = +-1, slope 0) the stage is s +- g / 0.35. */
static inline float va_stage(float *s, float x, float h, float g1, float kg, float g27, float gk)
{
    float dxs = x - *s, xs = dxs * h, y;
    if (xs > 3.0f || xs < -3.0f) {
        y = xs > 0.0f ? *s + gk : *s - gk;
    } else {
        float w = xs * xs, d = 27.0f + 9.0f * w, dd = d * d + g27 * (27.0f - 3.0f * w);
        y = *s + g1 * dxs - kg * xs * w * d / dd;
    }
    *s = 2.0f * y - *s;
    return y;
}

static inline float va_ota_sat(float x)       /* the feedback OTA's tanh (Pade), clamped past +-3 */
{
    float x2;
    if (x > 3.0f)
        return 1.0f;
    if (x < -3.0f)
        return -1.0f;
    x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

static inline float va_ladder_tick(va_ladder_t *f, float x)
{
    float S, u, amp, q, sq, g1nl, gnl, lp;
    union { uint32_t u; float f; } n;
    /* thermal noise: a deterministic LCG, a white float in [-1, 1) */
    f->seed = f->seed * 196314165u + 907633515u;
    n.u = (f->seed >> 9) | 0x40000000u;
    x += (n.f - 3.0f) * f->noise_lvl;
    /* a 1e-20 DC bias on s[0] flushes denormals without an FTZ flag (about 130 dB below anything audible) */
    f->s[0] += 1e-20f;
    /* Linear cascade predictor: y4 when u = 0. The (1 - g1) factor comes from the last stage's output = g1 input_prev +
     * (1 - g1) s_last, and propagates through to all state contributions. Missing this factor doubles the effective k at
     * g1 = 0.5 (cutoff near Fs/4), which made the filter self-oscillate at a much lower nominal k than it should. */
    S = f->s[0] * f->c0 + f->s[1] * f->c1 + f->s[2] * f->c2 + f->s[3] * f->c3;   /* (the weights carry fb_scale and 1 - g1) */
    /* The BA662 OTA: input and feedback share the differential pair (+IN: the oscillator mix x 1.5/(47+1.5); -IN: the
     * filter output x 1.5/(100+1.5)). In DSP-normalised units the input enters at VA_INPUT_RATIO relative to the feedback S.
     * Self-oscillation (input = 0): the differential is -S, identical to a feedback-only model. Low cutoff (S ~ 0): the OTA
     * boosts the input in proportion to k, a natural passband compensation. High cutoff (S ~ input x gain): the differential
     * partly cancels and the passband returns to its uncompensated level. The filter input is the direct path (10K) + the
     * resonance-controlled OTA output, over 1 + k G:
     *     u = (x 0.142 + k sat((0.065 x - S) fb_scale) / fb_scale) / (1 + k G)                                              */
    u = x * f->xg + va_ota_sat(x * f->xa - S) * f->ko;
    /* Per-stage describing-function pitch compensation: at a high state amplitude (self-oscillation, large feedback) the tanh
     * in a stage effectively lowers its gain, which shifts the pitch; dfGain pushes g1 up when the state amplitude is large.
     * It works alongside the k-based compensation in va_ladder_coeffs, which handles the linear bilinear warping; this
     * handles the nonlinear, saturation-induced pitch shift that only appears in self-oscillation. g1 / dfGain with
     * dfGain = max(1/sqrt(q), 0.65), q = 1 + 0.6 a^2, is g1 x min(sqrt(q), 1/0.65). For a < 0.1 (a state this small is the
     * ordinary playing level) sqrt(q) = 1 + 0.3 a^2 - 0.045 a^4 + ..., the series to a^2 with an error under 5e-6. */
    amp = fm_fabsf(f->s[3]);
    if (amp < 0.1f)
        sq = 1.0f + 0.3f * amp * amp;
    else {
        q = 1.0f + 0.6f * amp * amp;
        sq = q * fm_rsqrtf(q);
    }
    g1nl = f->g1 * (sq < 1.53846154f ? sq : 1.53846154f);
    g1nl = g1nl < 0.98f ? g1nl : 0.98f;
    gnl = g1nl / (1.0f - g1nl);
    {
        float h = VA_OTA_SCALE * (1.0f - g1nl), kg = (8.0f / VA_OTA_SCALE) * gnl, g27 = 27.0f * gnl, gk = gnl * (1.0f / VA_OTA_SCALE);
        lp = va_stage(&f->s[0], u, h, g1nl, kg, g27, gk);
        lp = va_stage(&f->s[1], lp, h, g1nl, kg, g27, gk);
        lp = va_stage(&f->s[2], lp, h, g1nl, kg, g27, gk);
        lp = va_stage(&f->s[3], lp, h, g1nl, kg, g27, gk);
    }
    /* output gain: passband makeup for the 4-pole cascade (direct gain x 8.59 = 1.22: unity near R = 0) */
    return lp * VA_OUT_GAIN;
}

/* n samples through the filter. The state is copied to locals for the run, so it and the coefficients stay in registers
 * (the same arithmetic as n calls of va_ladder_tick) */
static void va_ladder_run(va_ladder_t *f, const float *x, float *y, int n)
{
    va_ladder_t l = *f;
    int i;
    for (i = 0; i < n; i++)
        y[i] = va_ladder_tick(&l, x[i]);
    *f = l;
}

/* ============================================================
 * Half-band polyphase IIR resampler: 12-coefficient allpass network for 2x up / downsampling. The same algorithm and
 * coefficients as Laurent de Soras' HIIR library (WTFPL). One state type for both directions (a voice uses one of them).
 * ============================================================ */
#define VA_HB_N 12
typedef struct { float x[VA_HB_N], y[VA_HB_N]; } va_hb_t;

static const float VA_HB_COEF[VA_HB_N] = {
    0.036681502163648017f, 0.13654762463195794f, 0.27463175937945444f, 0.42313861743656711f,
    0.56109869787919531f,  0.67754004997416184f, 0.76974183386322703f, 0.83988962484963892f,
    0.89226081800387902f,  0.9315419599631839f,  0.96209454837808417f, 0.98781637073289585f
};

static void va_hb_reset(va_hb_t *h)
{
    int i;
    for (i = 0; i < VA_HB_N; i++)
        h->x[i] = h->y[i] = 0.0f;
}

/* 1 in, 2 out (the first sample of the pair in *a, the second in *b) */
static inline void va_hb_up(va_hb_t *h, float x, float *a, float *b)
{
    float even = x, odd = x;
    int i;
    for (i = 0; i < VA_HB_N; i += 2) {
        float t0 = (even - h->y[i]) * VA_HB_COEF[i] + h->x[i];
        float t1 = (odd - h->y[i + 1]) * VA_HB_COEF[i + 1] + h->x[i + 1];
        h->x[i] = even;
        h->x[i + 1] = odd;
        h->y[i] = t0;
        h->y[i + 1] = t1;
        even = t0;
        odd = t1;
    }
    *a = even;
    *b = odd;
}

/* 2 in (a the older sample), 1 out */
static inline float va_hb_down(va_hb_t *h, float a, float b)
{
    float s0 = b, s1 = a;
    int i;
    for (i = 0; i < VA_HB_N; i += 2) {
        float t0 = (s0 - h->y[i]) * VA_HB_COEF[i] + h->x[i];
        float t1 = (s1 - h->y[i + 1]) * VA_HB_COEF[i + 1] + h->x[i + 1];
        h->x[i] = s0;
        h->x[i + 1] = s1;
        h->y[i] = t0;
        h->y[i + 1] = t1;
        s0 = t0;
        s1 = t1;
    }
    return 0.5f * (s0 + s1);
}
