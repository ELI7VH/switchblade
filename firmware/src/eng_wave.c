/* SPDX-License-Identifier: GPL-3.0-only */
/* VOID: the VOID WAVE plugin's voice (VoidWave/Source/DSP), in Felucca's fixed point.
 *
 * Two wavetable oscillators on the plugin's built-in tables 0-8 (tools/gen_void_tables.py: the same
 * frames, keyframes crossfaded, band-limited per octave), blended by MIX; then, as the plugin's
 * Filter.cpp: a 2 % x|x| pre-saturation, its drive shaper (gain, sine, tanh), and the filter TYPE (the
 * track's P_FTYPE): LP = the plugin's LP24 tanh ladder (fb 4.2 re, makeup 1 + 1.8 re), BP / HP / LP12
 * / HP24 / NOTCH its Simper SVF (k = 2 (1 - r)^1.5); then its 0.72 soft clipper (VoiceManager.cpp).
 * The plugin's movement, per voice: pitch drift (+-1.5 cents, a damped random walk), WT drift (up to
 * +-0.055 of the position), and a micro-LFO per oscillator (0.01-0.25 Hz, 0.4-2.5 cents, 0.5-2 % level),
 * new random values every note; phase 0 at a new note (its FREE mode); a 5 ms fade-in before the filter.
 *
 * Felucca's own: the ADSR (amp: the plugin's ENV2; ENV -> FILTER, PITCH, SHAPE), the LFO, the matrix, UNISON /
 * SPREAD, glide and the FX. SHAPE moves the WT position (127 steps of it = the whole table): the plugin's LFO -> WT
 * POS. The plugin's ENV1, the filter's own envelope (EnvelopeGenerator.cpp: its EXP curves, velocity 0.5), is the
 * FLT ENV page: the track's P_FM1_ATK .. P_FM1_REL (the retired DIGITAL's operator 1, unused by every other engine
 * of a default build), on while P_FM1_LEVEL > 0 (1..127: its velocity sensitivity 0..1, the plugin's env1_vel_sens);
 * ENV DEST FLT is then its depth, the plugin's: the cutoff times
 * 1 + env * depth (63 = depth 1, an octave up at the top). P_FM1_LEVEL 0: the filter follows the amp ADSR by ENV
 * DEST FLT in cutoff steps, as every other engine.
 *
 * Signals: Q15 with 1.0 = the plugin's 1.0 (the tables hold it as VOID_WT_ONE); products are kept
 * under 2^31 by the shifts noted at them. State: per voice in the part's engine memory (void_vc_t). */
#include "void_tables.h"

/* the hot path's helpers, always inlined. Spelled __attribute: the host simulator (tests/hostsim.c) compiles
 * __attribute__ away (the sections), which would leave the render loop calling them there and its cost (regress.c,
 * void_test.c) unlike the firmware's */
#define VOID_INLINE static inline __attribute((always_inline))

static const char *const N_VOID_TBL[VOID_NTBL] = {"SINE", "SAW", "SQUARE", "SPECT", "FORMA", "FORME", "BASS1",
                                                  "BASS2", "NOISE"};
enum { VF_LP24, VF_BP, VF_HP, VF_LP12, VF_HP24, VF_NOTCH };   /* P_FTYPE (params.c N_FTYPE) */
#define VOID_SQUARE 2u
#define VOID_WT_NORM ((int32_t)((32768LL * 32768 + VOID_WT_ONE / 2) / VOID_WT_ONE))   /* table -> Q15 */
#define VOID_LIM_INC 1753014234u /* 18 kHz as a phase increment: the top harmonic of a level stays below */
#define VOID_RAMP_STEP 149       /* the 5 ms fade-in, Q15 per sample (32768 / 220.5) */
#define VOID_CLIP_THR 23593      /* the soft clipper's 0.72 */
#define VOID_OUT_LEVEL 18536     /* the plugin's voice: OSC level 0.8 * centre pan 0.707, Q15 */
/* voices a VOID part plays at most (POLY, UNISON). 6 of its heaviest preset cost about what 8 of PHASE WIRE, the
 * heaviest engine before it, do (tests/void_test.c, host instructions); raise it once the FM-1's PERF shows room */
#define VOID_POLY 6

