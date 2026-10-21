/* SPDX-License-Identifier: GPL-3.0-only */
/* NANO's SUB model (2026-10-21): the subtractive voice of fm1_subsynth.h and its 30 factory sounds of fm1_subsynth_presets.h (written for this
 * project, DIST CHORUS SUB/; docs/plans/2026-10-21-dist-chorus-sub.md). The NANO machine (eng_acid.c) plays it on a track whose P_RSV_LN + 1 is 1
 * (its presets 3..32, NANO_PX), the classic 303 otherwise. Polyphonic (SUB_POLY voices), integer.
 *
 * The module's voice, so its sounds play as they were made: osc 1 and osc 2 PolyBLEP saw or pulse (each its own wave and level; osc 2 +-24
 * semitones and +-50 cents; the pulse less its mean, so a narrow one carries no DC), the sub an octave below osc 1, white noise; the Chamberlin
 * state-variable filter twice a sample with the soft-saturated band path (LP12 / LP24 / BP / HP); its two exponential ADSRs (the attack aims past
 * the top, a retrigger starts from the level there is), at its control rate (8 samples); its triangle LFO (to pitch, cutoff and PW); velocity
 * into the amp and the filter envelope's depth; the VCA (+6 dB) and its soft knee. The machine keeps the notes: POLY / MONO / LEGATO, glide,
 * bend and the matrix are voice.c's (the module's note stack and glide are not used), and voice.c's LFO and matrix add to the cutoff and PW.
 * The parameters, all the track's own (saved with the sound; the module's values 0..127 as they are):
 *   E0 WAVE osc 1 SAW / PULS   E1 DTUN osc 2 detune (DET - 64)   E2 OSC2 level   E3 SUB level   E4 CUT   E5 RES   E6 PW   E7 NOIS
 *   ENV page (P_ATK .. P_REL) the amp ADSR;  ENV (P_ED_FLT) the filter envelope's depth (FENV - 64);  FILTER's TYPE (P_FTYPE) the mode
 *   FLT ENV page (P_FM1_ATK .. P_FM1_REL) the filter ADSR, P_FM1_LEVEL the key track
 *   no page (the retired FM operators' ids): P_FM2_ATK osc 1 level, _DEC osc 2 wave (>= 64 pulse), _SUS osc 2 semitones (64 = 0), _REL PWM,
 *   _LEVEL velocity; P_FM3_ATK LFO rate, _DEC LFO -> pitch, _SUS LFO -> cutoff, _LEVEL volume
 * The state: ph[0] osc 1, ph[1] osc 2, ph[2] the noise generator, s[0..2] the filter, s[3..5] the LP24 stage, s[6] the sub's level; the
 * envelopes and the LFO in the part's engine memory (sub_vs_t, a slot per voice). */
#define SUB_POLY 4                       /* voices a SUB track may use (POLY / UNISON) */
#define SUB_TRIM 4596                    /* the output trim (Q12, +1 dB): the audition median with the other machines' (-17 dBFS) */
#define SUB_LP24_Q 23170                 /* the second stage's damping (Q14: 1.414, Butterworth) */
#define SUB_ONE (1 << 30)                /* an envelope's full scale (Q30) */
#define SUB_ATK_TGT 1395864371           /* 1.3 x SUB_ONE: the attack aims past the top, like an analogue ADSR */
/* the release, a departure from the module (which ends a voice at -96 dB: a pad's 4 s time constant held it 46 s): it ends at -72 dB and
 * its time constant is held to that of 64 (1.3 s), so a voice is free within ~11 s of its key (regress.c's health: 12 s) */
#define SUB_REL_MAX 64
#define SUB_END (1 << 18)
enum { SUB_IDLE, SUB_A, SUB_D, SUB_R };
/* NANO_PX's columns (tools/four_presets.py): the model, then a SUB sound's values the preset_t has no room for */
enum { NPX_MODEL, NPX_FTYPE, NPX_MONO, NPX_GLIDE, NPX_FA, NPX_FD, NPX_FS, NPX_FR, NPX_KTRK, NPX_O1, NPX_W2, NPX_SEMI, NPX_PWM, NPX_VEL,
       NPX_LRATE, NPX_LPIT, NPX_LCUT, NPX_VOL, NPX_N };

