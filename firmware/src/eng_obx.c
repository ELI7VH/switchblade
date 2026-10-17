/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* OB-Z (was ANALOG): two band-limited oscillators (saw / square / tri / sine / PWM) through a
 * trapezoidal state-variable filter with an SEM-style response morph, a 4-pole option and slop.
 *
 *   E0 WAVE   E1 DTN   E2 MIX   E3 PW   E4 CUT   E5 RES   E6 MODE   E7 SLOP
 *
 *   MODE  the filter's response, morphed over 0..120 and back: LP at 0, NOTCH at 32, HP at 64, BP
 *         at 96, and over 96..120 the band-pass crossfades back to the low-pass, so MODE 120 is
 *         the low-pass corner again (sample for sample what MODE 0 plays) and the morph has no dead
 *         step. 121..127 is the 4-pole option, which the closing leg ramps into: a low-pass through
 *         a second SVF at the same cutoff, 24 dB an octave. MODE 0 is the low-pass OB-Z always had,
 *         step for step.
 *   SLOP  the voice's own analogue error, seeded from the note-on counter: +-SLOP x 6 cents of
 *         pitch and +-SLOP x 2 semitones of cutoff, the same for a voice's whole note and another
 *         draw on the next note-on (a retrigger never keeps it).
 *   PW    the pulse width of WAVE PWM (64 = square); the SHP routings add to it as before.
 *
 * WAVE SYNC and SUB (#104, appended: WAVE 0..4 play as they always did) are two more oscillator pairs (obx_render_x):
 *   SYNC  OSC 2 a saw hard-synced to OSC 1 (a saw): it restarts with OSC 1's every cycle, band-limited at the restart
 *         (polyBLEP scaled by the step the restart makes). DTN (shown SYNC) sets OSC 2 above OSC 1, 3/16 semitone a
 *         step (127: +23.8 semitones), and the SHP routings sweep it (ENV DEST / LFO DEST SHP, the matrix's SHP: 3/16
 *         semitone a step of shape, as PWM takes them for its width): the sync sweep. MIX between the two as for
 *         every pair (OSC 2 alone at 127)
 *   SUB   OSC 1 a saw, OSC 2 a square an octave below it (detuned by DTN in cents as before): MIX (shown SUB) is the
 *         sub oscillator's level
 *
 * (The copy of the ANALOG render that the ACID and DRIFT placeholders borrowed is gone: both machines have their own voices.) */
static const char *const N_ANALOG_WAVE[] = {"SAW", "SQR", "TRI", "SIN", "PWM", "SYNC", "SUB"};
enum { AW_SYNC = 5, AW_SUB = 6 };
static const param_desc_t ANALOG_SYNC = {"SYNC", F_INT, 0, 127, 10, 0, 0};   /* DTN's range and default */
static const param_desc_t ANALOG_SUB = {"SUB", F_PCT, 0, 127, 64, 0, 0};      /* MIX's */

#define ANALOG_K(res) (8192 - (res) * 7600 / 127)     /* tsvf_coef's damping, Q12 (the morph's and the 4-pole's taps) */

/* ------------------------------------------------------- OB-Z: the MODE morph --- */
/* The SVF hands out all three responses at once: the low-pass v2, the band-pass v1 and the
 * high-pass in - k v1 - v2; notch is low-pass + high-pass. So the morph is a crossfade of three
 * taps, with the weights (Q12) of the low-pass, high-pass and band-pass carrying the notch between
 * them. Every corner is exact, and a MODE sitting on one is that single tap, taken as 1.5's TYPE
 * always took it and at the same cost (obx_tap's kind): only the positions between two corners pay
 * for the crossfade, and MODE 0 returns v2 untouched. */
enum { OBX_M_NOTCH = 32, OBX_M_HP = 64, OBX_M_BP = 96, OBX_M_4POLE = 121 };
enum { OBX_T_LP, OBX_T_NOTCH, OBX_T_HP, OBX_T_BP, OBX_T_MIX };
typedef struct { int32_t wl, wh, wb; uint32_t kind, p4; } obx_flt_t;

/* The signal handed to the 4-pole's second tsvf_lp stage is held to +-OBX_P4_IN.
 *
 * Why it needs a bound: stage 1's output is not the oscillator mix. With |in| <= 32768 and RES 127
 * the corner sweep in obx_tap's comment puts |v2| at up to 184627, past dsp.c's contract for
 * tsvf_lp ("signals stay within +-150000 so products fit in 32 bits"). Fed that, stage 2's inner
 * products overflow int32: with ic1 held to +-150000 by tsvf_lp's own state clamp, |in| = 184627
 * and |ic2| = 150000 (v3 = in - ic2, up to 334627) the worst coefficient pair gives
 * |a2 ic1 + a3 v3| = 2.59e9, 120 % of INT32_MAX. (Renders stay far inside that, 63..81 % of it,
 * but nothing proves they must.)
 *
 * The derivation of the bound: v3 = in - ic2 gives |v3| <= OBX_P4_IN + 150000. a1, a2, a3 peak at
 * 8190, 3820 and 6406 over every (CUT, RES) the coefficient table makes, and the pair that
 * matters peaks together (CUT 32510, RES 127), so |a1 ic1 + a2 v3| and |a2 ic1 + a3 v3| are at
 * most
 *     a2 * 150000 + a3 * (OBX_P4_IN + 150000)      -- worst case over the table, swept in the test
 * = 2.044e9 at OBX_P4_IN = 100000 (95.2 % of 2^31 - 1), against 2.364e9 (110 %) at the dsp.c
 * contract's 150000: so the contract on its own is not enough for this stage, and the bound is
 * tighter. The largest bound that provably fits is about 116200 (118000 already reaches 100.5 %);
 * 100000 leaves 4.8 % of margin. A render's stage 1 reaches 86316 at most (tests/obx_test.c
 * test_p4_second_stage: RES 127, every CUT, MIDI 24..99, SLOP 0 and 127), so the clamp never
 * touches one and no hash moves; it only closes the proof.
 *
 * Two seams for that test, both observe-only in the sense that the clamp itself always runs,
 * unmodified, between them: OBX_P4_PRE(y) sees the value before the clamp (the test uses it to
 * drive stage 1 past the bound, so that the clamp has something to do), and OBX_P4_SEEN(x, y, c,
 * i1, i2) sees what the clamp produced (x), the value it was given (y), the stage's coefficients
 * and its state, and returns x. Both default to nothing. */
#ifndef OBX_P4_IN
#define OBX_P4_IN 100000
#endif
#ifndef OBX_P4_PRE
#define OBX_P4_PRE(y) (y)
#endif
#ifndef OBX_P4_SEEN
#define OBX_P4_SEEN(x, y, c, i1, i2) (x)
#endif
#define OBX_P4_FEED(y, c, i1, i2) \
    OBX_P4_SEEN(clamp(OBX_P4_PRE(y), -OBX_P4_IN, OBX_P4_IN), (y), (c), (i1), (i2))

/* the morph's position. OB-Z keeps 1.5's filter TYPE (P_FTYPE, EDIT > FILTER): TYPE LP (the
 * default, and LP12 / NOTCH, which ANALOG always played as LP) leaves the response to MODE, while
 * TYPE BP / HP (and HP24, played as HP) still pin it where they always put it. */
static inline int32_t obx_mode(const int16_t *p)
{
    static const uint8_t FT_MODE[6] = {0, OBX_M_BP, OBX_M_HP, 0, OBX_M_HP, 0};
    uint32_t f = (uint32_t)p[P_FTYPE];
    if (f < 6u && FT_MODE[f])
        return FT_MODE[f];
    return clamp(p[P_E6], 0, 127);
}

static void obx_mode_weights(obx_flt_t *w, int32_t mode)
{
    int32_t f = (mode & 31) << 7;                     /* 0..3968 of 4096 between two corners */
    w->p4 = (uint32_t)(mode >= OBX_M_4POLE);
    w->wl = 0;
    w->wh = 0;
    w->wb = 0;
    if (w->p4 || mode < OBX_M_NOTCH) {                /* LP -> NOTCH (the 4-pole starts from LP) */
        w->wl = 4096;
        w->wh = w->p4 ? 0 : f;
    } else if (mode < OBX_M_HP) {                     /* NOTCH -> HP */
        w->wl = 4096 - f;
        w->wh = 4096;
    } else if (mode < OBX_M_BP) {                     /* HP -> BP */
        w->wh = 4096 - f;
        w->wb = f;
    } else {                                          /* BP -> LP: the morph closes, 96 to 120 */
        f = (mode - OBX_M_BP) * 4096 / (OBX_M_4POLE - 1 - OBX_M_BP);   /* 24 steps, 4096 at 120 */
        w->wb = 4096 - f;
        w->wl = f;
    }
    w->kind = !w->wh && !w->wb                 ? (uint32_t)OBX_T_LP
              : w->wl == 4096 && w->wh == 4096 ? (uint32_t)OBX_T_NOTCH
              : !w->wl && !w->wb               ? (uint32_t)OBX_T_HP
              : !w->wl && !w->wh               ? (uint32_t)OBX_T_BP
                                               : (uint32_t)OBX_T_MIX;
}

static inline int32_t obx_tap(const tsvf_t *c, int32_t k, int32_t in, int32_t *ic1, int32_t *ic2,
                              const obx_flt_t *w)
{
    int32_t v3 = in - *ic2;
    int32_t v1 = (c->a1 * *ic1 + c->a2 * v3) >> 13;
    int32_t v2 = *ic2 + ((c->a2 * *ic1 + c->a3 * v3) >> 13);
    int32_t hp;
    *ic1 = clamp(2 * v1 - *ic1, -150000, 150000);
    *ic2 = clamp(2 * v2 - *ic2, -150000, 150000);
    switch (w->kind) {
    case OBX_T_LP:                                    /* MODE 0, and the 4-pole's first stage */
        return v2;                                    /* LP: tsvf_lp's (the same steps) */
    case OBX_T_NOTCH:
        return in - ((k * v1) >> 12);                 /* lp + hp */
    case OBX_T_HP:
        return in - ((k * v1) >> 12) - v2;
    case OBX_T_BP:
        return v1;
    default:
        break;
    }
    /* The crossfade. Nothing here is clamped, and the three taps are the same expressions the
     * pinned branches above return, so a MODE sitting a step short of a corner meets the corner
     * itself without anything but the morph's own 1/32 between them. The widest product fits:
     * ic1 / ic2 are held to +-150000 by the two lines above, |in| <= 32768 (the oscillator mix,
     * s >> 1), and a sweep of every CUT and every RES (k 592..8192) over the eight corners of that
     * box -- hp and v1 and v2 are each linear in (ic1, ic2, in), so a corner is where they peak --
     * gives |v1| <= 183328, |v2| <= 184627 and |hp| <= 481203. Against 2^31 - 1 = 2.147e9 the
     * products x 4096 are then 7.51e8, 7.56e8 and 1.97e9: of the crossfade the hp term has the
     * least room, and 8 % of it. It is not the least in the engine: the 4-pole's second stage has
     * 4.8 % (95.2 % used), see OBX_P4_IN. Renders reach nothing like that -- a sweep of RES 127
     * over every WAVE, CUT and the keyboard (tests/obx_test.c test_mode_bounded) peaks at
     * |hp| ~ 48000, a factor of ten below the bound -- and a +-400000 clamp on hp used to stand
     * here for the overflow the bound rules out. It only put a step across MODE 64, where the
     * pinned high-pass has no clamp. */
    hp = in - ((k * v1) >> 12) - v2;
    return ((v2 * w->wl) >> 12) + ((hp * w->wh) >> 12) + ((v1 * w->wb) >> 12);
}

/* SLOP: the voice's own tuning and cutoff error, drawn from the note-on counter its note_on left
 * in s[2]. The pitch comes back in 1/65536 of the phase increment (+-227 = +-6 cents) and the
 * cutoff in the cutoff's own Q8 steps (+-598 = +-2 semitones: one step is 0.86 of a semitone). */
static inline uint32_t obx_rnd(int32_t seed, uint32_t which)
{
    uint32_t h = (uint32_t)seed + which * 0x9E3779B9u;
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12;
    h *= 0x297A2D39u;
    return h ^ (h >> 15);
}
static void obx_slop(int32_t slop, int32_t seed, int32_t *pitch, int32_t *cut)
{
    if (!slop) {
        *pitch = 0;
        *cut = 0;
        return;
    }
    *pitch = ((int32_t)(obx_rnd(seed, 0) >> 17) - 16384) * slop / 9165;
    *cut = ((int32_t)(obx_rnd(seed, 1) >> 17) - 16384) * slop / 3480;
}
static inline uint32_t obx_detune(uint32_t inc, int32_t slop_pitch)
{
    return slop_pitch ? inc + (uint32_t)((int32_t)(inc >> 16) * slop_pitch) : inc;
}

static const param_desc_t *analog_desc(const track_t *t, uint32_t k)
{
    if (k == 1u && t->p[P_E0] == AW_SYNC)
        return &ANALOG_SYNC;
    if (k == 2u && t->p[P_E0] == AW_SUB)
        return &ANALOG_SUB;
    return 0;
}

static void obx_note_on(track_t *t, voice_t *v)
{
    (void)t;
    v->ph[1] = v->ph[0] + 0x40000000u;
    v->s[0] = v->s[1] = 0;                            /* filter */
    v->s[4] = v->s[5] = 0;                            /* the 4-pole's second stage */
    v->s[2] = (int32_t)(0x1234567u + (uint32_t)v->age * 2654435761u);   /* SLOP: the note-on counter */
    v->s[3] = 0;                                      /* SYNC: no restart pending */
}

/* WAVE SYNC / SUB: OSC 1 a saw, OSC 2 the synced saw / the sub square; then SLOP and the morph as
 * obx_osc has them. Its own function: obx_osc's loop stays what it was */
static __attribute__((noinline)) void obx_render_x(track_t *t, voice_t *v, int32_t *out, uint32_t n,
                                                   const vmod_t *m)
{
    const int16_t *p = t->p;
    uint32_t sub = p[P_E0] == AW_SUB, i;
    int32_t det = p[P_E1], mix = p[P_E2], sl_p, sl_c;
    int32_t cut;
    tsvf_t flt;
    obx_flt_t w;
    int32_t m2 = mix * 258, m1 = 32767 - m2;
    uint32_t ph0 = v->ph[0], ph1 = v->ph[1], inc1, inc2;
    int32_t ic1 = v->s[0], ic2 = v->s[1], ic3 = v->s[4], ic4 = v->s[5];
    int32_t jl = v->s[3];                             /* SYNC: 1 + the step of a restart a sample ago (16 bits), 0 none */
    obx_slop(p[P_E7], v->s[2], &sl_p, &sl_c);
    cut = (p[P_E4] << 8) + m->cutoff + sl_c;
    inc1 = obx_detune(m->inc, sl_p);
    if (sub) {                                        /* an octave below OSC 1, DTN as OSC 2 always had it */
        inc2 = obx_detune(det ? cents_inc(m->pitch16, det, 0) : m->inc, sl_p) >> 1;
    } else {                                          /* DTN and the shape above OSC 1, 3/16 semitone a step */
        inc2 = pitch_inc(clamp(m->pitch16 + det * 3 + (((m->shape - (64 << 8)) * 3) >> 8), 0, 2047));
        if (m->fine)
            inc2 += (uint32_t)((int32_t)(inc2 >> 12) * m->fine);
        inc2 = obx_detune(inc2, sl_p);
    }
    obx_mode_weights(&w, obx_mode(p));
    int32_t fk = ANALOG_K(p[P_E5]);
    tsvf_coef(&flt, cut, p[P_E5]);
    for (i = 0; i < n; i++) {
        int32_t a = osc_saw(ph0, inc1), b, s, y;
        if (sub) {
            b = osc_pulse(ph1, inc2, 0x80000000u);
        } else {
            int32_t bl = blep(ph1, inc2);
            if (jl) {                                 /* just restarted: the step was jl - 1, not a whole cycle */
                bl = (bl * ((jl - 1) >> 1)) >> 15;
                jl = 0;
            }
            b = (int32_t)(ph1 >> 16) - 32768 - bl;
            if (ph0 > 0xFFFFFFFFu - inc1)             /* OSC 1 restarts it before the next sample: round that step */
                b -= (blep(ph0, inc1) * (int32_t)(ph1 >> 17)) >> 15;
        }
        ph0 += inc1;
        ph1 += inc2;
        if (!sub && ph0 < inc1) {                     /* OSC 1 wrapped: OSC 2 restarts at the same point */
            uint32_t f = ph0 / ((inc1 >> 16) | 1u);   /* the part of a sample since, 0..65535 */
            uint32_t at = ph1 - f * (inc2 >> 16);     /* OSC 2 at the wrap */
            jl = (int32_t)(at >> 16) + 1;
            ph1 = f * (inc2 >> 16);
        }
        s = mulq15(a, m1) + mulq15(b, m2);
        y = obx_tap(&flt, fk, s >> 1, &ic1, &ic2, &w);
        if (w.p4)
            y = tsvf_lp(&flt, OBX_P4_FEED(y, &flt, ic3, ic4), &ic3, &ic4);
        s = soft_knee(y, 16000) << 1;
        out[i] += voice_amp(s, m, i) << 1;
    }
    v->ph[0] = ph0;
    v->ph[1] = ph1;
    v->s[0] = ic1;
    v->s[1] = ic2;
    v->s[3] = jl;
    v->s[4] = ic3;
    v->s[5] = ic4;
}

/* WAVE 0..4 through the MODE morph. morph is a constant at each call site: with it 0 the compiler
 * keeps the plain low-pass loop (and its cost) OB-Z always had, and obx_render_f gets its own copy
 * for every other MODE */
static inline __attribute__((always_inline)) void obx_osc(track_t *t, voice_t *v, int32_t *out, uint32_t n,
                                                          const vmod_t *m, uint32_t morph)
{
    const int16_t *p = t->p;
    uint32_t wave = (uint32_t)p[P_E0], i;
    int32_t det = p[P_E1], mix = p[P_E2], sl_p, sl_c;
    int32_t shp = m->shape;                                    /* PW offsets it; the SHP routings are already in */
    int32_t cut;
    tsvf_t flt;
    obx_flt_t w = {4096, 0, 0, OBX_T_LP, 0};                  /* morph 0: the plain low-pass */
    uint32_t inc1, inc2;
    uint32_t pw;
    int32_t m2 = mix * 258, m1 = 32767 - m2;                    /* osc mix Q15 */
    uint32_t ph0 = v->ph[0], ph1 = v->ph[1];                  /* state in locals: out[] may alias v->s[] */
    int32_t ic1 = v->s[0], ic2 = v->s[1], ic3 = v->s[4], ic4 = v->s[5];
    int32_t fk = morph ? ANALOG_K(p[P_E5]) : 0;
    obx_slop(p[P_E7], v->s[2], &sl_p, &sl_c);
    cut = (p[P_E4] << 8) + m->cutoff + sl_c;
    inc1 = obx_detune(m->inc, sl_p);
    inc2 = det ? obx_detune(cents_inc(m->pitch16, det, 0), sl_p) : inc1;   /* DTN in cents */
    if (p[P_E3] != 64)                                         /* PW: 64 is the square the shape alone gave */
        shp = clamp(shp + ((p[P_E3] - 64) << 8), -62 * 256, 190 * 256);
    pw = 0x80000000u + (uint32_t)((shp - (64 << 8)) << 15);
    if (morph)
        obx_mode_weights(&w, obx_mode(p));
    tsvf_coef(&flt, cut, p[P_E5]);
    for (i = 0; i < n; i++) {
        int32_t a, b, s, y;
        switch (wave) {
        case 1:
            a = osc_pulse(ph0, inc1, 0x80000000u);
            b = osc_pulse(ph1, inc2, 0x80000000u);
            break;
        case 2:
            a = osc_tri(ph0);
            b = osc_tri(ph1);
            break;
        case 3:
            a = osc_sine(ph0);
            b = osc_sine(ph1);
            break;
        case 4:
            a = osc_pulse(ph0, inc1, pw);
            b = osc_pulse(ph1, inc2, pw);
            break;
        default:
            a = osc_saw(ph0, inc1);
            b = osc_saw(ph1, inc2);
            break;
        }
        ph0 += inc1;
        ph1 += inc2;
        s = mulq15(a, m1) + mulq15(b, m2);
        {   /* filter, linear up to half scale, then a soft knee (only resonance peaks saturate); soft_knee()
             * written out: the host compiler makes this loop 1 % slower with the call */
            int32_t q;
            if (morph == 2u) {                               /* the 4-pole: two low-pass stages */
                y = tsvf_lp(&flt, s >> 1, &ic1, &ic2);
                y = tsvf_lp(&flt, OBX_P4_FEED(y, &flt, ic3, ic4), &ic3, &ic4);
            } else if (morph) {
                y = obx_tap(&flt, fk, s >> 1, &ic1, &ic2, &w);
            } else {
                y = tsvf_lp(&flt, s >> 1, &ic1, &ic2);
            }
            q = y < 0 ? -y : y;
            if (q > 16000) {
                q = 16000 + (softclip((q - 16000) * 2) >> 1);
                y = y < 0 ? -q : q;
            }
            s = y << 1;
        }
        out[i] += voice_amp(s, m, i) << 1;
    }
    v->ph[0] = ph0;
    v->ph[1] = ph1;
    v->s[0] = ic1;
    v->s[1] = ic2;
    if (morph == 2u) {
        v->s[4] = ic3;
        v->s[5] = ic4;
    }
}
static __attribute__((noinline)) void obx_render_f(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    obx_osc(t, v, out, n, m, 1);
}
static __attribute__((noinline)) void obx_render_p4(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    obx_osc(t, v, out, n, m, 2);
}

static void obx_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    int32_t mode;
    if (t->p[P_E0] >= AW_SYNC)
        obx_render_x(t, v, out, n, m);
    else if (!(mode = obx_mode(t->p)))
        obx_osc(t, v, out, n, m, 0);
    else if (mode >= OBX_M_4POLE)
        obx_render_p4(t, v, out, n, m);
    else
        obx_render_f(t, v, out, n, m);
}