typedef struct {                 /* per voice (engine memory) */
    int32_t f[4];                /* filter: the ladder's y0..y3; the SVFs' ic1, ic2 (HP24: and its second) */
    int32_t pd, pdv;             /* pitch drift and its velocity, cents Q20 */
    int32_t wd, wdv;             /* WT drift and its velocity, position Q20 */
    uint32_t lph[2], linc[2];    /* micro-LFOs: phase, increment per control block */
    int32_t lpit[2], llev[2];    /* their depths: cents Q20, level Q15 */
    int32_t rng;
    int32_t ramp;                /* fade-in, Q15 */
    uint32_t tick;               /* control blocks since the note: the plugin's per-block walk every 4th */
    uint32_t est, eu;            /* the filter envelope: stage (0 off, 1 attack, 2 decay / sustain, 3 release), */
    int32_t elv, epk, ers;       /* progress Q24; level, peak (velocity), the release's start, Q15 */
} void_vc_t;
#define VOID_MEM (sizeof(void_vc_t) * NVOICE)
ENG_MEM_FITS("VOID", VOID_MEM);

static void_vc_t *void_vc(const track_t *t, const voice_t *v)
{
    return (void_vc_t *)ENG_MEM((uint32_t)(t - trk) % NPART) + (uint32_t)(v - t->v) % NVOICE;
}

/* tanh(x / 32768) * 32767 up to x = 4.0 (VOID_TANH), linearly interpolated; past it, its last value */
VOID_INLINE int32_t void_tanh(int32_t x)
{
    int32_t a = x < 0 ? -x : x, i = a >> 8, y;
    y = i >= 512 ? VOID_TANH[512] : VOID_TANH[i] + (((VOID_TANH[i + 1] - VOID_TANH[i]) * (a & 255)) >> 8);
    return x < 0 ? -y : y;
}

/* the same for |x| <= 32767 (VOID_TANH1: signed, no clamp): the ladder's stages */
VOID_INLINE int32_t void_tanh1(int32_t x)
{
    uint32_t u = (uint32_t)(x + 32768), i = u >> 8;
    int32_t a = VOID_TANH1[i];
    return a + (((VOID_TANH1[i + 1u] - a) * (int32_t)(u & 255u)) >> 8);
}

/* a * b / 32768 for |a| < 2^17 (signals up to 4.0), b <= 32767 */
VOID_INLINE int32_t vmul(int32_t a, int32_t b) { return ((a >> 1) * b) >> 14; }

static inline int32_t void_rnd16(void_vc_t *c) { return (int32_t)(noise32(&c->rng) >> 16); }   /* 0..65535 */

/* ------------------------------------------------------------------ oscillators --- */
typedef struct {
    const int16_t *a, *b;        /* the two keyframes' level (b unused when w = 0) */
    uint32_t sh, mask;           /* index = phase >> sh; the level's length - 1 */
    int32_t w;                   /* crossfade a -> b, Q15 */
    uint32_t pw;                 /* SQUARE: the pulse's width as a phase (0: not the SQUARE) */
} void_osc_t;

/* table tb at position pos (frames Q8, 0 .. 255 << 8), for a phase increment inc */
static void void_osc_at(void_osc_t *o, uint32_t tb, int32_t pos, uint32_t inc)
{
    uint32_t n0l = VOID_N0LOG2[tb], hmax = (1u << n0l) >> 2, lv = 0, k = VOID_KFIRST[tb], k1 = VOID_KFIRST[tb + 1] - 1u;
    uint32_t ofs;
    while (lv < VOID_NLEV - 1u && inc > VOID_LIM_INC / (hmax >> lv))   /* the most harmonics below 18 kHz */
        lv++;
    ofs = (2u << n0l) - ((2u << n0l) >> lv);
    o->sh = 32u - (n0l - lv);
    o->mask = ((1u << n0l) >> lv) - 1u;
    o->w = 0;
    o->pw = 0;
    if (tb == VOID_SQUARE) {                           /* one saw (1 - 2 phase); the pulse is two readings */
        o->a = o->b = VOID_WAVE + VOID_KOFS[k] + ofs;  /* of it: width 0.5 - 0.45 t (the plugin's 50 -> 5 %) */
        o->pw = 2147483648u - ((uint32_t)pos >> 2) * 118427u;   /* 0.45 * 2^32 / 65280 * 4 */
        return;
    }
    while (k < k1 && ((int32_t)VOID_KPOS[k + 1] << 8) <= pos)
        k++;
    o->a = o->b = VOID_WAVE + VOID_KOFS[k] + ofs;
    if (k < k1) {
        int32_t k0p = (int32_t)VOID_KPOS[k] << 8, span = (int32_t)(VOID_KPOS[k + 1] - VOID_KPOS[k]);
        o->b = VOID_WAVE + VOID_KOFS[k + 1] + ofs;
        o->w = clamp(((pos - k0p) << 7) / span, 0, 32767);
    }
}