typedef struct {                         /* a voice's envelopes and LFO (the part's engine memory, eng_acid.c acid_t) */
    int32_t av, fv;                      /* the amp and filter envelopes, Q30 */
    int32_t vs;                          /* the velocity's scale, Q15 (the module's vel_scale) */
    uint32_t lfo;                        /* the LFO's phase */
    uint8_t as, fs;                      /* their stages */
} sub_vs_t;
static sub_vs_t *sub_vs(const track_t *t, const voice_t *v);   /* (eng_acid.c: the slot of voice v) */

/* the module's tables: the one-pole coefficient (Q30, per control tick) of a time constant of 1 ms .. 10 s on a cubic knob, and the LFO's
 * step per control tick for 0.05 .. 20 Hz (fm1_subsynth.h jss_time_coef, JSS_LFO_RATE) */
static const int32_t SUB_TC[128] = {214748364, 214748364, 214748364, 178956970, 153391689, 134217728, 97612893, 76695844, 56512727, 42949672, 33554432, 26188824, 20648881, 16777216, 13591668, 11184810, 9336885, 7837531, 6628035, 5651272, 4880644, 4227330, 3677198, 3234162, 2848121, 2520520, 2246321, 2006994, 1801580, 1624420, 1466860, 1330535, 1210531, 1104672, 1010105, 926438, 851500, 784898, 724522, 670669, 621738, 577280, 537139, 500578, 467453, 437013, 409200, 383616, 360195, 338613, 318712, 300347, 283383, 267632, 253061, 239567, 226958, 215221, 204288, 194096, 184555, 175648, 167275, 159450, 152088, 145178, 138690, 132577, 126814, 121381, 116256, 111418, 106839, 102514, 98418, 94536, 90856, 87360, 84043, 80896, 77897, 75050, 72339, 69755, 67293, 64949, 62711, 60574, 58533, 56581, 54718, 52932, 51225, 49593, 48027, 46526, 45086, 43708, 42383, 41112, 39892, 38718, 37590, 36506, 35463, 34461, 33494, 32564, 31668, 30805, 29972, 29169, 28395, 27648, 26927, 26231, 25558, 24908, 24281, 23674, 23087, 22519, 21970, 21438, 20924, 20426, 19943, 19476};
static const uint32_t SUB_LFO[128] = {38952u, 39728u, 42072u, 47520u, 53760u, 62328u, 73232u, 85704u, 100504u, 116864u, 134784u, 155040u, 177640u, 201792u, 227504u, 255552u, 285160u, 317104u, 350608u, 386448u, 423848u, 463576u, 504872u, 548504u, 593696u, 641224u, 690304u, 740952u, 793928u, 849248u, 906128u, 964560u, 1025336u, 1087664u, 1152336u, 1219336u, 1287904u, 1358024u, 1430480u, 1504504u, 1580856u, 1658768u, 1738240u, 1820832u, 1904192u, 1989904u, 2077944u, 2167544u, 2258704u, 2352200u, 2448032u, 2545424u, 2644368u, 2745656u, 2848504u, 2953688u, 3060432u, 3169504u, 3280144u, 3393120u, 3507648u, 3624520u, 3742944u, 3863712u, 3986040u, 4109920u, 4236136u, 4364696u, 4494808u, 4626488u, 4760496u, 4896840u, 5034752u, 5174216u, 5316016u, 5459376u, 5605072u, 5752328u, 5901920u, 6053072u, 6206568u, 6361608u, 6518216u, 6677936u, 6838440u, 7001280u, 7166456u, 7333192u, 7501480u, 7672112u, 7844304u, 8018824u, 8195688u, 8373328u, 8554088u, 8736408u, 8920280u, 9106496u, 9294264u, 9483592u, 9676040u, 9869264u, 10064832u, 10262728u, 10462184u, 10663200u, 10866552u, 11072248u, 11279496u, 11488304u, 11699448u, 11912152u, 12127192u, 12344568u, 12562728u, 12784000u, 13006056u, 13230440u, 13457168u, 13685456u, 13916080u, 14148264u, 14382776u, 14618856u, 14856496u, 15096464u, 15338776u, 15582640u};