/* OBX_PRESETS: presets/four/obx.py (tools/four_presets.py, four_presets.h) */
/* edit[] is the contract Task 4.1's brief pins and later tasks read, so the slots keep their order.
 * icons.c param_icon matches MODE by its address, `d == &ENG_OBX.edit[6]` (the label "MODE" is the
 * arp's, and ui_test wants each label once): reordering edit[] silently re-points that icon, so the
 * index there has to move with the slot. */
static const engine_t ENG_OBX = {
    .name = "OB-Z",
    .page_title = {"OSC", "FLT"},
    .edit = {
        {"WAVE", F_ENUM, 0, 6, 0, N_ANALOG_WAVE, 0},
        {"DTN", F_INT, 0, 127, 10, 0, "ct"},
        {"MIX", F_PCT, 0, 127, 64, 0, 0},
        {"PW", F_PCT, 0, 127, 64, 0, 0},
        {"CUT", F_CUTOFF, 0, 127, 90, 0, 0},
        {"RES", F_PCT, 0, 127, 30, 0, 0},
        {"MODE", F_PCT, 0, 127, 0, 0, 0},
        {"SLOP", F_PCT, 0, 127, 0, 0, 0},
    },
    .presets = OBX_PRESETS,
    .npresets = NELEM(OBX_PRESETS),
    .note_on = obx_note_on,
    .render = obx_render,
    .desc = analog_desc,
    .knob = {P_E4, P_E5, P_ED_FLT, P_E1},
    .keep = 0x33,                /* the filter, both stages */
};