/* one level read at phase ph, linear between its samples */
VOID_INLINE int32_t void_rd(const int16_t *t, uint32_t ph, uint32_t sh, uint32_t mask)
{
    uint32_t i = ph >> sh;
    int32_t f = (int32_t)((ph << (32u - sh)) >> 17);  /* the fraction, Q15 */
    int32_t a = t[i], b = t[(i + 1u) & mask];
    return a + (((b - a) * f) >> 15);
}

/* the oscillator at phase ph, in table units (the SQUARE's pulse halved: it spans twice the table's peak) */
VOID_INLINE int32_t void_osc(const void_osc_t *o, uint32_t ph)
{
    int32_t s = void_rd(o->a, ph, o->sh, o->mask);
    if (o->pw)                                         /* (halved: its weight is doubled) */
        return (s - void_rd(o->a, ph - o->pw, o->sh, o->mask)) >> 1;
    if (o->w)
        s += ((void_rd(o->b, ph, o->sh, o->mask) - s) * o->w) >> 15;
    return s;
}

/* inc * 2^(cents / 1200), cents Q20 (a few cents: the drift, the micro-LFOs) */
static inline uint32_t void_detune(uint32_t inc, int32_t ct20)
{
    int32_t r = ((ct20 >> 4) * 606) >> 16;            /* inc ratio - 1, Q20 (606 / 2^20 = ln 2 / 1200) */
    return inc + (uint32_t)(((int32_t)(inc >> 12) * r) >> 8);
}

/* the filter envelope (the plugin's ENV1: EXP curves, u^2) over one control block: its level, Q15 */
static int32_t void_env1(void_vc_t *c, const int16_t *p, int gate)
{
    static const int8_t ID[3] = {P_FM1_ATK - P_FM1_ATK, P_FM1_DEC - P_FM1_ATK, P_FM1_REL - P_FM1_ATK};
    int32_t sus = p[P_FM1_SUS] * 258, u, w;
    if (c->est && c->est < 3u && !gate) {              /* the release: from where it is */
        c->est = 3;
        c->eu = 0;
        c->ers = c->elv;
    }
    if (!c->est)
        return 0;
    c->eu += ENV_LIN[clamp(p[P_FM1_ATK + ID[c->est - 1u]], 0, 127)];   /* Q24 per block (Felucca's times) */
    if (c->eu >= 1u << 24) {
        c->eu = 1u << 24;
        if (c->est == 1u) {                            /* attack done: the decay */
            c->est = 2;
            c->eu = 0;
        } else if (c->est == 3u) {
            c->est = 0;
            return c->elv = 0;
        }
    }
    u = (int32_t)(c->eu >> 9);                         /* Q15 */
    if (c->est == 1u)
        return c->elv = mulq15(mulq15(u, u), c->epk);  /* curve(n / A) * peak */
    w = mulq15(32767 - u, 32767 - u);                  /* curve(1 - n / X) */
    if (c->est == 2u)                                  /* S peak + (peak - S peak) curve */
        return c->elv = mulq15(sus, c->epk) + mulq15(c->epk - mulq15(sus, c->epk), w);
    return c->elv = mulq15(c->ers, w);                 /* start * curve */
}

/* ------------------------------------------------------------------------ filter --- */
/* Filter.cpp processSample: the pre-saturation, the drive, the TYPE, the close-gain; coefficients per block */
typedef struct {
    uint32_t ft;                 /* VF_* */
    int32_t gq, kq, fb, mk;      /* ladder: g / (1 + g) Q15, -, feedback Q12, makeup Q14; SVF: -, k Q12 */
    int32_t sat;                 /* SVF: its state saturation above RES 0.78 (r - 0.78, Q15; 0 below) */
    tsvf_t sv;
    int32_t drv, dg, dasym, doff, dk, dm, drec;   /* the drive shaper (drv 0: off) */
    int32_t close;               /* close-gain, Q15 (LP12 / LP24: fades the output as the cutoff reaches 20 Hz) */
} void_flt_t;