static int sub_on(const track_t *t) { return t->p[P_RSV_LN + 1] == 1; }

static inline int32_t sub_clamp(int32_t x, int32_t lim) { return x > lim ? lim : x < -lim ? -lim : x; }
/* the band path's soft knee: linear to 0.75, then 4:1, then a hard limit */
static inline int32_t sub_sat(int32_t x)
{
    if (x > 49152)
        x = 49152 + ((x - 49152) >> 2);
    else if (x < -49152)
        x = -49152 + ((x + 49152) >> 2);
    return sub_clamp(x, 65535);
}
/* the PolyBLEP residual in Q15 for a +2 step at phase 0 (t the phase, dt the increment, 2^32 = one cycle) */
static inline int32_t sub_blep(uint32_t t, uint32_t dt)
{
    uint32_t d = dt >> 16, u;
    int32_t x;
    if (!d)
        return 0;                                    /* sub-audio: nothing to fix */
    if (t < dt) {                                    /* just after the edge */
        x = (int32_t)(((t >> 16) << 15) / d);
        x = x > 32768 ? 32768 : x;
        x = 32768 - x;
        return -((x * x) >> 15);
    }
    u = t + dt;                                      /* just before it (the sum wraps) */
    if (u < dt) {
        x = (int32_t)(((u >> 16) << 15) / d);
        x = x > 32768 ? 32768 : x;
        return (x * x) >> 15;
    }
    return 0;
}
static inline int32_t sub_osc(int pulse, uint32_t t, uint32_t dt, uint32_t pw)
{
    int32_t v;
    if (!pulse)
        return (int32_t)(t >> 16) - 32768 - sub_blep(t, dt);
    v = t < pw ? 32767 : -32768;
    return v + sub_blep(t, dt) - sub_blep(t - pw, dt) - ((int32_t)(pw >> 16) - 32768);   /* (less its mean: a narrow pulse carries no DC) */
}
/* one sample through the filter, twice (2x oversampled: stable to ~15 kHz) */
static inline void sub_svf2x(int32_t *s, int32_t in, int32_t f, int32_t q)
{
    int32_t l = s[0], b = s[1], h = s[2], k;
    for (k = 0; k < 2; k++) {
        l = sub_clamp(l + ((f * b) >> 15), 65535);
        h = sub_clamp(in - l - ((q * b) >> 14), 65535);
        b = sub_sat(b + ((f * h) >> 15));
    }
    s[0] = l, s[1] = b, s[2] = h;
}
/* one control tick of an envelope (the module's jss_env_tick): ADSR values 0..127 */
static int32_t sub_env_tick(int32_t *v, uint8_t *st, const int16_t *adsr)
{
    switch (*st) {
    case SUB_A:
        *v += (int32_t)(((int64_t)(SUB_ATK_TGT - *v) * SUB_TC[adsr[0] & 127]) >> 30);
        if (*v >= SUB_ONE) {
            *v = SUB_ONE;
            *st = SUB_D;
        }
        break;
    case SUB_D:
        *v += (int32_t)(((int64_t)(clamp(adsr[2], 0, 127) * 8454660 - *v) * SUB_TC[adsr[1] & 127]) >> 30);   /* (to the sustain, 127 = SUB_ONE) */
        break;
    case SUB_R:
        *v += (int32_t)(((int64_t)(0 - *v) * SUB_TC[clamp(adsr[3], 0, SUB_REL_MAX)]) >> 30);
        if (*v < SUB_END) {                           /* -72 dB */
            *v = 0;
            *st = SUB_IDLE;
        }
        break;
    default:
        break;
    }
    return *v;
}

static void sub_note_on(track_t *t, voice_t *v)
{
    sub_vs_t *s = sub_vs(t, v);
    int32_t va = clamp(t->p[P_FM2_LEVEL], 0, 127) * 32767 / 127, v15 = v->vel * 32767 / 127;
    if (!v->ph[2])
        v->ph[2] = 0x9E3779B9u ^ ((uint32_t)v->note << 16);   /* the noise generator's seed */
    s->vs = 32768 - va + ((va * v15) >> 15);
    s->as = s->fs = SUB_A;                           /* (a retrigger starts from the level there is: no click) */
}