/* cut: the cutoff index (CUTOFF_HZ) Q8; res, drv 0..127; shut: the cutoff is at the plugin's 20 Hz (CUT 0, no
 * modulation up: the index stops at 30 Hz) */
static void void_flt_set(void_flt_t *fl, uint32_t ft, int32_t cut, int32_t res, int32_t drv, int shut)
{
    int32_t p15 = VOID_RES_P15[res], ci = cut >> 8;    /* (1 - r)^1.5 */
    fl->ft = ft;
    fl->kq = clamp(p15 >> 2, 40, 8192);                /* SVF k = 2 (1 - r)^1.5, Q12 */
    fl->sat = res * 258 > 25559 ? res * 258 - 25559 : 0;   /* applySVF: above r 0.78 */
    fl->drv = drv;
    if (ft == VF_LP24) {
        int32_t g = SVF_G[ci], re = 32767 - p15;       /* tan(pi fc / FS) Q12; re = 1 - (1 - r)^1.5 */
        if (ci < 127)
            g += ((SVF_G[ci + 1] - g) * (cut & 255)) >> 8;
        g = (int32_t)(((uint32_t)g << 15) / (uint32_t)(4096 + g));   /* g / (1 + g), Q15 */
        fl->gq = g > 32000 ? 32000 : g;
        fl->fb = (re * 17203) >> 15;                   /* 4.2 re, Q12 */
        fl->mk = 16384 + ((re * 29491) >> 15);         /* 1 + 1.8 re, Q14 */
    } else {
        tsvf_coef_k(&fl->sv, cut, fl->kq);
    }
    fl->close = 32767;
    if (ft == VF_LP24 || ft == VF_LP12) {              /* (cutoff - 20) / (60 + 220 r^2) */
        int32_t hz = CUTOFF_HZ[ci], r2 = (res * res * 220) / (127 * 127);
        if (ci < 127)
            hz += ((CUTOFF_HZ[ci + 1] - hz) * (cut & 255)) >> 8;
        fl->close = shut ? 0 : clamp(((hz - 20) << 15) / (60 + r2), 0, 32767);
    }
    if (drv) {                                         /* softClip, d = DRV / 127 */
        int32_t d = drv * 258;
        fl->dg = 4096 + ((mulq15(d, d) * 28) >> 3);    /* gain 1 + 28 d^2, Q12 */
        fl->dasym = mulq15(d, 10486);                  /* asym 0.32 d */
        fl->doff = mulq15(fl->dasym, 14746);           /* 0.45 asym */
        fl->dk = 32768 + mulq15(d, 21299);             /* sin(y pi / 2 (1 + 0.65 d)): the phase of y (Q15) */
        fl->dm = 6144 + mulq15(d, 14336);              /* tanh(. (1.5 + 3.5 d)), Q12 */
        fl->drec = (int32_t)((32768u << 15) / (uint32_t)(32768 + mulq15(d, 5898)));   /* 1 / (1 + 0.18 d) */
    }
}

/* the input stage: the pre-saturation 0.978 (x + 0.022 x|x|), then the drive (drv: fl->drv != 0, a constant where inlined) */
VOID_INLINE int32_t void_front(const void_flt_t *fl, int32_t s, const int drv)
{
    s = vmul(s + vmul((s >> 1) * ((s < 0 ? -s : s) >> 1) >> 13, 721), 32047);
    if (drv) {
        int32_t yy = ((s * (fl->dg >> 4)) >> 8) + fl->dasym;   /* x gain + asym (|s| < 2^17, gain < 2^13 Q8) */
        yy = sine_i((uint32_t)yy * (uint32_t)fl->dk);           /* (the phase wraps: sin is periodic) */
        yy = void_tanh((yy * fl->dm) >> 12);
        s = mulq15(yy - fl->doff, fl->drec);
    }
    return s;
}

/* applyLadder: four tanh one-poles, a unit-delay feedback (the stages stay within +-1: void_tanh1) */
VOID_INLINE int32_t void_ladder(const void_flt_t *fl, int32_t *f, int32_t s)
{
    int32_t u = clamp(s - ((fl->fb * f[3]) >> 12), -160000, 160000);
    int32_t x = void_tanh(((u >> 2) * 29491) >> 13);   /* tanh(0.9 u) */
    f[0] += mulq15(fl->gq, void_tanh1(x) - f[0]);
    f[1] += mulq15(fl->gq, void_tanh1(f[0]) - f[1]);
    f[2] += mulq15(fl->gq, void_tanh1(f[1]) - f[2]);
    f[3] += mulq15(fl->gq, void_tanh1(f[2]) - f[3]);
    return (f[3] * fl->mk) >> 14;
}

/* applySVF's saturation above r 0.78: x / (1 + a), a = c |x| (c = sat 0.18 or sat 0.08, Q15; a < 0.2), as
 * x (1 - a + a^2) (within 0.7 %) */
VOID_INLINE int32_t void_svf_sat(int32_t x, int32_t c)
{
    int32_t a = (c * (x < 0 ? -x : x)) >> 15;
    return ((x >> 2) * (32768 - a + ((a * a) >> 15))) >> 13;
}

/* Simper SVF on f[o], f[o + 1]: LP v2, BP v1, HP x - k v1 - v2, notch x - k v1 */
VOID_INLINE int32_t void_svf1(const void_flt_t *fl, int32_t *f, int32_t s, uint32_t ft)
{
    const tsvf_t *c = &fl->sv;
    int32_t v3 = s - f[1], v1 = (c->a1 * f[0] + c->a2 * v3) >> 13, v2 = f[1] + ((c->a2 * f[0] + c->a3 * v3) >> 13);
    f[0] = clamp(2 * v1 - f[0], -150000, 150000);
    f[1] = clamp(2 * v2 - f[1], -150000, 150000);
    if (fl->sat) {
        f[0] = void_svf_sat(f[0], (fl->sat * 5898) >> 15);
        f[1] = void_svf_sat(f[1], (fl->sat * 2621) >> 15);
    }
    if (ft == VF_LP12)
        return v2;
    if (ft == VF_BP)
        return v1;
    if (ft == VF_NOTCH)
        return s - ((fl->kq * v1) >> 12);
    return s - ((fl->kq * v1) >> 12) - v2;            /* HP, and the first of HP24 */
}
VOID_INLINE int32_t void_svf(const void_flt_t *fl, int32_t *f, int32_t s)
{
    int32_t y = void_svf1(fl, f, s, fl->ft);
    return fl->ft == VF_HP24 ? void_svf1(fl, f + 2, y, VF_HP) : y;   /* a second HP, the same coefficients */
}

/* the close-gain; nearly shut, the states drain (0.95 a sample) */
VOID_INLINE int32_t void_close(const void_flt_t *fl, int32_t *f, int32_t y)
{
    if (fl->close < 32767) {
        y = (int32_t)(((int64_t)y * fl->close) >> 15);
        if (fl->close < 1638) {
            uint32_t k;
            for (k = 0; k < 4u; k++)
                f[k] = (f[k] * 31130) >> 15;
        }
    }
    return y;
}

/* one sample s (Q15) through the filter, its state f[4] (the render loop has its own copies of this) */
static inline int32_t void_flt_run(const void_flt_t *fl, int32_t *f, int32_t s)
{
    s = void_front(fl, s, fl->drv != 0);
    return void_close(fl, f, fl->ft == VF_LP24 ? void_ladder(fl, f, s) : void_svf(fl, f, s));
}