/* the amp envelope (ownenv: voice.c asks once a block, CTL = 4 control ticks); a key up releases both envelopes */
static int32_t sub_amp(const track_t *t, const voice_t *v)
{
    sub_vs_t *s = sub_vs(t, v);
    int32_t ae, a, k;
    if (!v->gate) {
        if (s->as != SUB_IDLE)
            s->as = SUB_R;
        if (s->fs != SUB_IDLE)
            s->fs = SUB_R;
    }
    for (k = 0; k < (int32_t)(CTL / 8u); k++)
        sub_env_tick(&s->av, &s->as, &t->p[P_ATK]);
    ae = s->av >> 15;
    a = (((ae * s->vs) >> 15) * (clamp(t->p[P_FM3_LEVEL], 0, 127) * 32767 / 127)) >> 15;
    return s->as == SUB_IDLE ? 0 : a > 32767 ? 32767 : a < 0 ? 0 : a;
}
static int sub_done(const track_t *t, const voice_t *v)
{
    const sub_vs_t *s = sub_vs(t, v);
    return s->as == SUB_IDLE;
}

static __attribute__((noinline)) void sub_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)   /* (its own: tests/target_budget.py) */
{
    const int16_t *p = t->p;
    sub_vs_t *s = sub_vs(t, v);
    int p1 = p[P_E0] == 1, p2 = p[P_FM2_DEC] >= 64, ft = p[P_FTYPE] >= 0 && p[P_FTYPE] <= 3 ? p[P_FTYPE] : 0;
    int32_t o1 = clamp(p[P_FM2_ATK], 0, 127) * 32767 / 127, o2 = clamp(p[P_E2], 0, 127) * 32767 / 127;
    int32_t sbl = clamp(p[P_E3], 0, 127) * 32767 / 127, nl = clamp(p[P_E7], 0, 127) * 32767 / 127;
    int32_t semi16 = (clamp(p[P_FM2_SUS], 0, 127) - 64) * 24 / 63 * 16, dct = clamp(p[P_E1], -64, 63) * 50 / 63;
    int32_t pwb = 16384 - clamp(p[P_E6], 0, 127) * 14745 / 127 - (((m->shape - (64 << 8)) * 14745 / 127) >> 8);   /* (m->shape: 64 << 8 = none) */
    int32_t pwm = clamp(p[P_FM2_REL], 0, 127) * 14745 / 127, lpit = clamp(p[P_FM3_DEC], 0, 127) * 512 / 127;
    int32_t lcut = clamp(p[P_FM3_SUS], 0, 127) * 12288 / 127, ktrk = clamp(p[P_FM1_LEVEL], 0, 127) * 32767 / 127;
    int32_t fen = ((int32_t)clamp(p[P_ED_FLT], -64, 63) * 24576 / 63 * s->vs) >> 15;   /* the filter envelope's depth (Q8 semitones) x velocity */
    int32_t q = 32768 - clamp(p[P_E5], 0, 127) * 32368 / 127, a = m->amp0, da = (m->amp1 - m->amp0) / (int32_t)n;
    /* voice.c's ENV x ENV and accent share of m->cutoff is the 303's way, the module's filter envelope stands for it here; what stays (voice.c's
     * LFO, the matrix) in cutoff steps (0.86 semitone) -> Q8 semitones */
    int32_t mc = m->cutoff - ((m->envq15 * p[P_ED_FLT]) >> 7) - (v->vel > 110 ? (m->envq15 * 24) >> 7 : 0);
    int32_t base = 3072 + clamp(p[P_E4], 0, 127) * 31744 / 127 + ((mc * 219) >> 8);
    uint32_t ph1 = v->ph[0], ph2 = v->ph[1], rng = v->ph[2], li = SUB_LFO[clamp(p[P_FM3_ATK], 0, 127)], i0, i;
    int32_t *fs = v->s, sub_hi = v->s[6];
    if (n > CTL)
        n = CTL;
    for (i0 = 0; i0 < n; i0 += 8u) {                  /* the module's control rate: the filter envelope, the LFO, pitch, PW, cutoff */
        uint32_t m8 = n - i0 < 8u ? n - i0 : 8u, pc, inc1, inc2, pw;
        int32_t fe, lfo, cut, hz, f, pw15;
        fe = sub_env_tick(&s->fv, &s->fs, &p[P_FM1_ATK]) >> 15;
        s->lfo += li;
        pc = s->lfo >> 16;
        lfo = pc < 32768u ? (int32_t)(pc * 2u) - 32768 : (int32_t)((65535u - pc) * 2u) - 32768;
        {
            int32_t d16 = ((lfo * lpit) >> 15) >> 4;     /* (Q8 semitones -> 1/16) */
            inc1 = cents_inc(m->pitch16 + d16, 0, m->fine);
            inc2 = cents_inc(m->pitch16 + d16 + semi16, dct, m->fine);
        }
        pw15 = clamp(pwb - ((lfo * pwm) >> 15), 1638, 31129);
        pw = (uint32_t)pw15 << 17;
        cut = base + ((fe * fen) >> 15) + (int32_t)(((int64_t)(m->pitch16 * 16 - 60 * 256) * ktrk) >> 15) + ((lfo * lcut) >> 15);
        hz = (int32_t)(440.0f * fm_exp2f(((float)cut * (1.0f / 256.0f) - 69.0f) * (1.0f / 12.0f)));
        hz = hz > 15000 ? 15000 : hz < 8 ? 8 : hz;
        f = (hz * 38246) >> 15;                         /* pi fc / (2 fs) in Q15 (2x oversampled) */
        for (i = i0; i < i0 + m8; i++) {
            int32_t mix = 0, y, o;
            a += da;
            ph1 += inc1;
            if (ph1 < inc1)
                sub_hi ^= 1;                             /* osc 1 wrapped: the sub flips (an octave down) */
            ph2 += inc2;
            if (o1)
                mix = (sub_osc(p1, ph1, inc1, pw) * o1) >> 17;
            if (o2)
                mix += (sub_osc(p2, ph2, inc2, pw) * o2) >> 17;
            if (sbl) {
                int32_t sq = sub_hi ? 32767 : -32768, bl = sub_blep(ph1, inc1);
                sq += ((ph1 < inc1) ? sub_hi : !sub_hi) ? bl : -bl;
                mix += (sq * sbl) >> 17;
            }
            if (nl) {
                rng = rng * 1664525u + 1013904223u;
                mix += (((int32_t)(rng >> 16) - 32768) * nl) >> 17;
            }
            sub_svf2x(fs, mix, f, q);
            if (ft == 0) {                               /* LP24: a second stage on the low-pass */
                sub_svf2x(fs + 3, fs[0], f, SUB_LP24_Q);
                y = fs[3];
            } else
                y = ft == 1 ? fs[1] : ft == 2 ? fs[2] : fs[0];
            o = (((y * a) >> 14) * SUB_TRIM) >> 12;      /* the VCA (+6 dB), the trim */
            if (o > 24576)                               /* the soft knee: a screaming filter rounds off */
                o = 24576 + ((o - 24576) >> 2);
            else if (o < -24576)
                o = -24576 + ((o + 24576) >> 2);
            out[i] += sub_clamp(o, 32767);
        }
    }
    v->ph[0] = ph1, v->ph[1] = ph2, v->ph[2] = rng, v->s[6] = sub_hi;
}

/* EDIT in the SUB model: the 303's slots and ranges, other labels */
static const char *const N_SUB_WAVE[] = {"SAW", "PULS"};
static const param_desc_t SUB_EDIT[8] = {
    {"WAVE", F_ENUM, 0, 1, 0, N_SUB_WAVE, 0}, {"DTUN", F_BIPCT, -64, 63, 0, 0, 0},
    {"OSC2", F_PCT, 0, 127, 60, 0, 0},        {"SUB", F_PCT, 0, 127, 80, 0, 0},
    {"CUT", F_CUTOFF, 0, 127, 50, 0, 0},      {"RES", F_PCT, 0, 127, 90, 0, 0},
    {"PW", F_PCT, 0, 127, 0, 0, 0},           {"NOIS", F_PCT, 0, 127, 0, 0, 0},
};