/* ------------------------------------------------------------------------- voice --- */
static void void_note_on(track_t *t, voice_t *v)
{
    void_vc_t *c = void_vc(t, v);
    uint32_t k;
    if (!c->rng)
        c->rng = (int32_t)(0x2545F491u ^ ((uint32_t)(v - t->v) * 0x9E3779B9u) ^ ((uint32_t)(t - trk) << 24));
    v->ph[0] = v->ph[1] = 0;                           /* FREE: a new note from phase 0 (a sounding voice */
    if (!v->env) {                                     /* keeps its phases: voice.c) */
        c->f[0] = c->f[1] = c->f[2] = c->f[3] = 0;
        c->ramp = 0;
    }
    c->pd = c->pdv = c->wd = c->wdv = 0;               /* the plugin starts its drifts at the note */
    c->est = 1;                                        /* the filter envelope: an attack from 0 (the plugin's */
    c->eu = 0;                                         /* restarts at 0), its peak 1 - vs + vs velocity */
    c->elv = 0;
    {
        int32_t vs = clamp(t->p[P_FM1_LEVEL] - 1, 0, 126) * 260;   /* (LEVEL 1..127: vs 0..1) */
        c->epk = 32767 - vs + ((vs * (int32_t)v->vel) >> 7);
    }
    c->tick = 0;
    for (k = 0; k < 2u; k++) {                         /* UnisonVoice.cpp, per note: */
        uint32_t u1 = (uint32_t)void_rnd16(c), u2 = (uint32_t)void_rnd16(c);
        int32_t u3 = void_rnd16(c), u4 = void_rnd16(c);
        c->linc[k] = 31165u + ((u1 >> 4) * 747950u >> 12);   /* 0.01 + U 0.24 Hz (Hz * CTL / FS * 2^32) */
        c->lph[k] = u2 << 16;
        c->lpit[k] = 419430 + ((u3 * 8601) >> 8);      /* 0.4 + U 2.1 cents, Q20 */
        c->llev[k] = 164 + ((u4 * 492) >> 16);         /* 0.005 + U 0.015, Q15 */
    }
}

/* the plugin's per-block random walks (VoiceManager.cpp), every 4th control block (its 128 samples) */
static void void_walk(void_vc_t *c)
{
    c->pdv += ((void_rnd16(c) - 32768) * 1258) >> 16;  /* (U - 0.5) 0.0012 cents */
    c->pdv = mulq15(c->pdv, 32539);                    /* * 0.993 */
    c->pd = clamp(c->pd + c->pdv, -1572864, 1572864);  /* +-1.5 cents */
    c->wdv += ((void_rnd16(c) - 32768) * 524) >> 16;   /* (U - 0.5) 0.0005 */
    c->wdv = mulq15(c->wdv, 32637);                    /* * 0.996 */
    c->wd = clamp(c->wd + c->wdv, -57672, 57672);      /* +-0.055 */
}

/* a block of one voice, the inner loop specialised by its filter (lad: the ladder, else the SVFs) and drive */
typedef struct {
    void_osc_t o1, o2;
    void_flt_t fl;
    int32_t c1, c2;              /* each oscillator's weight: blend, micro-LFO level, table -> Q15; Q14 */
    uint32_t inc1, inc2;
    int two;                     /* OSC2 sounds (MIX > 0) */
} void_blk_t;

/* the loop of a block, specialised at each call by its filter (lad: the ladder, else the SVFs) and drive */
VOID_INLINE void void_block(const void_blk_t *b, voice_t *v, void_vc_t *c, int32_t *out, uint32_t n, const vmod_t *m,
                            const int lad, const int drv)
{
    /* everything the loop reads, local: out[] is written through a pointer, so the compiler would read each of
     * these again after every sample */
    const void_osc_t o1 = b->o1, o2 = b->o2;
    const void_flt_t fl = b->fl;
    const int32_t c1 = b->c1, c2 = b->c2, two = b->two;
    const uint32_t inc1 = b->inc1, inc2 = b->inc2;
    uint32_t i, ph1 = v->ph[0], ph2 = v->ph[1];
    int32_t ramp = c->ramp, f[4] = {c->f[0], c->f[1], c->f[2], c->f[3]};
    int32_t amp = m->amp0 << CTL_LOG2, damp = m->amp1 - m->amp0;   /* voice_amp's ramp, stepped */
    for (i = 0; i < n; i++) {
        int32_t s = void_osc(&o1, ph1) * c1, y, a;
        if (two)
            s += void_osc(&o2, ph2) * c2;
        s >>= 14;                                      /* Q15, 1.0 = the plugin's */
        ph1 += inc1;
        ph2 += inc2;
        if (ramp < 32767) {
            ramp += VOID_RAMP_STEP;
            s = mulq15(s, ramp > 32767 ? 32767 : ramp);
        }
        s = void_front(&fl, s, drv);
        y = void_close(&fl, f, lad ? void_ladder(&fl, f, s) : void_svf(&fl, f, s));
        /* the voice at the plugin's level, then its soft clipper: above 0.72, thr + o / (1 + o); the plugin's level
         * is ~6 dB below Felucca's other engines, made up after the clipper (which stays at the plugin's level) */
        y = mulq15(clamp(y, -110000, 110000), VOID_OUT_LEVEL);
        a = y < 0 ? -y : y;
        if (a > VOID_CLIP_THR) {                       /* o / (1 + o) from VOID_SOFT (o < 2: the clamp above) */
            uint32_t o = (uint32_t)(a - VOID_CLIP_THR), k = o >> 8;
            a = VOID_CLIP_THR + VOID_SOFT[k] + (((VOID_SOFT[k + 1u] - VOID_SOFT[k]) * (int32_t)(o & 255u)) >> 8);
            y = y < 0 ? -a : a;
        }
        out[i] += mulq15(mulq15(y, amp >> CTL_LOG2), VOICE_FS) << 2;   /* voice_amp; +6 dB after the clipper: */
        amp += damp;                                   /* Felucca's engines' level */
    }
    v->ph[0] = ph1;
    v->ph[1] = ph2;
    c->ramp = ramp;
    c->f[0] = f[0];
    c->f[1] = f[1];
    c->f[2] = f[2];
    c->f[3] = f[3];
}

static void void_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    void_vc_t *c = void_vc(t, v);
    void_blk_t b;
    int32_t mix = clamp(p[P_E3], 0, 127) * 2, cutk = clamp(p[P_E4], 0, 127), pos, l1, l2, g1, g2, mc;
    if (n > CTL)
        n = CTL;
    if ((c->tick++ & 3u) == 0u)
        void_walk(c);
    c->lph[0] += c->linc[0];
    c->lph[1] += c->linc[1];
    l1 = sine_i(c->lph[0]);
    l2 = sine_i(c->lph[1]);
    g1 = 32767 + mulq15(l1, c->llev[0]);               /* micro-LFO level, Q15 around 1 */
    g2 = 32767 + mulq15(l2, c->llev[1]);
    /* pitch: the drift (both) and each micro-LFO, in cents; OSC2 against OSC1 by its step, or (MORPH) OSC1's pitch
     * and phase with its micro-LFO: one oscillator reading two tables, as the plugin's Morph mix */
    b.inc1 = void_detune(m->inc, c->pd + ((l1 * (c->lpit[0] >> 8)) >> 7));
    if (p[P_E6] >= VOID_OSC2_MORPH) {
        b.inc2 = b.inc1;
        v->ph[1] = v->ph[0];
        g2 = g1;
    } else {
        b.inc2 = void_detune(cents_inc(m->pitch16, VOID_OSC2_CT[clamp(p[P_E6], 0, VOID_OSC2_N - 1)], m->fine),
                             c->pd + ((l2 * (c->lpit[1] >> 8)) >> 7));
    }
    /* position: the knob, SHAPE (127 steps = the table), the drift */
    pos = p[P_E1] * 255 * 256 / 127 + (m->shape - (64 << 8)) * 255 / 127 + ((c->wd * 255) >> 12);
    pos = clamp(pos, 0, 255 << 8);
    void_osc_at(&b.o1, (uint32_t)clamp(p[P_E0], 0, VOID_NTBL - 1), pos, b.inc1);
    void_osc_at(&b.o2, (uint32_t)clamp(p[P_E2], 0, VOID_NTBL - 1), pos, b.inc2);
    /* the weights: (1 - mix) and mix (MIX 0..127 -> 0..254 / 256), the micro-LFO levels, table -> Q15 (Q14). The
     * SQUARE's pulse is twice the table's peak: its reading is halved (void_osc) and its weight doubled */
    b.two = mix != 0;
    b.c1 = (int32_t)((((int64_t)g1 * (256 - mix)) * (VOID_WT_NORM >> 1)) >> 23);
    b.c2 = (int32_t)((((int64_t)g2 * mix) * (VOID_WT_NORM >> 1)) >> 23);
    if (b.o1.pw)
        b.c1 <<= 1;
    if (b.o2.pw)
        b.c2 <<= 1;
    /* the filter: CUT on the plugin's knob law, plus ENV / LFO / matrix (m->cutoff: cutoff steps << 8). With FLT ENV
     * on, ENV DEST FLT's share of m->cutoff (the amp ADSR's) is the filter envelope's instead, the plugin's way */
    mc = m->cutoff;
    if (p[P_FM1_LEVEL]) {
        int32_t e1 = void_env1(c, p, v->gate), x;
        mc -= (m->envq15 * p[P_ED_FLT]) >> 7;
        x = 2 * mulq15(e1, p[P_ED_FLT] * 520) + (256 << 8);   /* env * depth (63 = 1) as VOID_LOG2P1's index, Q8 */
        x = clamp(x, 0, 512 << 8);
        {
            int32_t k = x >> 8, a = VOID_LOG2P1[k];
            mc += k < 512 ? a + (((VOID_LOG2P1[k + 1] - a) * (x & 255)) >> 8) : a;
        }
    }
    void_flt_set(&b.fl, (uint32_t)p[P_FTYPE] % 6u, clamp((int32_t)VOID_CUT_Q8[cutk] + mc, 0, 127 << 8),
                 clamp(p[P_E5], 0, 127), clamp(p[P_E7], 0, 127), !cutk && mc <= 0);
    if (b.fl.ft == VF_LP24) {
        if (b.fl.drv)
            void_block(&b, v, c, out, n, m, 1, 1);
        else
            void_block(&b, v, c, out, n, m, 1, 0);
    } else if (b.fl.drv) {
        void_block(&b, v, c, out, n, m, 0, 1);
    } else {
        void_block(&b, v, c, out, n, m, 0, 0);
    }
}

/* ----------------------------------------------------------------------- presets --- */
/* the factory sounds: presets/four/wave.py (tools/four_presets.py: WAVE_PRESETS in four_presets.h, category.c).
 * WAVE_PX holds what a preset_t cannot (core.h void_px_t), applied by void_track_loaded */
_Static_assert(NELEM(WAVE_PX) == NELEM(WAVE_PRESETS), "WAVE_PX: one per preset");

/* a preset just loaded on track t (ui.c apply_preset_to, after the defaults and the preset's own values) */
static void void_track_loaded(track_t *t)
{
    const void_px_t *x;
    uint32_t k;
    if (t->eng_req % NENGINES != ENGI_WAVE || t->preset >= NELEM(WAVE_PX))
        return;
    x = &WAVE_PX[t->preset];
    t->p[P_FTYPE] = x->ftype;
    if (x->voice == V_UNISON)
        t->p[P_VOICE] = V_UNISON;
    if (x->detune)
        t->p[P_DETUNE] = (int16_t)(x->detune - 1u);
    t->p[P_SPRD] = x->spread;
    t->p[P_GLIDE] = x->glide;
    if (x->lfo[0]) {
        t->p[P_LRATE] = x->lfo[1];
        t->p[P_LWAVE] = x->lfo[2];
        t->p[P_LTRIG] = x->lfo[3];
        t->p[P_LFADE] = x->lfo[4];
    }
    t->p[P_FM1_LEVEL] = x->fe[0];
    t->p[P_FM1_ATK] = x->fe[1];
    t->p[P_FM1_DEC] = x->fe[2];
    t->p[P_FM1_SUS] = x->fe[3];
    t->p[P_FM1_REL] = x->fe[4];
    for (k = 0; k < 4u; k++) {                         /* (mod.c's NMSLOT slots) */
        t->p[P_M1SRC + 3u * k] = x->mx[k][0];
        t->p[P_M1DST + 3u * k] = x->mx[k][1];
        t->p[P_M1AMT + 3u * k] = x->mx[k][2];
    }
}

static const engine_t ENG_WAVE = {
    .name = "WAVE",
    .page_title = {"OSC", "FILTER"},
    .edit = {
        {"TBL1", F_ENUM, 0, VOID_NTBL - 1, 1, N_VOID_TBL, 0},
        {"POS", F_PCT, 0, 127, 0, 0, 0},
        {"TBL2", F_ENUM, 0, VOID_NTBL - 1, 1, N_VOID_TBL, 0},
        {"MIX", F_PCT, 0, 127, 64, 0, 0},
        {"CUT", F_INT, 0, 127, 100, 0, 0},
        {"RES", F_PCT, 0, 127, 0, 0, 0},
        {"OSC2", F_ENUM, 0, VOID_OSC2_N - 1, VOID_OSC2_ZERO, N_VOID_OSC2, 0},
        {"DRV", F_PCT, 0, 127, 0, 0, 0},
    },
    .presets = WAVE_PRESETS,
    .npresets = NELEM(WAVE_PRESETS),
    .knob = {P_E4, P_E5, P_ED_FLT, P_E1},
    .poly = VOID_POLY,
    .note_on = void_note_on,
    .render = void_render,
    .mem = VOID_MEM,
};
